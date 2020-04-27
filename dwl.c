/*
 * See LICENSE file for copyright and license details.
 */
#define _POSIX_C_SOURCE 200112L
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <linux/input-event-codes.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)

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
	struct wlr_xdg_surface *xdg_surface;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	Monitor *mon;
	int x, y, w, h; /* layout-relative, includes border */
	int bw;
	unsigned int tags;
	int isfloating;
} Client;

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

/* Used to move all of the data necessary to render a surface from the top-level
 * frame handler to the per-surface render function. */
struct render_data {
	struct wlr_output *output;
	struct timespec *when;
	int x, y; /* layout-relative */
};

/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void arrange(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void createkeyboard(struct wlr_input_device *device);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_input_device *device);
static void cursorframe(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static Monitor *dirtomon(int dir);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void incnmaster(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static bool keybinding(uint32_t mods, xkb_keysym_t sym);
static void keyboardfocus(Client *c, struct wlr_surface *surface);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static void maprequest(struct wl_listener *listener, void *data);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time);
static void motionrelative(struct wl_listener *listener, void *data);
static void movemouse(const Arg *arg);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void quit(const Arg *arg);
static void raiseclient(Client *c);
static void refocus(void);
static void render(struct wlr_surface *surface, int sx, int sy, void *data);
static void renderclients(Monitor *m, struct timespec *now);
static void rendermon(struct wl_listener *listener, void *data);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizemouse(const Arg *arg);
static void run(char *startup_cmd);
static void scalebox(struct wlr_box *box, float scale);
static Client *selclient(void);
static void setcursor(struct wl_listener *listener, void *data);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m);
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
static Client *xytoclient(double x, double y,
		struct wlr_surface **surface, double *sx, double *sy);
static Monitor *xytomon(double x, double y);

/* variables */
static struct wl_display *dpy;
static struct wlr_backend *backend;
static struct wlr_renderer *drw;

static struct wlr_xdg_shell *xdg_shell;
static struct wl_listener new_xdg_surface;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
static struct wl_list stack;   /* stacking z-order */

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;
static struct wl_listener cursor_motion;
static struct wl_listener cursor_motion_absolute;
static struct wl_listener cursor_button;
static struct wl_listener cursor_axis;
static struct wl_listener cursor_frame;

static struct wlr_seat *seat;
static struct wl_listener new_input;
static struct wl_listener request_cursor;
static struct wl_list keyboards;
static unsigned int cursor_mode;
static Client *grabc;
static double grabsx, grabsy; /* surface-relative */

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static struct wl_listener new_output;
static Monitor *selmon;

/* configuration, allows nested code to access above variables */
#include "config.h"

void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->w = MAX(1, c->w);
	c->h = MAX(1, c->h);

	if (c->x >= bbox->x + bbox->width)
		c->x = bbox->x + bbox->width - c->w;
	if (c->y >= bbox->y + bbox->height)
		c->y = bbox->y + bbox->height - c->h;
	if (c->x + c->w + 2 * c->bw <= bbox->x)
		c->x = bbox->x;
	if (c->y + c->h + 2 * c->bw <= bbox->y)
		c->y = bbox->y;
}

void
arrange(Monitor *m)
{
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
chvt(const Arg *arg)
{
	struct wlr_session *s = wlr_backend_get_session(backend);
	if (!s)
		return;
	wlr_session_change_vt(s, arg->ui);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct wlr_event_pointer_button *event = data;
	/* Notify the client with pointer focus that a button press has occurred */
	/* XXX probably don't want to pass the event if it's handled by the
	 * compositor at the bottom of this function */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		/* XXX should reset to the pointer focus's current setcursor */
		if (cursor_mode != CurNormal)
			wlr_xcursor_manager_set_cursor_image(cursor_mgr,
					"left_ptr", cursor);
		cursor_mode = CurNormal;
		return;
	}

	/* Change focus if the button was _pressed_ over a client */
	double sx, sy;
	struct wlr_surface *surface;
	Client *c = xytoclient(cursor->x, cursor->y, &surface, &sx, &sy);
	if (c) {
		keyboardfocus(c, surface);
		raiseclient(c);
	}

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	uint32_t mods = wlr_keyboard_get_modifiers(keyboard);
	for (int i = 0; i < LENGTH(buttons); i++)
		if (event->button == buttons[i].button &&
				CLEANMASK(mods) == CLEANMASK(buttons[i].mod) &&
				buttons[i].func)
			buttons[i].func(&buttons[i].arg);
}

