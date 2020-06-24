/*
 * See LICENSE file for copyright and license details.
 */
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>
#include <xkbcommon/xkbcommon.h>

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define WLR_SURFACE(C)          (c->isxdg ? c->xdg_surface->surface : c->xwayland_surface->surface)

/* enums */
enum { CurNormal, CurMove, CurResize }; /* cursor */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct {
	struct wl_list link;
	struct wl_list flink;
	struct wl_list slink;
	union {
		struct wlr_xdg_surface *xdg_surface;
		struct wlr_xwayland_surface *xwayland_surface;
	};
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wlr_box geom;  /* layout-relative, includes border */
	int isxdg;
	Monitor *mon;
	int bw;
	unsigned int tags;
	int isfloating;
} Client;

typedef struct {
	struct wl_listener request_mode;
	struct wl_listener destroy;
} Decoration;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	struct wl_list link;
	struct wlr_input_device *device;

	struct wl_listener modifiers;
	struct wl_listener key;
} Keyboard;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wlr_box m;      /* monitor area, layout-relative */
	struct wlr_box w;      /* window area, layout-relative */
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	double mfact;
	int nmaster;
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
} MonitorRule;

typedef struct {
	const char *id;
	const char *title;
	unsigned int tags;
	int isfloating;
	int monitor;
} Rule;

/* Used to move all of the data necessary to render a surface from the top-level
 * frame handler to the per-surface render function. */
struct render_data {
	struct wlr_output *output;
	struct timespec *when;
	int x, y; /* layout-relative */
};

/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void createkeyboard(struct wlr_input_device *device);
static void createmon(struct wl_listener *listener, void *data);
static void createnotifyxdg(struct wl_listener *listener, void *data);
static void createnotifyxwayland(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_input_device *device);
static void createxdeco(struct wl_listener *listener, void *data);
static void cursorframe(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void destroyxdeco(struct wl_listener *listener, void *data);
static Monitor *dirtomon(int dir);
static void focusclient(Client *c, struct wlr_surface *surface, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void getxdecomode(struct wl_listener *listener, void *data);
static void incnmaster(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static Client *lastfocused(void);
static void maprequest(struct wl_listener *listener, void *data);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void quit(const Arg *arg);
static void render(struct wlr_surface *surface, int sx, int sy, void *data);
static void renderclients(Monitor *m, struct timespec *now);
static void rendermon(struct wl_listener *listener, void *data);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void run(char *startup_cmd);
static void scalebox(struct wlr_box *box, float scale);
static Client *selclient(void);
static void setcursor(struct wl_listener *listener, void *data);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m, unsigned int newtags);
static void setup(void);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unmapnotify(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static Client *xytoclient(double x, double y);
static Monitor *xytomon(double x, double y);

/* variables */
static const char broken[] = "broken";
static struct wl_display *dpy;
static struct wlr_backend *backend;
static struct wlr_renderer *drw;
static struct wlr_compositor *compositor;
static struct wlr_xwayland *xwayland;

static struct wlr_xdg_shell *xdg_shell;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
static struct wl_list stack;   /* stacking z-order */
static struct wlr_xdg_decoration_manager_v1 *xdeco_mgr;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_seat *seat;
static struct wl_list keyboards;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy; /* client-relative */

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;

/* global event handlers */
static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener new_input = {.notify = inputdevice};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdeco = {.notify = createxdeco};
static struct wl_listener new_xdg_surface = {.notify = createnotifyxdg};
static struct wl_listener new_xwayland_surface = {.notify = createnotifyxwayland};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};

/* configuration, allows nested code to access above variables */
#include "config.h"

/* function implementations */
void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->geom.width = MAX(1, c->geom.width);
	c->geom.height = MAX(1, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width + 2 * c->bw <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height + 2 * c->bw <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	const char *appid, *title;
	unsigned int i, newtags = 0;
	const Rule *r;
	Monitor *mon = selmon, *m;

	/* rule matching */
	c->isfloating = 0;
	if (c->isxdg) {
		if (!(appid = c->xdg_surface->toplevel->app_id))
			appid = broken;
		if (!(title = c->xdg_surface->toplevel->title))
			title = broken;
	} else {
		if (!(title = c->xwayland_surface->title))
			title = broken;
	}

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id)))
		{
			c->isfloating = r->isfloating;
			newtags |= r->tags;
			i = 0;
			wl_list_for_each(m, &mons, link)
				if (r->monitor == i++)
					mon = m;
		}
	}
	setmon(c, mon, newtags);
}