void
createkeyboard(struct wlr_input_device *device)
{
	Keyboard *kb = device->data = calloc(1, sizeof(*kb));
	kb->device = device;

	/* Prepare an XKB keymap and assign it to the keyboard. */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_map_new_from_names(context, &xkb_rules,
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

	/* Some backends don't have modes. DRM+KMS does, and we need to set a mode
	 * before we can use the output. The mode is a tuple of (width, height,
	 * refresh rate), and each monitor supports only a specific set of modes. We
	 * just pick the monitor's preferred mode, a more sophisticated compositor
	 * would let the user configure it. */
	if (!wl_list_empty(&wlr_output->modes)) {
		struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
		wlr_output_set_mode(wlr_output, mode);
		wlr_output_enable(wlr_output, true);
		if (!wlr_output_commit(wlr_output))
			return;
	}

	/* Allocates and configures monitor state using configured rules */
	Monitor *m = wlr_output->data = calloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
	m->tagset[0] = m->tagset[1] = 1;
	int i;
	for (i = 0; i < LENGTH(monrules); i++)
		if (!monrules[i].name ||
				!strcmp(wlr_output->name, monrules[i].name)) {
			m->mfact = monrules[i].mfact;
			m->nmaster = monrules[i].nmaster;
			wlr_output_set_scale(wlr_output, monrules[i].scale);
			wlr_xcursor_manager_load(cursor_mgr, monrules[i].scale);
			m->lt[0] = m->lt[1] = monrules[i].lt;
			wlr_output_set_transform(wlr_output, monrules[i].rr);
			break;
		}
	/* Sets up a listener for the frame notify event. */
	m->frame.notify = rendermon;
	wl_signal_add(&wlr_output->events.frame, &m->frame);
	wl_list_insert(&mons, &m->link);

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
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup. */
	struct wlr_xdg_surface *xdg_surface = data;
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return;

	/* Allocate a Client for this surface */
	Client *c = xdg_surface->data = calloc(1, sizeof(*c));
	c->xdg_surface = xdg_surface;
	c->bw = borderpx;
	struct wlr_box geom;
	wlr_xdg_surface_get_geometry(c->xdg_surface, &geom);
	c->w = geom.width + 2 * c->bw;
	c->h = geom.height + 2 * c->bw;

	/* Tell the client not to try anything fancy */
	wlr_xdg_toplevel_set_tiled(c->xdg_surface, true);

	/* Listen to the various events it can emit */
	c->map.notify = maprequest;
	wl_signal_add(&xdg_surface->events.map, &c->map);
	c->unmap.notify = unmapnotify;
	wl_signal_add(&xdg_surface->events.unmap, &c->unmap);
	c->destroy.notify = destroynotify;
	wl_signal_add(&xdg_surface->events.destroy, &c->destroy);
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
	free(c);
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
focusmon(const Arg *arg)
{
	Monitor *m = dirtomon(arg->i);

	if (m == selmon)
		return;
	selmon = m;
	refocus();
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *sel = selclient();
	if (!sel)
		return;
	Client *c;
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
	keyboardfocus(c, NULL);
	raiseclient(c);
}

void
incnmaster(const Arg *arg)
{
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
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
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&keyboards))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	wlr_seat_set_capabilities(seat, caps);
}

bool
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	bool handled = false;
	for (int i = 0; i < LENGTH(keys); i++)
		if (sym == keys[i].keysym &&
				CLEANMASK(mods) == CLEANMASK(keys[i].mod) &&
				keys[i].func) {
			keys[i].func(&keys[i].arg);
			handled = true;
		}
	return handled;
}