void
arrange(Monitor *m)
{
	/* Get effective monitor geometry to use for window area */
	m->m = *wlr_output_layout_get_box(output_layout, m->wlr_output);
	m->w = m->m;
	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	/* XXX recheck pointer focus here... or in resize()? */
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_event_pointer_axis *event = data;
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_event_pointer_button *event = data;
	struct wlr_surface *surface;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	switch (event->state) {
	case WLR_BUTTON_PRESSED:;
		/* Change focus if the button was _pressed_ over a client */
		if ((c = xytoclient(cursor->x, cursor->y))) {
			if (c->isxdg)
				surface = wlr_xdg_surface_surface_at(c->xdg_surface,
						cursor->x - c->geom.x - c->bw,
						cursor->y - c->geom.y - c->bw, NULL, NULL);
			else
				surface = wlr_surface_surface_at(c->xwayland_surface->surface,
						cursor->x - c->geom.x - c->bw,
						cursor->y - c->geom.y - c->bw, NULL, NULL);
			focusclient(c, surface, 1);
		}

		keyboard = wlr_seat_get_keyboard(seat);
		mods = wlr_keyboard_get_modifiers(keyboard);
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) &&
					event->button == b->button && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WLR_BUTTON_RELEASED:
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* XXX should reset to the pointer focus's current setcursor */
		if (cursor_mode != CurNormal) {
			wlr_xcursor_manager_set_cursor_image(cursor_mgr,
					"left_ptr", cursor);
			cursor_mode = CurNormal;
			/* Drop the window off on its new monitor */
			selmon = xytomon(cursor->x, cursor->y);
			setmon(grabc, selmon, 0);
			return;
		}
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
chvt(const Arg *arg)
{
	struct wlr_session *s = wlr_backend_get_session(backend);
	if (!s)
		return;
	wlr_session_change_vt(s, arg->ui);
}

void
createkeyboard(struct wlr_input_device *device)
{
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	Keyboard *kb;

	kb = device->data = calloc(1, sizeof(*kb));
	kb->device = device;

	/* Prepare an XKB keymap and assign it to the keyboard. */
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	keymap = xkb_map_new_from_names(context, &xkb_rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(device->keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	kb->modifiers.notify = keypressmod;
	wl_signal_add(&device->keyboard->events.modifiers, &kb->modifiers);
	kb->key.notify = keypress;
	wl_signal_add(&device->keyboard->events.key, &kb->key);

	wlr_seat_set_keyboard(seat, device);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&keyboards, &kb->link);
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	Monitor *m;
	const MonitorRule *r;

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_set_mode(wlr_output, wlr_output_preferred_mode(wlr_output));

	/* Allocates and configures monitor state using configured rules */
	m = wlr_output->data = calloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
	m->tagset[0] = m->tagset[1] = 1;
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->mfact = r->mfact;
			m->nmaster = r->nmaster;
			wlr_output_set_scale(wlr_output, r->scale);
			wlr_xcursor_manager_load(cursor_mgr, r->scale);
			m->lt[0] = m->lt[1] = r->lt;
			wlr_output_set_transform(wlr_output, r->rr);
			break;
		}
	}
	/* Sets up a listener for the frame notify event. */
	m->frame.notify = rendermon;
	wl_signal_add(&wlr_output->events.frame, &m->frame);
	wl_list_insert(&mons, &m->link);

	wlr_output_enable(wlr_output, 1);
	if (!wlr_output_commit(wlr_output))
		return;

	/* Adds this to the output layout. The add_auto function arranges outputs
	 * from left-to-right in the order they appear. A more sophisticated
	 * compositor would let the user configure the arrangement of outputs in the
	 * layout.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	wlr_output_layout_add_auto(output_layout, wlr_output);
	sgeom = *wlr_output_layout_get_box(output_layout, NULL);
}

void
createnotifyxdg(struct wl_listener *listener, void *data)
{
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup. */
	struct wlr_xdg_surface *xdg_surface = data;
	Client *c;

	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;

	/* Allocate a Client for this surface */
	c = xdg_surface->data = calloc(1, sizeof(*c));
	c->xdg_surface = xdg_surface;
	c->isxdg = 1;
	c->bw = borderpx;

	/* Tell the client not to try anything fancy */
	wlr_xdg_toplevel_set_tiled(c->xdg_surface, WLR_EDGE_TOP |
			WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);

	/* Listen to the various events it can emit */
	c->map.notify = maprequest;
	wl_signal_add(&xdg_surface->events.map, &c->map);
	c->unmap.notify = unmapnotify;
	wl_signal_add(&xdg_surface->events.unmap, &c->unmap);
	c->destroy.notify = destroynotify;
	wl_signal_add(&xdg_surface->events.destroy, &c->destroy);
}

void
createnotifyxwayland(struct wl_listener *listener, void *data)
{
	struct wlr_xwayland_surface *xwayland_surface = data;
	Client *c;

	/* Allocate a Client for this surface */
	c = xwayland_surface->data = calloc(1, sizeof(*c));
	c->xwayland_surface = xwayland_surface;
	c->isxdg = 0;
	c->bw = borderpx;

	/* Listen to the various events it can emit */
	c->map.notify = maprequest;
	wl_signal_add(&xwayland_surface->events.map, &c->map);
	c->unmap.notify = unmapnotify;
	wl_signal_add(&xwayland_surface->events.unmap, &c->unmap);
	c->destroy.notify = destroynotify;
	wl_signal_add(&xwayland_surface->events.destroy, &c->destroy);
}

void
createpointer(struct wlr_input_device *device)
{
	/* We don't do anything special with pointers. All of our pointer handling
	 * is proxied through wlr_cursor. On another compositor, you might take this
	 * opportunity to do libinput configuration on the device to set
	 * acceleration, etc. */
	wlr_cursor_attach_input_device(cursor, device);
}

void
createxdeco(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	Decoration *d = wlr_deco->data = calloc(1, sizeof(*d));

	wl_signal_add(&wlr_deco->events.request_mode, &d->request_mode);
	d->request_mode.notify = getxdecomode;
	wl_signal_add(&wlr_deco->events.destroy, &d->destroy);
	d->destroy.notify = destroyxdeco;

	getxdecomode(&d->request_mode, wlr_deco);
}


void
cursorframe(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is destroyed and should never be shown again. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
	wl_list_remove(&c->destroy.link);
	free(c);
}

void
destroyxdeco(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	Decoration *d = wlr_deco->data;

	wl_list_remove(&d->destroy.link);
	wl_list_remove(&d->request_mode.link);
	free(d);
}

Monitor *
dirtomon(int dir)
{
	Monitor *m;

	if (dir > 0) {
		if (selmon->link.next == &mons)
			return wl_container_of(mons.next, m, link);
		return wl_container_of(selmon->link.next, m, link);
	} else {
		if (selmon->link.prev == &mons)
			return wl_container_of(mons.prev, m, link);
		return wl_container_of(selmon->link.prev, m, link);
	}
}