void
keyboardfocus(Client *c, struct wlr_surface *surface)
{
	if (c) {
		/* assert(VISIBLEON(c, c->mon)); ? */
		/* If no surface provided, use the client's xdg_surface */
		if (!surface)
			surface = c->xdg_surface->surface;
		/* Focus the correct monitor as well */
		selmon = c->mon;
	}

	/* XXX Need to understand xdg toplevel/popups to know if there's more
	 * simplification that can be done in this function */
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	/* Don't re-focus an already focused surface. */
	if (prev_surface == surface)
		return;
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the
		 * client know it no longer has focus and the client will
		 * repaint accordingly, e.g. stop displaying a caret.
		 */
		struct wlr_xdg_surface *previous = wlr_xdg_surface_from_wlr_surface(
					seat->keyboard_state.focused_surface);
		wlr_xdg_toplevel_set_activated(previous, false);
	}
	/*
	 * Tell the seat to have the keyboard enter this surface.
	 * wlroots will keep track of this and automatically send key
	 * events to the appropriate clients without additional work on
	 * your part.  If surface == NULL, this will clear focus.
	 */
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
	wlr_seat_keyboard_notify_enter(seat, surface,
			kb->keycodes, kb->num_keycodes, &kb->modifiers);
	if (c) {
		/* Move the client to the front of the focus stack */
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		/* Activate the new surface */
		wlr_xdg_toplevel_set_activated(c->xdg_surface, true);
	}
}