void
focusclient(Client *c, struct wlr_surface *surface, int lift)
{
	Client *sel = selclient();
	struct wlr_keyboard *kb;
	/* Previous and new xdg toplevel surfaces */
	Client *ptl = sel;
	Client *tl = c;
	/* Previously focused surface */
	struct wlr_surface *psurface = seat->keyboard_state.focused_surface;

	if (c) {
		/* assert(VISIBLEON(c, c->mon)); ? */
		/* Use top-level wlr_surface if nothing more specific given */
		if (!surface)
			surface = WLR_SURFACE(c);

		/* Focus the correct monitor (must come after selclient!) */
		selmon = c->mon;

		/* Move the client to the front of the focus stack */
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);

		/* Also raise client in stacking order if requested */
		if (lift) {
			wl_list_remove(&c->slink);
			wl_list_insert(&stack, &c->slink);
		}
	}

	/*
	 * If the focused surface has changed, tell the seat to have the
	 * keyboard enter the new surface.  wlroots will keep track of this and
	 * automatically send key events to the appropriate clients.  If surface
	 * is NULL, we clear the focus instead.
	 */
	if (!surface) {
		wlr_seat_keyboard_notify_clear_focus(seat);
	} else if (surface != psurface) {
		kb = wlr_seat_get_keyboard(seat);
		wlr_seat_keyboard_notify_enter(seat, surface,
				kb->keycodes, kb->num_keycodes, &kb->modifiers);
	}

	/*
	 * If the focused toplevel has changed, deactivate the old one and
	 * activate the new one.  This lets the clients know to repaint
	 * accordingly, e.g. show/hide a caret.
	 */
	if (tl != ptl && ptl) {
		if (ptl->isxdg)
			wlr_xdg_toplevel_set_activated(ptl->xdg_surface, 0);
		else
			wlr_xwayland_surface_activate(ptl->xwayland_surface, 0);
	}
	if (tl != ptl && tl) {
		if (tl->isxdg)
			wlr_xdg_toplevel_set_activated(tl->xdg_surface, 1);
		else
			wlr_xwayland_surface_activate(tl->xwayland_surface, 1);
	}
}

void
focusmon(const Arg *arg)
{
	Monitor *m = dirtomon(arg->i);

	if (m == selmon)
		return;
	selmon = m;
	focusclient(lastfocused(), NULL, 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = selclient();
	if (!sel)
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;  /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break;  /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue;  /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break;  /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, NULL, 1);
}

void
getxdecomode(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *wlr_deco = data;
	wlr_xdg_toplevel_decoration_v1_set_mode(wlr_deco,
			WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(device);
		break;
	default:
		/* XXX handle other input device types */
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* XXX do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&keyboards))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

int
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	int handled = 0;
	const Key *k;
	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod) &&
				sym == k->keysym && k->func) {
			k->func(&k->arg);
			handled = 1;
		}
	}
	return handled;
}

void
keypress(struct wl_listener *listener, void *data)
{
	/* This event is raised when a key is pressed or released. */
	Keyboard *kb = wl_container_of(listener, kb, key);
	struct wlr_event_keyboard_key *event = data;
	int i;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			kb->device->keyboard->xkb_state, keycode, &syms);

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(kb->device->keyboard);
	/* On _press_, attempt to process a compositor keybinding. */
	if (event->state == WLR_KEY_PRESSED)
		for (i = 0; i < nsyms; i++)
			handled = keybinding(mods, syms[i]) || handled;

	if (!handled) {
		/* Pass unhandled keycodes along to the client. */
		wlr_seat_set_keyboard(seat, kb->device);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	Keyboard *kb = wl_container_of(listener, kb, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(seat, kb->device);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
		&kb->device->keyboard->modifiers);
}

Client *
lastfocused(void)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink)
		if (VISIBLEON(c, selmon))
			return c;
	return NULL;
}

void
maprequest(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *c = wl_container_of(listener, c, map);
	/* Insert this client into client lists. */
	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);
	wl_list_insert(&stack, &c->slink);

	if (c->isxdg) {
		wlr_xdg_surface_get_geometry(c->xdg_surface, &c->geom);
		c->geom.width += 2 * c->bw;
		c->geom.height += 2 * c->bw;
	} else {
		c->geom.x = c->xwayland_surface->x;
		c->geom.y = c->xwayland_surface->y;
		c->geom.width = c->xwayland_surface->width + 2 * c->bw;
		c->geom.height = c->xwayland_surface->height + 2 * c->bw;
	}

	/* Set initial monitor, tags, floating status, and focus */
	applyrules(c);
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(cursor, event->device, event->x, event->y);
	motionnotify(event->time_msec);
}

void
motionnotify(uint32_t time)
{
	double sx = 0, sy = 0;
	struct wlr_surface *surface = NULL;
	Client *c;

	/* Update selmon (even while dragging a window) */
	if (sloppyfocus)
		selmon = xytomon(cursor->x, cursor->y);

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position. */
		resize(grabc, cursor->x - grabcx, cursor->y - grabcy,
				grabc->geom.width, grabc->geom.height, 1);
		return;
	} else if (cursor_mode == CurResize) {
		resize(grabc, grabc->geom.x, grabc->geom.y,
				cursor->x - grabc->geom.x,
				cursor->y - grabc->geom.y, 1);
		return;
	}

	/* Otherwise, find the client under the pointer and send the event along. */
	if ((c = xytoclient(cursor->x, cursor->y))) {
		if (c->isxdg)
			surface = wlr_xdg_surface_surface_at(c->xdg_surface,
					cursor->x - c->geom.x - c->bw,
					cursor->y - c->geom.y - c->bw, &sx, &sy);
		else
			surface = wlr_surface_surface_at(c->xwayland_surface->surface,
					cursor->x - c->geom.x - c->bw,
					cursor->y - c->geom.y - c->bw, &sx, &sy);
	}
	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface)
		wlr_xcursor_manager_set_cursor_image(cursor_mgr,
				"left_ptr", cursor);

	pointerfocus(c, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_event_pointer_motion *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(cursor, event->device,
			event->delta_x, event->delta_y);
	motionnotify(event->time_msec);
}

void
moveresize(const Arg *arg)
{
	grabc = xytoclient(cursor->x, cursor->y);
	if (!grabc)
		return;

	/* Float the window and tell motionnotify to grab it */
	setfloating(grabc, 1);
	switch (cursor_mode = arg->ui) {
	case CurMove:
		grabcx = cursor->x - grabc->geom.x;
		grabcy = cursor->y - grabc->geom.y;
		wlr_xcursor_manager_set_cursor_image(cursor_mgr, "fleur", cursor);
		break;
	case CurResize:
		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started */
		wlr_cursor_warp_closest(cursor, NULL,
				grabc->geom.x + grabc->geom.width,
				grabc->geom.y + grabc->geom.height);
		wlr_xcursor_manager_set_cursor_image(cursor_mgr,
				"bottom_right_corner", cursor);
		break;
	}
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	/* Use top level surface if nothing more specific given */
	if (c && !surface)
		surface = WLR_SURFACE(c);

	/* If surface is already focused, only notify of motion */
	if (surface && surface == seat->pointer_state.focused_surface) {
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
		return;
	}

	/* If surface is NULL, clear pointer focus, otherwise let the client
	 * know that the mouse cursor has entered one of its surfaces. */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	/* If keyboard focus follows mouse, enforce that */
	if (sloppyfocus)
		focusclient(c, surface, 0);
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
render(struct wlr_surface *surface, int sx, int sy, void *data)
{
	/* This function is called for every surface that needs to be rendered. */
	struct render_data *rdata = data;
	struct wlr_output *output = rdata->output;
	double ox = 0, oy = 0;
	struct wlr_box obox;
	float matrix[9];
	enum wl_output_transform transform;

	/* We first obtain a wlr_texture, which is a GPU resource. wlroots
	 * automatically handles negotiating these with the client. The underlying
	 * resource could be an opaque handle passed from the client, or the client
	 * could have sent a pixel buffer which we copied to the GPU, or a few other
	 * means. You don't have to worry about this, wlroots takes care of it. */
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (!texture)
		return;

	/* The client has a position in layout coordinates. If you have two displays,
	 * one next to the other, both 1080p, a client on the rightmost display might
	 * have layout coordinates of 2000,100. We need to translate that to
	 * output-local coordinates, or (2000 - 1920). */
	wlr_output_layout_output_coords(output_layout, output, &ox, &oy);

	/* We also have to apply the scale factor for HiDPI outputs. This is only
	 * part of the puzzle, dwl does not fully support HiDPI. */
	obox.x = ox + rdata->x + sx;
	obox.y = oy + rdata->y + sy;
	obox.width = surface->current.width;
	obox.height = surface->current.height;
	scalebox(&obox, output->scale);

	/*
	 * Those familiar with OpenGL are also familiar with the role of matrices
	 * in graphics programming. We need to prepare a matrix to render the
	 * client with. wlr_matrix_project_box is a helper which takes a box with
	 * a desired x, y coordinates, width and height, and an output geometry,
	 * then prepares an orthographic projection and multiplies the necessary
	 * transforms to produce a model-view-projection matrix.
	 *
	 * Naturally you can do this any way you like, for example to make a 3D
	 * compositor.
	 */
	transform = wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &obox, transform, 0,
		output->transform_matrix);

	/* This takes our matrix, the texture, and an alpha, and performs the actual
	 * rendering on the GPU. */
	wlr_render_texture_with_matrix(drw, texture, matrix, 1);

	/* This lets the client know that we've displayed that frame and it can
	 * prepare another one now if it likes. */
	wlr_surface_send_frame_done(surface, rdata->when);
}