void
keypress(struct wl_listener *listener, void *data)
{
	/* This event is raised when a key is pressed or released. */
	Keyboard *kb = wl_container_of(listener, kb, key);
	struct wlr_event_keyboard_key *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			kb->device->keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t mods = wlr_keyboard_get_modifiers(kb->device->keyboard);
	/* On _press_, attempt to process a compositor keybinding. */
	if (event->state == WLR_KEY_PRESSED)
		for (int i = 0; i < nsyms; i++)
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

void
maprequest(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *c = wl_container_of(listener, c, map);
	/* XXX Apply client rules here */
	/* Insert this client into the list and put it on selmon. */
	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);
	wl_list_insert(&stack, &c->slink);
	setmon(c, selmon);
	keyboardfocus(c, c->xdg_surface->surface);
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
	/* Update selmon (even while dragging a window) */
	if (sloppyfocus)
		selmon = xytomon(cursor->x, cursor->y);

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position. */
		/* XXX assumes the surface is at (0,0) within grabc */
		resize(grabc, cursor->x - grabsx - grabc->bw,
				cursor->y - grabsy - grabc->bw,
				grabc->w, grabc->h, 1);
		return;
	} else if (cursor_mode == CurResize) {
		/*
		 * Note that I took some shortcuts here. In a more fleshed-out
		 * compositor, you'd wait for the client to prepare a buffer at
		 * the new size, then commit any movement that was prepared.
		 */
		resize(grabc, grabc->x, grabc->y,
				cursor->x - grabc->x, cursor->y - grabc->y, 1);
		return;
	}

	/* Otherwise, find the client under the pointer and send the event along. */
	double sx = 0, sy = 0;
	struct wlr_surface *surface = NULL;
	Client *c = xytoclient(cursor->x, cursor->y, &surface, &sx, &sy);
	/* If there's no client under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * around the screen, not over any clients. */
	if (!c)
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
movemouse(const Arg *arg)
{
	struct wlr_surface *surface;
	grabc = xytoclient(cursor->x, cursor->y, &surface, &grabsx, &grabsy);
	if (!grabc)
		return;

	/* Float the window and tell motionnotify to grab it */
	if (!grabc->isfloating && selmon->lt[selmon->sellt]->arrange)
		grabc->isfloating = 1;
	cursor_mode = CurMove;
	wlr_xcursor_manager_set_cursor_image(cursor_mgr, "fleur", cursor);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	/* If surface is already focused, only notify of motion */
	if (surface && surface == seat->pointer_state.focused_surface) {
		wlr_seat_pointer_notify_motion(seat, time, sx, sy);
		return;
	}
	/* If surface is NULL, clear pointer focus, otherwise let the client
	 * know that the mouse cursor has entered one of its surfaces. */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	/* If keyboard focus follows mouse, enforce that */
	if (sloppyfocus && surface)
		keyboardfocus(c, surface);
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
refocus(void)
{
	Client *c = NULL, *next;
	wl_list_for_each(next, &fstack, flink) {
		if (VISIBLEON(next, selmon)) {
			c = next;
			break;
		}
	}
	/* XXX consider: should this ever? always? raise the client? */
	keyboardfocus(c, NULL);
}

void
raiseclient(Client *c)
{
	if (!c)
		return;
	wl_list_remove(&c->slink);
	wl_list_insert(&stack, &c->slink);
}

void
render(struct wlr_surface *surface, int sx, int sy, void *data)
{
	/* This function is called for every surface that needs to be rendered. */
	struct render_data *rdata = data;
	struct wlr_output *output = rdata->output;

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
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(
			output_layout, output, &ox, &oy);
	ox += rdata->x + sx, oy += rdata->y + sy;

	/* We also have to apply the scale factor for HiDPI outputs. This is only
	 * part of the puzzle, dwl does not fully support HiDPI. */
	struct wlr_box obox = {
		.x = ox,
		.y = oy,
		.width = surface->current.width,
		.height = surface->current.height,
	};
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
	float matrix[9];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current.transform);
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
	/* Each subsequent window we render is rendered on top of the last. Because
	 * our stacking list is ordered front-to-back, we iterate over it backwards. */
	Client *c;
	wl_list_for_each_reverse(c, &stack, slink) {
		/* Only render visible clients which show on this monitor */
		struct wlr_box cbox = {
			.x = c->x, .y = c->y, .width = c->w, .height = c->h,
		};
		if (!VISIBLEON(c, c->mon) || !wlr_output_layout_intersects(
					output_layout, m->wlr_output, &cbox))
			continue;

		double ox = c->x, oy = c->y;
		wlr_output_layout_output_coords(output_layout, m->wlr_output,
				&ox, &oy);
		int w = c->xdg_surface->surface->current.width;
		int h = c->xdg_surface->surface->current.height;
		struct wlr_box borders[] = {
			{ox, oy, w + 2 * c->bw, c->bw},             /* top */
			{ox, oy + c->bw, c->bw, h},                 /* left */
			{ox + c->bw + w, oy + c->bw, c->bw, h},     /* right */
			{ox, oy + c->bw + h, w + 2 * c->bw, c->bw}, /* bottom */
		};
		int i;
		for (i = 0; i < sizeof(borders) / sizeof(borders[0]); i++) {
			scalebox(&borders[i], m->wlr_output->scale);
			wlr_render_rect(drw, &borders[i], bordercolor,
					m->wlr_output->transform_matrix);
		}

		struct render_data rdata = {
			.output = m->wlr_output,
			.when = now,
			.x = c->x + c->bw,
			.y = c->y + c->bw,
		};
		/* This calls our render function for each surface among the
		 * xdg_surface's toplevel and popups. */
		wlr_xdg_surface_for_each_surface(c->xdg_surface, render, &rdata);
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
	/* Get effective monitor geometry to use for window area */
	m->m = *wlr_output_layout_get_box(output_layout, m->wlr_output);
	m->w = m->m;

	arrange(m);

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
	struct wlr_box *bbox = interact ? &sgeom : &c->mon->w;
	c->x = x;
	c->y = y;
	c->w = w;
	c->h = h;
	applybounds(c, bbox);
	/* wlroots makes this a no-op if size hasn't changed */
	wlr_xdg_toplevel_set_size(c->xdg_surface,
			c->w - 2 * c->bw, c->h - 2 * c->bw);
}

void
resizemouse(const Arg *arg)
{
	struct wlr_surface *surface;
	grabc = xytoclient(cursor->x, cursor->y, &surface, &grabsx, &grabsy);
	if (!grabc)
		return;

	/* Doesn't work for X11 output - the next absolute motion event
	 * returns the cursor to where it started */
	wlr_cursor_warp_closest(cursor, NULL,
			grabc->x + grabc->w, grabc->y + grabc->h);

	/* Float the window and tell motionnotify to resize it */
	if (!grabc->isfloating && selmon->lt[selmon->sellt]->arrange)
		grabc->isfloating = 1;
	cursor_mode = CurResize;
	wlr_xcursor_manager_set_cursor_image(cursor_mgr,
			"bottom_right_corner", cursor);
}

void
run(char *startup_cmd)
{
	pid_t startup_pid = -1;

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket) {
		wlr_backend_destroy(backend);
		exit(EXIT_FAILURE);
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend)) {
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
	setenv("WAYLAND_DISPLAY", socket, true);
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
	/* If we're "grabbing" the cursor, don't use the client's image */
	/* XXX still need to save the provided surface to restore later */
	if (cursor_mode != CurNormal)
		return;
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
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
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	/* XXX change layout symbol? */
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
}