void
renderclients(Monitor *m, struct timespec *now)
{
	Client *c;
	double ox, oy;
	int i, w, h;
	struct render_data rdata;
	struct wlr_box *borders;
	struct wlr_surface *surface;
	/* Each subsequent window we render is rendered on top of the last. Because
	 * our stacking list is ordered front-to-back, we iterate over it backwards. */
	wl_list_for_each_reverse(c, &stack, slink) {
		/* Only render visible clients which show on this monitor */
		if (!VISIBLEON(c, c->mon) || !wlr_output_layout_intersects(
					output_layout, m->wlr_output, &c->geom))
			continue;

		surface = WLR_SURFACE(c);
		ox = c->geom.x, oy = c->geom.y;
		wlr_output_layout_output_coords(output_layout, m->wlr_output,
				&ox, &oy);
		w = surface->current.width;
		h = surface->current.height;
		borders = (struct wlr_box[4]) {
			{ox, oy, w + 2 * c->bw, c->bw},             /* top */
			{ox, oy + c->bw, c->bw, h},                 /* left */
			{ox + c->bw + w, oy + c->bw, c->bw, h},     /* right */
			{ox, oy + c->bw + h, w + 2 * c->bw, c->bw}, /* bottom */
		};
		for (i = 0; i < 4; i++) {
			scalebox(&borders[i], m->wlr_output->scale);
			wlr_render_rect(drw, &borders[i], bordercolor,
					m->wlr_output->transform_matrix);
		}

		/* This calls our render function for each surface among the
		 * xdg_surface's toplevel and popups. */
		rdata.output = m->wlr_output,
		rdata.when = now,
		rdata.x = c->geom.x + c->bw,
		rdata.y = c->geom.y + c->bw;
		if (c->isxdg)
			wlr_xdg_surface_for_each_surface(c->xdg_surface, render, &rdata);
		else
			wlr_surface_for_each_surface(c->xwayland_surface->surface, render, &rdata);
	}
}

void
rendermon(struct wl_listener *listener, void *data)
{
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	/* wlr_output_attach_render makes the OpenGL context current. */
	if (!wlr_output_attach_render(m->wlr_output, NULL))
		return;

	/* Begin the renderer (calls glViewport and some other GL sanity checks) */
	wlr_renderer_begin(drw, m->wlr_output->width, m->wlr_output->height);
	wlr_renderer_clear(drw, rootcolor);

	renderclients(m, &now);

	/* Hardware cursors are rendered by the GPU on a separate plane, and can be
	 * moved around without re-rendering what's beneath them - which is more
	 * efficient. However, not all hardware supports hardware cursors. For this
	 * reason, wlroots provides a software fallback, which we ask it to render
	 * here. wlr_cursor handles configuring hardware vs software cursors for you,
	 * and this function is a no-op when hardware cursors are in use. */
	wlr_output_render_software_cursors(m->wlr_output, NULL);

	/* Conclude rendering and swap the buffers, showing the final frame
	 * on-screen. */
	wlr_renderer_end(drw);
	wlr_output_commit(m->wlr_output);
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	/*
	 * Note that I took some shortcuts here. In a more fleshed-out
	 * compositor, you'd wait for the client to prepare a buffer at
	 * the new size, then commit any movement that was prepared.
	 */
	struct wlr_box *bbox = interact ? &sgeom : &c->mon->w;
	c->geom.x = x;
	c->geom.y = y;
	c->geom.width = w;
	c->geom.height = h;
	applybounds(c, bbox);
	/* wlroots makes this a no-op if size hasn't changed */
	if (c->isxdg)
		wlr_xdg_toplevel_set_size(c->xdg_surface,
				c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
	else
		wlr_xwayland_surface_configure(c->xwayland_surface,
				c->geom.x, c->geom.y,
				c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
}

void
run(char *startup_cmd)
{
	pid_t startup_pid = -1;

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket) {
		perror("startup: display_add_socket_auto");
		wlr_backend_destroy(backend);
		exit(EXIT_FAILURE);
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend)) {
		perror("startup: backend_start");
		wlr_backend_destroy(backend);
		wl_display_destroy(dpy);
		exit(EXIT_FAILURE);
	}

	/* Now that outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* XXX hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping.  still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);

	/* Set the WAYLAND_DISPLAY environment variable to our socket and run the
	 * startup command if requested. */
	setenv("WAYLAND_DISPLAY", socket, 1);
	if (startup_cmd) {
		startup_pid = fork();
		if (startup_pid < 0) {
			perror("startup: fork");
			wl_display_destroy(dpy);
			exit(EXIT_FAILURE);
		}
		if (startup_pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			perror("startup: execl");
			wl_display_destroy(dpy);
			exit(EXIT_FAILURE);
		}
	}
	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);
	wl_display_run(dpy);

	if (startup_cmd) {
		kill(startup_pid, SIGTERM);
		waitpid(startup_pid, NULL, 0);
	}
}

void
scalebox(struct wlr_box *box, float scale)
{
	box->x *= scale;
	box->y *= scale;
	box->width *= scale;
	box->height *= scale;
}

Client *
selclient(void)
{
	Client *c = wl_container_of(fstack.next, c, flink);
	if (wl_list_empty(&fstack) || !VISIBLEON(c, selmon))
		return NULL;
	return c;
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image */
	/* XXX still need to save the provided surface to restore later */
	if (cursor_mode != CurNormal)
		return;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided surface as the cursor image. It will set the
	 * hardware cursor on the output that it's currently on and continue to
	 * do so as the cursor moves between outputs. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setfloating(Client *c, int floating)
{
	if (c->isfloating == floating)
		return;
	c->isfloating = floating;
	arrange(c->mon);
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	/* XXX change layout symbol? */
	arrange(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if (f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setmon(Client *c, Monitor *m, unsigned int newtags)
{
	int hadfocus;
	Monitor *oldmon = c->mon;
	struct wlr_surface *surface = WLR_SURFACE(c);
	if (oldmon == m)
		return;
	hadfocus = (c == selclient());
	c->mon = m;
	/* XXX leave/enter is not optimal but works */
	if (oldmon) {
		wlr_surface_send_leave(surface, oldmon->wlr_output);
		arrange(oldmon);
	}
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		applybounds(c, &m->m);
		wlr_surface_send_enter(surface, m->wlr_output);
		c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
		arrange(m);
	}
	/* Focus can change if c is the top of selmon before or after */
	if (hadfocus || c == selclient())
		focusclient(lastfocused(), NULL, 1);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor
	 */
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor
	 */
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void
setup(void)
{
	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. The NULL argument here optionally allows you
	 * to pass in a custom renderer if wlr_renderer doesn't meet your needs. The
	 * backend uses the renderer, for example, to fall back to software cursors
	 * if the backend does not support hardware cursors (some older GPUs
	 * don't). */
	backend = wlr_backend_autocreate(dpy, NULL);

	/* If we don't provide a renderer, autocreate makes a GLES2 renderer for us.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	drw = wlr_backend_get_renderer(backend);
	wlr_renderer_init_wl_display(drw, dpy);

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	compositor = wlr_compositor_create(dpy, drw);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_primary_selection_v1_device_manager_create(dpy);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create();
	wlr_xdg_output_manager_v1_create(dpy, output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	wl_signal_add(&backend->events.new_output, &new_output);

	/* Set up our client lists and the xdg-shell. The xdg-shell is a
	 * Wayland protocol which is used for application windows. For more
	 * detail on shells, refer to the article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	wl_list_init(&clients);
	wl_list_init(&fstack);
	wl_list_init(&stack);
	xdg_shell = wlr_xdg_shell_create(dpy);
	wl_signal_add(&xdg_shell->events.new_surface, &new_xdg_surface);

	/* Use xdg_decoration protocol to negotiate server-side decorations */
	xdeco_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdeco_mgr->events.new_toplevel_decoration, &new_xdeco);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). Scaled cursors will be loaded with each output. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in my
	 * input handling blog post:
	 *
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute,
			&cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_list_init(&keyboards);
	wl_signal_add(&backend->events.new_input, &new_input);
	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor,
			&request_cursor);
	wl_signal_add(&seat->events.request_set_selection,
			&request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection,
			&request_set_psel);

	/*
	 * Initialise the XWayland X server.
	 * It will be started when the first X client is started.
	 */
	xwayland = wlr_xwayland_create(dpy, compositor, true);
	if (xwayland) {
		wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

		setenv("DISPLAY", xwayland->display_name, true);
	} else {
		fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
	}
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwl: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(EXIT_FAILURE);
	}
}

void
tag(const Arg *arg)
{
	Client *sel = selclient();
	if (sel && arg->ui & TAGMASK) {
		sel->tags = arg->ui & TAGMASK;
		focusclient(lastfocused(), NULL, 1);
		arrange(selmon);
	}
}

void
tagmon(const Arg *arg)
{
	Client *sel = selclient();
	if (!sel)
		return;
	setmon(sel, dirtomon(arg->i), 0);
}

void
tile(Monitor *m)
{
	unsigned int i, n = 0, h, mw, my, ty;
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating)
			n++;
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->w.width * m->mfact : 0;
	else
		mw = m->w.width;
	i = my = ty = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating)
			continue;
		if (i < m->nmaster) {
			h = (m->w.height - my) / (MIN(n, m->nmaster) - i);
			resize(c, m->w.x, m->w.y + my, mw, h, 0);
			my += c->geom.height;
		} else {
			h = (m->w.height - ty) / (n - i);
			resize(c, m->w.x + mw, m->w.y + ty, m->w.width - mw, h, 0);
			ty += c->geom.height;
		}
		i++;
	}
}