void
setmon(Client *c, Monitor *m)
{
	if (c->mon == m)
		return;
	int hadfocus = (c == selclient());
	/* XXX leave/enter should be in resize and check all outputs */
	if (c->mon)
		wlr_surface_send_leave(c->xdg_surface->surface, c->mon->wlr_output);
	c->mon = m;
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		applybounds(c, &m->m);
		wlr_surface_send_enter(c->xdg_surface->surface, m->wlr_output);
		c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	}
	/* Focus can change if c is the top of selmon before or after */
	if (hadfocus || c == selclient())
		refocus();
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
	 * to dig your fingers in and play with their behavior if you want. */
	wlr_compositor_create(dpy, drw);
	wlr_data_device_manager_create(dpy);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create();

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	new_output.notify = createmon;
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
	new_xdg_surface.notify = createnotify;
	wl_signal_add(&xdg_shell->events.new_surface,
			&new_xdg_surface);

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
	cursor_motion.notify = motionrelative;
	wl_signal_add(&cursor->events.motion, &cursor_motion);
	cursor_motion_absolute.notify = motionabsolute;
	wl_signal_add(&cursor->events.motion_absolute,
			&cursor_motion_absolute);
	cursor_button.notify = buttonpress;
	wl_signal_add(&cursor->events.button, &cursor_button);
	cursor_axis.notify = axisnotify;
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	cursor_frame.notify = cursorframe;
	wl_signal_add(&cursor->events.frame, &cursor_frame);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_list_init(&keyboards);
	new_input.notify = inputdevice;
	wl_signal_add(&backend->events.new_input, &new_input);
	seat = wlr_seat_create(dpy, "seat0");
	request_cursor.notify = setcursor;
	wl_signal_add(&seat->events.request_set_cursor,
			&request_cursor);
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
		refocus();
	}
}

void
tagmon(const Arg *arg)
{
	Client *sel = selclient();
	if (!sel)
		return;
	setmon(sel, dirtomon(arg->i));
}

void
tile(Monitor *m)
{
	unsigned int i, n = 0, h, mw, my, ty;
	Client *c;

	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, m) && !c->isfloating)
			n++;
	}
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
			my += c->h;
		} else {
			h = (m->w.height - ty) / (n - i);
			resize(c, m->w.x + mw, m->w.y + ty, m->w.width - mw, h, 0);
			ty += c->h;
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
	sel->isfloating = !sel->isfloating /* || sel->isfixed */;
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
		refocus();
	}
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		refocus();
	}
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	setmon(c, NULL);
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
	refocus();
}

Client *
xytoclient(double x, double y,
		struct wlr_surface **surface, double *sx, double *sy)
{
	/* XXX what if (x,y) is within a window's border? */
	/* This iterates over all of our surfaces and attempts to find one under the
	 * cursor. This relies on stack being ordered from top-to-bottom. */
	Client *c;
	wl_list_for_each(c, &stack, slink) {
		/* Skip clients that aren't visible */
		if (!VISIBLEON(c, c->mon))
			continue;
		/*
		 * XDG toplevels may have nested surfaces, such as popup windows
		 * for context menus or tooltips. This function tests if any of
		 * those are underneath the coordinates x and y (in layout
		 * coordinates). If so, it sets the surface pointer to that
		 * wlr_surface and the sx and sy coordinates to the coordinates
		 * relative to that surface's top-left corner.
		 */
		double _sx, _sy;
		struct wlr_surface *_surface = NULL;
		_surface = wlr_xdg_surface_surface_at(c->xdg_surface,
				x - c->x - c->bw, y - c->y - c->bw, &_sx, &_sy);

		if (_surface) {
			*sx = _sx;
			*sy = _sy;
			*surface = _surface;
			return c;
		}
	}
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
	wlr_log_init(WLR_INFO, NULL);
	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	dpy = wl_display_create();

	setup();
	run(startup_cmd);

	/* Once wl_display_run returns, we shut down the server. */
	wl_display_destroy_clients(dpy);
	wl_display_destroy(dpy);
	return EXIT_SUCCESS;
}