void
togglefloating(const Arg *arg)
{
	Client *sel = selclient();
	if (!sel)
		return;
	/* return if fullscreen */
	setfloating(sel, !sel->isfloating /* || sel->isfixed */);
}

void
toggletag(const Arg *arg)
{
	unsigned int newtags;
	Client *sel = selclient();
	if (!sel)
		return;
	newtags = sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		sel->tags = newtags;
		focusclient(lastfocused(), NULL, 1);
		arrange(selmon);
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focusclient(lastfocused(), NULL, 1);
		arrange(selmon);
	}
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	setmon(c, NULL, 0);
	wl_list_remove(&c->link);
	wl_list_remove(&c->flink);
	wl_list_remove(&c->slink);
}

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focusclient(lastfocused(), NULL, 1);
	arrange(selmon);
}

Client *
xytoclient(double x, double y)
{
	/* Find the topmost visible client (if any) at point (x, y), including
	 * borders. This relies on stack being ordered from top to bottom. */
	Client *c;
	wl_list_for_each(c, &stack, slink)
		if (VISIBLEON(c, c->mon) && wlr_box_contains_point(&c->geom, x, y))
			return c;
	return NULL;
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	enum wlr_log_importance loglevel = WLR_ERROR;

	int c;
	while ((c = getopt(argc, argv, "qvds:h")) != -1) {
		switch (c) {
		case 'q':
			loglevel = WLR_SILENT;
			break;
		case 'v':
			loglevel = WLR_INFO;
			break;
		case 'd':
			loglevel = WLR_DEBUG;
			break;
		case 's':
			startup_cmd = optarg;
			break;
		default:
			goto usage;
		}
	}
	if (optind < argc)
		goto usage;
	wlr_log_init(loglevel, NULL);

	// Wayland requires XDG_RUNTIME_DIR for creating its communications
	// socket
	if (!getenv("XDG_RUNTIME_DIR")) {
		fprintf(stderr, "XDG_RUNTIME_DIR must be set\n");
		exit(EXIT_FAILURE);
	}

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	dpy = wl_display_create();

	setup();
	run(startup_cmd);

	/* Once wl_display_run returns, we shut down the server. */
	wlr_xwayland_destroy(xwayland);
	wl_display_destroy_clients(dpy);
	wl_display_destroy(dpy);
	return EXIT_SUCCESS;

usage:
	printf("Usage: %s [-qvd] [-s startup command]\n", argv[0]);
	return EXIT_FAILURE;
}
