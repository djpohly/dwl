/*
 * See LICENSE file for copyright and license details.
 */
#define _POSIX_C_SOURCE 200809L
#include <getopt.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_input_inhibitor.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#ifdef XWAYLAND
#include <X11/Xlib.h>
#include <wlr/xwayland.h>
#endif

#include "util.h"

/* macros */
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define END(A)                  ((A) + LENGTH(A))
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))

/* enums */
enum { CurNormal, CurMove, CurResize }; /* cursor */
enum { XDGShell, LayerShell, X11Managed, X11Unmanaged }; /* client types */
enum { LyrBg, LyrBottom, LyrTop, LyrOverlay, LyrTile, LyrFloat, LyrNoFocus, NUM_LAYERS }; /* scene layers */
#ifdef XWAYLAND
enum { NetWMWindowTypeDialog, NetWMWindowTypeSplash, NetWMWindowTypeToolbar,
	NetWMWindowTypeUtility, NetLast }; /* EWMH atoms */
#endif

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
	/* Must be first */
	unsigned int type; /* XDGShell or X11* */
	struct wlr_scene_node *scene;
	struct wlr_scene_rect *border[4]; /* top, bottom, left, right */
	struct wlr_scene_node *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wlr_box geom, prev;  /* layout-relative, includes border */
	Monitor *mon;
#ifdef XWAYLAND
	struct wl_listener activate;
	struct wl_listener configure;
#endif
	int bw;
	unsigned int tags;
	int isfloating, isurgent;
	uint32_t resize; /* configure serial of a pending resize */
	int isfullscreen;
} Client;

typedef struct {
	uint32_t singular_anchor;
	uint32_t anchor_triplet;
	int *positive_axis;
	int *negative_axis;
	int margin;
} Edge;

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
	struct wl_listener destroy;
} Keyboard;

typedef struct {
	/* Must be first */
	unsigned int type; /* LayerShell */
	int mapped;
	struct wlr_scene_node *scene;
	struct wl_list link;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener surface_commit;

	struct wlr_box geo;
} LayerSurface;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wlr_box m;      /* monitor area, layout-relative */
	struct wlr_box w;      /* window area, layout-relative */
	struct wl_list layers[4]; /* LayerSurface::link */
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	double mfact;
	int nmaster;
	int un_map; /* If a map/unmap happened on this monitor, then this should be true */
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

/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static void applyexclusive(struct wlr_box *usable_area, uint32_t anchor,
		int32_t exclusive, int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void cleanup(void);
static void cleanupkeyboard(struct wl_listener *listener, void *data);
static void cleanupmon(struct wl_listener *listener, void *data);
static void closemon(Monitor *m);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void createidleinhibitor(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_input_device *device);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_input_device *device);
static void cursorframe(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static Monitor *dirtomon(enum wlr_direction dir);
static void dragicondestroy(struct wl_listener *listener, void *data);
static void focusclient(Client *c, int lift);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void incnmaster(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static int keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static void killclient(const Arg *arg);
static void maplayersurfacenotify(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void monocle(Monitor *m);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void printstatus(void);
static void quit(const Arg *arg);
static void quitsignal(int signo);
static void rendermon(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void run(char *startup_cmd);
static Client *selclient(void);
static void setcursor(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m, unsigned int newtags);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void startdrag(struct wl_listener *listener, void *data);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unmaplayersurface(LayerSurface *layersurface);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static void virtualkeyboard(struct wl_listener *listener, void *data);
static Monitor *xytomon(double x, double y);
static struct wlr_scene_node *xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static struct wl_display *dpy;
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_node *layers[NUM_LAYERS];
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
static struct wlr_idle *idle;
static struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_input_inhibit_manager *input_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;

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
static struct wl_listener idle_inhibitor_create = {.notify = createidleinhibitor};
static struct wl_listener idle_inhibitor_destroy = {.notify = destroyidleinhibitor};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_input = {.notify = inputdevice};
static struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_surface = {.notify = createnotify};
static struct wl_listener new_layer_shell_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};
static struct wl_listener drag_icon_destroy = {.notify = dragicondestroy};

#ifdef XWAYLAND
static void activatex11(struct wl_listener *listener, void *data);
static void configurex11(struct wl_listener *listener, void *data);
static void createnotifyx11(struct wl_listener *listener, void *data);
static Atom getatom(xcb_connection_t *xc, const char *name);
static void xwaylandready(struct wl_listener *listener, void *data);
static struct wl_listener new_xwayland_surface = {.notify = createnotifyx11};
static struct wl_listener xwayland_ready = {.notify = xwaylandready};
static struct wlr_xwayland *xwayland;
static Atom netatom[NetLast];
#endif

/* configuration, allows nested code to access above variables */
#include "config.h"

/* attempt to encapsulate suck into one file */
#include "client.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

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
applyexclusive(struct wlr_box *usable_area,
		uint32_t anchor, int32_t exclusive,
		int32_t margin_top, int32_t margin_right,
		int32_t margin_bottom, int32_t margin_left) {
	Edge edges[] = {
		{ /* Top */
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP,
			.positive_axis = &usable_area->y,
			.negative_axis = &usable_area->height,
			.margin = margin_top,
		},
		{ /* Bottom */
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->height,
			.margin = margin_bottom,
		},
		{ /* Left */
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT,
			.anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = &usable_area->x,
			.negative_axis = &usable_area->width,
			.margin = margin_left,
		},
		{ /* Right */
			.singular_anchor = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT,
			.anchor_triplet = ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM,
			.positive_axis = NULL,
			.negative_axis = &usable_area->width,
			.margin = margin_right,
		}
	};
	for (size_t i = 0; i < LENGTH(edges); i++) {
		if ((anchor == edges[i].singular_anchor || anchor == edges[i].anchor_triplet)
				&& exclusive + edges[i].margin > 0) {
			if (edges[i].positive_axis)
				*edges[i].positive_axis += exclusive + edges[i].margin;
			if (edges[i].negative_axis)
				*edges[i].negative_axis -= exclusive + edges[i].margin;
			break;
		}
	}
}

void
applyrules(Client *c)
{
	/* rule matching */
	const char *appid, *title;
	unsigned int i, newtags = 0;
	const Rule *r;
	Monitor *mon = selmon, *m;

	c->isfloating = client_is_float_type(c);
	if (!(appid = client_get_appid(c)))
		appid = broken;
	if (!(title = client_get_title(c)))
		title = broken;

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			newtags |= r->tags;
			i = 0;
			wl_list_for_each(m, &mons, link)
				if (r->monitor == i++)
					mon = m;
		}
	}
	wlr_scene_node_reparent(c->scene, layers[c->isfloating ? LyrFloat : LyrTile]);
	setmon(c, mon, newtags);
}

void
arrange(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &clients, link)
		wlr_scene_node_set_enabled(c->scene, VISIBLEON(c, c->mon));

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	/* TODO recheck pointer focus here... or in resize()? */
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *layersurface;
	struct wlr_box full_area = m->m;

	wl_list_for_each(layersurface, list, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface = layersurface->layer_surface;
		struct wlr_layer_surface_v1_state *state = &wlr_layer_surface->current;
		struct wlr_box bounds;
		struct wlr_box box = {
			.width = state->desired_width,
			.height = state->desired_height
		};
		const uint32_t both_horiz = ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
		const uint32_t both_vert = ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP
			| ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;

		if (exclusive != (state->exclusive_zone > 0))
			continue;

		bounds = state->exclusive_zone == -1 ? full_area : *usable_area;

		/* Horizontal axis */
		if ((state->anchor & both_horiz) && box.width == 0) {
			box.x = bounds.x;
			box.width = bounds.width;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x = bounds.x;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x = bounds.x + (bounds.width - box.width);
		} else {
			box.x = bounds.x + ((bounds.width / 2) - (box.width / 2));
		}
		/* Vertical axis */
		if ((state->anchor & both_vert) && box.height == 0) {
			box.y = bounds.y;
			box.height = bounds.height;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y = bounds.y;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y = bounds.y + (bounds.height - box.height);
		} else {
			box.y = bounds.y + ((bounds.height / 2) - (box.height / 2));
		}
		/* Margin */
		if ((state->anchor & both_horiz) == both_horiz) {
			box.x += state->margin.left;
			box.width -= state->margin.left + state->margin.right;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT)) {
			box.x += state->margin.left;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT)) {
			box.x -= state->margin.right;
		}
		if ((state->anchor & both_vert) == both_vert) {
			box.y += state->margin.top;
			box.height -= state->margin.top + state->margin.bottom;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP)) {
			box.y += state->margin.top;
		} else if ((state->anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM)) {
			box.y -= state->margin.bottom;
		}
		if (box.width < 0 || box.height < 0) {
			wlr_layer_surface_v1_destroy(wlr_layer_surface);
			continue;
		}
		layersurface->geo = box;

		if (state->exclusive_zone > 0)
			applyexclusive(usable_area, state->anchor, state->exclusive_zone,
					state->margin.top, state->margin.right,
					state->margin.bottom, state->margin.left);
		wlr_scene_node_set_position(layersurface->scene, box.x, box.y);
		wlr_layer_surface_v1_configure(wlr_layer_surface, box.width, box.height);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	LayerSurface *layersurface;
	struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);

	/* Arrange exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (memcmp(&usable_area, &m->w, sizeof(struct wlr_box))) {
		m->w = usable_area;
		arrange(m);
	}

	/* Arrange non-exlusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	/* Find topmost keyboard interactive layer, if such a layer exists */
	for (size_t i = 0; i < LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(layersurface,
				&m->layers[layers_above_shell[i]], link) {
			if (layersurface->layer_surface->current.keyboard_interactive &&
					layersurface->layer_surface->mapped) {
				/* Deactivate the focused client. */
				focusclient(NULL, 0);
				wlr_seat_keyboard_notify_enter(seat, layersurface->layer_surface->surface,
						kb->keycodes, kb->num_keycodes, &kb->modifiers);
				return;
			}
		}
	}
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_event_pointer_axis *event = data;
	wlr_idle_notify_activity(idle, seat);
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	struct wlr_event_pointer_button *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	Client *c;
	const Button *b;

	wlr_idle_notify_activity(idle, seat);

	switch (event->state) {
	case WLR_BUTTON_PRESSED:
		/* Change focus if the button was _pressed_ over a client */
		xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
		/* Don't focus unmanaged clients */
		if (c && !client_is_unmanaged(c))
			focusclient(c, 1);

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
		/* TODO should reset to the pointer focus's current setcursor */
		if (cursor_mode != CurNormal) {
			wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);
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
	wlr_session_change_vt(wlr_backend_get_session(backend), arg->ui);
}

void
cleanup(void)
{
#ifdef XWAYLAND
	wlr_xwayland_destroy(xwayland);
#endif
	wl_display_destroy_clients(dpy);

	wlr_backend_destroy(backend);
	wlr_xcursor_manager_destroy(cursor_mgr);
	wlr_cursor_destroy(cursor);
	wlr_output_layout_destroy(output_layout);
	wlr_seat_destroy(seat);
	wl_display_destroy(dpy);
}

void
cleanupkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_input_device *device = data;
	Keyboard *kb = device->data;

	wl_list_remove(&kb->link);
	wl_list_remove(&kb->modifiers.link);
	wl_list_remove(&kb->key.link);
	wl_list_remove(&kb->destroy.link);
	free(kb);
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
	struct wlr_output *wlr_output = data;
	Monitor *m = wlr_output->data;
	int nmons, i = 0;

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	if ((nmons = wl_list_length(&mons)))
		do /* don't switch to disabled mons */
			selmon = wl_container_of(mons.prev, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

	focusclient(focustop(selmon), 1);
	closemon(m);
	free(m);
}

void
closemon(Monitor *m)
{
	/* move closed monitor's clients to the focused one */
	Client *c;

	wl_list_for_each(c, &clients, link) {
		if (c->isfloating && c->geom.x > m->m.width)
			resize(c, c->geom.x - m->w.width, c->geom.y,
				c->geom.width, c->geom.height, 0);
		if (c->mon == m)
			setmon(c, selmon, c->tags);
	}
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, surface_commit);
	struct wlr_layer_surface_v1 *wlr_layer_surface = layersurface->layer_surface;
	struct wlr_output *wlr_output = wlr_layer_surface->output;
	Monitor *m;

	wlr_scene_node_reparent(layersurface->scene,
			layers[wlr_layer_surface->current.layer]);

	if (!wlr_output || !(m = wlr_output->data))
		return;

	if (wlr_layer_surface->current.committed == 0
			&& layersurface->mapped == wlr_layer_surface->mapped)
		return;

	layersurface->mapped = wlr_layer_surface->mapped;

	if (layers[wlr_layer_surface->current.layer] != layersurface->scene) {
		wl_list_remove(&layersurface->link);
		wl_list_insert(&m->layers[wlr_layer_surface->current.layer],
			&layersurface->link);
	}
	arrangelayers(m);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;
}

void
createidleinhibitor(struct wl_listener *listener, void *data)
{
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
	wl_signal_add(&idle_inhibitor->events.destroy, &idle_inhibitor_destroy);

	wlr_idle_set_enabled(idle, seat, 0);
}

void
createkeyboard(struct wlr_input_device *device)
{
	struct xkb_context *context;
	struct xkb_keymap *keymap;
	Keyboard *kb = device->data = ecalloc(1, sizeof(*kb));
	kb->device = device;

	/* Prepare an XKB keymap and assign it to the keyboard. */
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	keymap = xkb_keymap_new_from_names(context, &xkb_rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(device->keyboard, repeat_rate, repeat_delay);

	/* Here we set up listeners for keyboard events. */
	LISTEN(&device->keyboard->events.modifiers, &kb->modifiers, keypressmod);
	LISTEN(&device->keyboard->events.key, &kb->key, keypress);
	LISTEN(&device->events.destroy, &kb->destroy, cleanupkeyboard);

	wlr_seat_set_keyboard(seat, device);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&keyboards, &kb->link);
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *wlr_layer_surface = data;
	LayerSurface *layersurface;
	Monitor *m;
	struct wlr_layer_surface_v1_state old_state;

	if (!wlr_layer_surface->output) {
		wlr_layer_surface->output = selmon->wlr_output;
	}

	layersurface = ecalloc(1, sizeof(LayerSurface));
	layersurface->type = LayerShell;
	LISTEN(&wlr_layer_surface->surface->events.commit,
		&layersurface->surface_commit, commitlayersurfacenotify);
	LISTEN(&wlr_layer_surface->events.destroy, &layersurface->destroy,
			destroylayersurfacenotify);
	LISTEN(&wlr_layer_surface->events.map, &layersurface->map,
			maplayersurfacenotify);
	LISTEN(&wlr_layer_surface->events.unmap, &layersurface->unmap,
			unmaplayersurfacenotify);

	layersurface->layer_surface = wlr_layer_surface;
	wlr_layer_surface->data = layersurface;
	m = wlr_layer_surface->output->data;

	layersurface->scene = wlr_layer_surface->surface->data =
			wlr_scene_subsurface_tree_create(layers[wlr_layer_surface->pending.layer],
			wlr_layer_surface->surface);
	layersurface->scene->data = layersurface;

	wl_list_insert(&m->layers[wlr_layer_surface->pending.layer],
			&layersurface->link);

	/* Temporarily set the layer's current state to pending
	 * so that we can easily arrange it
	 */
	old_state = wlr_layer_surface->current;
	wlr_layer_surface->current = wlr_layer_surface->pending;
	arrangelayers(m);
	wlr_layer_surface->current = old_state;
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	const MonitorRule *r;
	Monitor *m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;

	wlr_output_init_render(wlr_output, alloc, drw);

	/* Initialize monitor state using configured rules */
	for (size_t i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);
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

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_set_mode(wlr_output, wlr_output_preferred_mode(wlr_output));
	wlr_output_enable_adaptive_sync(wlr_output, 1);

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);

	wlr_output_enable(wlr_output, 1);
	if (!wlr_output_commit(wlr_output))
		return;

	wl_list_insert(&mons, &m->link);
	printstatus();

	/* Adds this to the output layout in the order it was configured in.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	wlr_output_layout_add_auto(output_layout, wlr_output);

	/* If length == 1 we need update selmon.
	 * Maybe it will change in run(). */
	if (wl_list_length(&mons) == 1) {
		Client *c;
		selmon = m;
		/* If there is any client, set c->mon to this monitor */
		wl_list_for_each(c, &clients, link)
			setmon(c, m, c->tags);
	}
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup,
	 * or when wlr_layer_shell receives a new popup from a layer.
	 * If you want to do something tricky with popups you should check if
	 * its parent is wlr_xdg_shell or wlr_layer_shell */
	struct wlr_xdg_surface *xdg_surface = data;
	Client *c;

	if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_POPUP) {
		struct wlr_box box;
		xdg_surface->surface->data = wlr_scene_xdg_surface_create(
				xdg_surface->popup->parent->data, xdg_surface);
		if (!(c = client_from_popup(xdg_surface->popup)) || !c->mon)
			return;
		box = c->mon->m;
		box.x -= c->geom.x;
		box.y -= c->geom.y;
		wlr_xdg_popup_unconstrain_from_box(xdg_surface->popup, &box);
		return;
	} else if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_NONE)
		return;

	/* Allocate a Client for this surface */
	c = xdg_surface->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = xdg_surface;
	c->bw = borderpx;

	LISTEN(&xdg_surface->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&xdg_surface->events.map, &c->map, mapnotify);
	LISTEN(&xdg_surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&xdg_surface->events.destroy, &c->destroy, destroynotify);
	LISTEN(&xdg_surface->toplevel->events.set_title, &c->set_title, updatetitle);
	LISTEN(&xdg_surface->toplevel->events.request_fullscreen, &c->fullscreen,
			fullscreennotify);
	c->isfullscreen = 0;
}

void
createpointer(struct wlr_input_device *device)
{
	if (wlr_input_device_is_libinput(device)) {
		struct libinput_device *libinput_device =  (struct libinput_device*)
			wlr_libinput_get_device_handle(device);

		if (tap_to_click && libinput_device_config_tap_get_finger_count(libinput_device))
			libinput_device_config_tap_set_enabled(libinput_device, LIBINPUT_CONFIG_TAP_ENABLED);

		if (libinput_device_config_scroll_has_natural_scroll(libinput_device))
			libinput_device_config_scroll_set_natural_scroll_enabled(libinput_device, natural_scrolling);
	}

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
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	/* I've been testing and at this point the inhibitor has not yet been
	 * removed from the list, checking if it has at least one item. */
	wlr_idle_set_enabled(idle, seat, wl_list_length(&idle_inhibit_mgr->inhibitors) <= 1);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, destroy);

	if (layersurface->layer_surface->mapped)
		unmaplayersurface(layersurface);
	wl_list_remove(&layersurface->link);
	wl_list_remove(&layersurface->destroy.link);
	wl_list_remove(&layersurface->map.link);
	wl_list_remove(&layersurface->unmap.link);
	wl_list_remove(&layersurface->surface_commit.link);
	if (layersurface->layer_surface->output) {
		Monitor *m = layersurface->layer_surface->output->data;
		if (m)
			arrangelayers(m);
		layersurface->layer_surface->output = NULL;
	}
	free(layersurface);
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is destroyed and should never be shown again. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->map.link);
	wl_list_remove(&c->unmap.link);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		wl_list_remove(&c->configure.link);
		wl_list_remove(&c->activate.link);
	} else
#endif
		wl_list_remove(&c->commit.link);
	free(c);
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	struct wlr_output *next;
	if ((next = wlr_output_layout_adjacent_output(output_layout,
			dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout,
			dir ^ (WLR_DIRECTION_LEFT|WLR_DIRECTION_RIGHT),
			selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	return selmon;
}

void
dragicondestroy(struct wl_listener *listener, void *data)
{
	struct wlr_drag_icon *icon = data;
	wlr_scene_node_destroy(icon->data);
	// Focus enter isn't sent during drag, so refocus the focused node.
	focusclient(selclient(), 1);
	motionnotify(0);
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	struct wlr_keyboard *kb;
	int i;

	/* Raise client in stacking order if requested */
	if (c && lift)
		wlr_scene_node_raise_to_top(c->scene);

	if (c && client_surface(c) == old)
		return;

	/* Put the new client atop the focus stack and select its monitor */
	if (c) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;

		for (i = 0; i < 4; i++)
			wlr_scene_rect_set_color(c->border[i], focuscolor);
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with focuscolor
		 * and focus it after the overlay is closed.
		 * It's probably pointless to check if old is a layer surface
		 * since it can't be anything else at this point. */
		if (wlr_surface_is_layer_surface(old)) {
			struct wlr_layer_surface_v1 *wlr_layer_surface =
				wlr_layer_surface_v1_from_wlr_surface(old);

			if (wlr_layer_surface->mapped && (
						wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_TOP ||
						wlr_layer_surface->current.layer == ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
						))
				return;
		} else {
			Client *w;
			struct wlr_scene_node *node = old->data;
			if ((w = node->data))
				for (i = 0; i < 4; i++)
					wlr_scene_rect_set_color(w->border[i], bordercolor);

			client_activate_surface(old, 0);
		}
	}

	printstatus();
	wlr_idle_set_enabled(idle, seat, wl_list_empty(&idle_inhibit_mgr->inhibitors));

	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Have a client, so focus its top-level wlr_surface */
	kb = wlr_seat_get_keyboard(seat);
	wlr_seat_keyboard_notify_enter(seat, client_surface(c),
			kb->keycodes, kb->num_keycodes, &kb->modifiers);

	/* Activate the new client */
	client_activate_surface(client_surface(c), 1);
}

void
focusmon(const Arg *arg)
{
	do
		selmon = dirtomon(arg->i);
	while (!selmon->wlr_output->enabled);
	focusclient(focustop(selmon), 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = selclient();
	if (!sel || (sel->isfullscreen && lockfullscreen))
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
	focusclient(c, 1);
}

Client *
focustop(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink)
		if (VISIBLEON(c, m))
			return c;
	return NULL;
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	int fullscreen = client_wants_fullscreen(c);

	if (!c->mon) {
		/* if the client is not mapped yet, let mapnotify() call setfullscreen() */
		c->isfullscreen = fullscreen;
		return;
	}
	setfullscreen(c, fullscreen);
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
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
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
	int i;
	/* This event is raised when a key is pressed or released. */
	Keyboard *kb = wl_container_of(listener, kb, key);
	struct wlr_event_keyboard_key *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			kb->device->keyboard->xkb_state, keycode, &syms);

	int handled = 0;
	uint32_t mods = wlr_keyboard_get_modifiers(kb->device->keyboard);

	wlr_idle_notify_activity(idle, seat);

	/* On _press_ if there is no active screen locker,
	 * attempt to process a compositor keybinding. */
	if (!input_inhibit_mgr->active_inhibitor
			&& event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
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

void
killclient(const Arg *arg)
{
	Client *sel = selclient();
	if (sel)
		client_send_close(sel);
}

void
maplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, map);
	wlr_surface_send_enter(layersurface->layer_surface->surface,
		layersurface->layer_surface->output);
	motionnotify(0);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *c = wl_container_of(listener, c, map);
	int i;

	/* Create scene tree for this client and its border */
	c->scene = &wlr_scene_tree_create(layers[LyrTile])->node;
	c->scene_surface = client_surface(c)->data = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene_surface->data = c;

	if (client_is_unmanaged(c)) {
		client_get_geometry(c, &c->geom);
		/* Floating */
		wlr_scene_node_reparent(c->scene, layers[LyrFloat]);
		wlr_scene_node_set_position(c->scene, c->geom.x + borderpx,
			c->geom.y + borderpx);
		return;
	}

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0, bordercolor);
		c->border[i]->node.data = c;
		wlr_scene_rect_set_color(c->border[i], bordercolor);
		wlr_scene_node_lower_to_bottom(&c->border[i]->node);
	}

	/* Initialize client geometry with room for border */
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	client_get_geometry(c, &c->geom);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	/* Insert this client into client lists. */
	wl_list_insert(&clients, &c->link);
	wl_list_insert(&fstack, &c->flink);

	/* Set initial monitor, tags, floating status, and focus */
	applyrules(c);
	printstatus();

	if (c->isfullscreen)
		setfullscreen(c, 1);

	c->mon->un_map = 1;
}

void
monocle(Monitor *m)
{
	Client *c;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		resize(c, m->w.x, m->w.y, m->w.width, m->w.height, 0);
	}
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
	Client *c = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_drag_icon *icon;

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_idle_notify_activity(idle, seat);

		/* Update selmon (even while dragging a window) */
		if (sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
	}

	if (seat->drag && (icon = seat->drag->icon))
		wlr_scene_node_set_position(icon->data, cursor->x + icon->surface->sx,
				cursor->y + icon->surface->sy);
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

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, NULL, &sx, &sy);

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && time)
		wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);

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
	wlr_cursor_move(cursor, event->device, event->delta_x, event->delta_y);
	motionnotify(event->time_msec);
}

void
moveresize(const Arg *arg)
{
	if (cursor_mode != CurNormal)
		return;
	xytonode(cursor->x, cursor->y, NULL, &grabc, NULL, NULL, NULL);
	if (!grabc || client_is_unmanaged(grabc))
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
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	/*
	 * Called when a client such as wlr-randr requests a change in output
	 * configuration.  This is only one way that the layout can be changed,
	 * so any Monitor information should be updated by updatemons() after an
	 * output_layout.change event, not here.
	 */
	struct wlr_output_configuration_head_v1 *config_head;
	int ok = 1;

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;

		wlr_output_enable(wlr_output, config_head->state.enabled);
		if (config_head->state.enabled) {
			if (config_head->state.mode)
				wlr_output_set_mode(wlr_output, config_head->state.mode);
			else
				wlr_output_set_custom_mode(wlr_output,
						config_head->state.custom_mode.width,
						config_head->state.custom_mode.height,
						config_head->state.custom_mode.refresh);

			wlr_output_layout_move(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);
			wlr_output_set_transform(wlr_output, config_head->state.transform);
			wlr_output_set_scale(wlr_output, config_head->state.scale);
		}

		if (!(ok = wlr_output_test(wlr_output)))
			break;
	}
	wl_list_for_each(config_head, &config->heads, link) {
		if (ok && !test)
			wlr_output_commit(config_head->state.output);
		else
			wlr_output_rollback(config_head->state.output);
	}
	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

void
pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;
	int internal_call = !time;

	if (sloppyfocus && !internal_call && c && !client_is_unmanaged(c))
		focusclient(c, 0);

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (internal_call) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);

}

void
printstatus(void)
{
	Monitor *m = NULL;
	Client *c;
	unsigned int occ, urg, sel;

	wl_list_for_each(m, &mons, link) {
		occ = urg = 0;
		wl_list_for_each(c, &clients, link) {
			if (c->mon != m)
				continue;
			occ |= c->tags;
			if (c->isurgent)
				urg |= c->tags;
		}
		if ((c = focustop(m))) {
			printf("%s title %s\n", m->wlr_output->name, client_get_title(c));
			printf("%s fullscreen %u\n", m->wlr_output->name, c->isfullscreen);
			printf("%s floating %u\n", m->wlr_output->name, c->isfloating);
			sel = c->tags;
		} else {
			printf("%s title \n", m->wlr_output->name);
			printf("%s fullscreen \n", m->wlr_output->name);
			printf("%s floating \n", m->wlr_output->name);
			sel = 0;
		}

		printf("%s selmon %u\n", m->wlr_output->name, m == selmon);
		printf("%s tags %u %u %u %u\n", m->wlr_output->name, occ, m->tagset[m->seltags],
				sel, urg);
		printf("%s layout %s\n", m->wlr_output->name, m->lt[m->sellt]->symbol);
	}
	fflush(stdout);
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
quitsignal(int signo)
{
	quit(NULL);
}

void
rendermon(struct wl_listener *listener, void *data)
{
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);
	Client *c;
	int skip = 0;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	/* Render if no XDG clients have an outstanding resize and are visible on
	 * this monitor. */
	/* Checking m->un_map for every client is not optimal but works */
	wl_list_for_each(c, &clients, link) {
		if ((c->resize && m->un_map) || (c->type == XDGShell
				&& (c->surface.xdg->pending.geometry.width !=
				c->surface.xdg->current.geometry.width
				|| c->surface.xdg->pending.geometry.height !=
				c->surface.xdg->current.geometry.height))) {
			/* Lie */
			wlr_surface_send_frame_done(client_surface(c), &now);
			skip = 1;
		}
	}
	if (!skip && !wlr_scene_output_commit(m->scene_output))
		return;
	/* Let clients know a frame has been rendered */
	wlr_scene_output_send_frame_done(m->scene_output, &now);
	m->un_map = 0;
}

void
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
resize(Client *c, int x, int y, int w, int h, int interact)
{
	int min_width = 0, min_height = 0;
	struct wlr_box *bbox = interact ? &sgeom : &c->mon->w;
	client_min_size(c, &min_width, &min_height);
	c->geom.x = x;
	c->geom.y = y;
	c->geom.width = MAX(min_width + 2 * c->bw, w);
	c->geom.height = MAX(min_height + 2 * c->bw, h);
	applybounds(c, bbox);

	/* Update scene-graph, including borders */
	wlr_scene_node_set_position(c->scene, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(c->scene_surface, c->bw, c->bw);
	wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
	wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
	wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

	/* wlroots makes this a no-op if size hasn't changed */
	c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
			c->geom.height - 2 * c->bw);
}

void
run(char *startup_cmd)
{
	pid_t startup_pid = -1;

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	/* Now that the socket exists, run the startup command */
	if (startup_cmd) {
		int piperw[2];
		if (pipe(piperw) < 0)
			die("startup: pipe:");
		if ((startup_pid = fork()) < 0)
			die("startup: fork:");
		if (startup_pid == 0) {
			dup2(piperw[0], STDIN_FILENO);
			close(piperw[0]);
			close(piperw[1]);
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			die("startup: execl:");
		}
		dup2(piperw[1], STDOUT_FILENO);
		close(piperw[1]);
		close(piperw[0]);
	}
	/* If nobody is reading the status output, don't terminate */
	signal(SIGPIPE, SIG_IGN);
	printstatus();

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	/* Now that outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* TODO hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping.  still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_xcursor_manager_set_cursor_image(cursor_mgr, "left_ptr", cursor);

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wl_display_run(dpy);

	if (startup_cmd) {
		kill(startup_pid, SIGTERM);
		waitpid(startup_pid, NULL, 0);
	}
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
	/* TODO still need to save the provided surface to restore later */
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
	c->isfloating = floating;
	wlr_scene_node_reparent(c->scene, layers[c->isfloating ? LyrFloat : LyrTile]);
	arrange(c->mon);
	printstatus();
}

void
setfullscreen(Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	c->bw = fullscreen ? 0 : borderpx;
	client_set_fullscreen(c, fullscreen);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m.x, c->mon->m.y, c->mon->m.width, c->mon->m.height, 0);
	} else {
		/* restore previous size instead of arrange for floating windows since
		 * client positions are set by the user and cannot be recalculated */
		resize(c, c->prev.x, c->prev.y, c->prev.width, c->prev.height, 0);
	}
	arrange(c->mon);
	printstatus();
}

void
setlayout(const Arg *arg)
{
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	/* TODO change layout symbol? */
	arrange(selmon);
	printstatus();
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
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;

	/* TODO leave/enter is not optimal but works */
	if (oldmon) {
		wlr_surface_send_leave(client_surface(c), oldmon->wlr_output);
		arrange(oldmon);
	}
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		resize(c, c->geom.x, c->geom.y, c->geom.width, c->geom.height, 0);
		wlr_surface_send_enter(client_surface(c), m->wlr_output);
		c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
		arrange(m);
	}
	focusclient(focustop(selmon), 1);
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
	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	dpy = wl_display_create();

	/* Set up signal handlers */
	sigchld(0);
	signal(SIGINT, quitsignal);
	signal(SIGTERM, quitsignal);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. The NULL argument here optionally allows you
	 * to pass in a custom renderer if wlr_renderer doesn't meet your needs. The
	 * backend uses the renderer, for example, to fall back to software cursors
	 * if the backend does not support hardware cursors (some older GPUs
	 * don't). */
	if (!(backend = wlr_backend_autocreate(dpy)))
		die("couldn't create backend");

	/* Initialize the scene graph used to lay out windows */
	scene = wlr_scene_create();
	layers[LyrBg] = &wlr_scene_tree_create(&scene->node)->node;
	layers[LyrBottom] = &wlr_scene_tree_create(&scene->node)->node;
	layers[LyrTile] = &wlr_scene_tree_create(&scene->node)->node;
	layers[LyrFloat] = &wlr_scene_tree_create(&scene->node)->node;
	layers[LyrTop] = &wlr_scene_tree_create(&scene->node)->node;
	layers[LyrOverlay] = &wlr_scene_tree_create(&scene->node)->node;
	layers[LyrNoFocus] = &wlr_scene_tree_create(&scene->node)->node;

	/* Create a renderer with the default implementation */
	if (!(drw = wlr_renderer_autocreate(backend)))
		die("couldn't create renderer");
	wlr_renderer_init_wl_display(drw, dpy);

	/* Create a default allocator */
	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't create allocator");

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	compositor = wlr_compositor_create(dpy, drw);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_data_control_manager_v1_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_gamma_control_manager_v1_create(dpy);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);

	/* Initializes the interface used to implement urgency hints */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create();
	wl_signal_add(&output_layout->events.change, &layout_change);
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

	idle = wlr_idle_create(dpy);

	idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
	wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &idle_inhibitor_create);

	layer_shell = wlr_layer_shell_v1_create(dpy);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_shell_surface);

	xdg_shell = wlr_xdg_shell_create(dpy);
	wl_signal_add(&xdg_shell->events.new_surface, &new_xdg_surface);

	input_inhibit_mgr = wlr_input_inhibit_manager_create(dpy);

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	wlr_xdg_decoration_manager_v1_create(dpy);

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
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
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
	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
			&new_virtual_keyboard);
	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);

	wlr_scene_set_presentation(scene, wlr_presentation_create(dpy, backend));

#ifdef XWAYLAND
	/*
	 * Initialise the XWayland X server.
	 * It will be started when the first X client is started.
	 */
	xwayland = wlr_xwayland_create(dpy, compositor, 1);
	if (xwayland) {
		wl_signal_add(&xwayland->events.ready, &xwayland_ready);
		wl_signal_add(&xwayland->events.new_surface, &new_xwayland_surface);

		setenv("DISPLAY", xwayland->display_name, 1);
	} else {
		fprintf(stderr, "failed to setup XWayland X server, continuing without it\n");
	}
#endif
}

void
sigchld(int unused)
{
	/* We should be able to remove this function in favor of a simple
	 *     signal(SIGCHLD, SIG_IGN);
	 * but the Xwayland implementation in wlroots currently prevents us from
	 * setting our own disposition for SIGCHLD.
	 */
	if (signal(SIGCHLD, sigchld) == SIG_ERR)
		die("can't install SIGCHLD handler:");
	while (0 < waitpid(-1, NULL, WNOHANG))
		;
}

void
spawn(const Arg *arg)
{
	if (fork() == 0) {
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwl: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;

	if (!drag->icon)
		return;

	drag->icon->data = wlr_scene_subsurface_tree_create(layers[LyrNoFocus], drag->icon->surface);
	motionnotify(0);
	wl_signal_add(&drag->icon->events.destroy, &drag_icon_destroy);
}

void
tag(const Arg *arg)
{
	Client *sel = selclient();
	if (sel && arg->ui & TAGMASK) {
		sel->tags = arg->ui & TAGMASK;
		focusclient(focustop(selmon), 1);
		arrange(selmon);
	}
	printstatus();
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
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? m->w.width * m->mfact : 0;
	else
		mw = m->w.width;
	i = my = ty = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
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
	/* return if fullscreen */
	if (sel && !sel->isfullscreen)
		setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = selclient();
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
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
		focusclient(focustop(selmon), 1);
		arrange(selmon);
	}
	printstatus();
}

void
toggleview(const Arg *arg)
{
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focusclient(focustop(selmon), 1);
		arrange(selmon);
	}
	printstatus();
}

void
unmaplayersurface(LayerSurface *layersurface)
{
	layersurface->layer_surface->mapped = 0;
	if (layersurface->layer_surface->surface ==
			seat->keyboard_state.focused_surface)
		focusclient(selclient(), 1);
	motionnotify(0);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *layersurface = wl_container_of(listener, layersurface, unmap);
	unmaplayersurface(layersurface);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	if (c->mon)
		c->mon->un_map = 1;

	if (client_is_unmanaged(c)) {
		wlr_scene_node_destroy(c->scene);
		return;
	}

	wl_list_remove(&c->link);
	setmon(c, NULL, 0);
	wl_list_remove(&c->flink);
	wlr_scene_node_destroy(c->scene);
	printstatus();
}

void
updatemons(struct wl_listener *listener, void *data)
{
	/*
	 * Called whenever the output layout changes: adding or removing a
	 * monitor, changing an output's mode or position, etc.  This is where
	 * the change officially happens and we update geometry, window
	 * positions, focus, and the stored configuration in wlroots'
	 * output-manager implementation.
	 */
	struct wlr_output_configuration_v1 *config =
		wlr_output_configuration_v1_create();
	Monitor *m;
	sgeom = *wlr_output_layout_get_box(output_layout, NULL);
	wl_list_for_each(m, &mons, link) {
		struct wlr_output_configuration_head_v1 *config_head =
			wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* TODO: move clients off disabled monitors */
		/* TODO: move focus if selmon is disabled */

		/* Get the effective monitor geometry to use for surfaces */
		m->m = m->w = *wlr_output_layout_get_box(output_layout, m->wlr_output);
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);
		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);

		config_head->state.enabled = m->wlr_output->enabled;
		config_head->state.mode = m->wlr_output->current_mode;
		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;
	}

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	if (c == focustop(c->mon))
		printstatus();
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c;

	if (!wlr_surface_is_xdg_surface(event->surface))
		return;
	c = wlr_xdg_surface_from_wlr_surface(event->surface)->data;
	if (c != selclient()) {
		c->isurgent = 1;
		printstatus();
	}
}

void
view(const Arg *arg)
{
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	printstatus();
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *keyboard = data;
	struct wlr_input_device *device = &keyboard->input_device;
	createkeyboard(device);
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

struct wlr_scene_node *
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node, *pnode;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	LayerSurface *l = NULL;
	const int *layer;
	int focus_order[] = { LyrOverlay, LyrTop, LyrFloat, LyrTile, LyrBottom, LyrBg };

	for (layer = focus_order; layer < END(focus_order); layer++) {
		if ((node = wlr_scene_node_at(layers[*layer], x, y, nx, ny))) {
			if (node->type == WLR_SCENE_NODE_SURFACE)
				surface = wlr_scene_surface_from_node(node)->surface;
			/* Walk the tree to find a node that knows the client */
			for (pnode = node; pnode && !c; pnode = pnode->parent)
				c = pnode->data;
			if (c && c->type == LayerShell) {
				c = NULL;
				l = pnode->data;
			}
		}
		if (surface)
			break;
	}

	if (psurface) *psurface = surface;
	if (pc) *pc = c;
	if (pl) *pl = l;
	return node;
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = selclient();

	if (!sel || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}

#ifdef XWAYLAND
void
activatex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, activate);

	/* Only "managed" windows can be activated */
	if (c->type == X11Managed)
		wlr_xwayland_surface_activate(c->surface.xwayland, 1);
}

void
configurex11(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	wlr_xwayland_surface_configure(c->surface.xwayland,
			event->x, event->y, event->width, event->height);
}

void
createnotifyx11(struct wl_listener *listener, void *data)
{
	struct wlr_xwayland_surface *xwayland_surface = data;
	Client *c;
	wl_list_for_each(c, &clients, link)
		if (c->isfullscreen && VISIBLEON(c, c->mon))
			setfullscreen(c, 0);

	/* Allocate a Client for this surface */
	c = xwayland_surface->data = ecalloc(1, sizeof(*c));
	c->surface.xwayland = xwayland_surface;
	c->type = xwayland_surface->override_redirect ? X11Unmanaged : X11Managed;
	c->bw = borderpx;
	c->isfullscreen = 0;

	/* Listen to the various events it can emit */
	LISTEN(&xwayland_surface->events.map, &c->map, mapnotify);
	LISTEN(&xwayland_surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&xwayland_surface->events.request_activate, &c->activate, activatex11);
	LISTEN(&xwayland_surface->events.request_configure, &c->configure,
			configurex11);
	LISTEN(&xwayland_surface->events.set_title, &c->set_title, updatetitle);
	LISTEN(&xwayland_surface->events.destroy, &c->destroy, destroynotify);
	LISTEN(&xwayland_surface->events.request_fullscreen, &c->fullscreen,
			fullscreennotify);
}

Atom
getatom(xcb_connection_t *xc, const char *name)
{
	Atom atom = 0;
	xcb_intern_atom_reply_t *reply;
	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(xc, 0, strlen(name), name);
	if ((reply = xcb_intern_atom_reply(xc, cookie, NULL)))
		atom = reply->atom;
	free(reply);

	return atom;
}

void
xwaylandready(struct wl_listener *listener, void *data)
{
	struct wlr_xcursor *xcursor;
	xcb_connection_t *xc = xcb_connect(xwayland->display_name, NULL);
	int err = xcb_connection_has_error(xc);
	if (err) {
		fprintf(stderr, "xcb_connect to X server failed with code %d\n. Continuing with degraded functionality.\n", err);
		return;
	}

	/* Collect atoms we are interested in.  If getatom returns 0, we will
	 * not detect that window type. */
	netatom[NetWMWindowTypeDialog] = getatom(xc, "_NET_WM_WINDOW_TYPE_DIALOG");
	netatom[NetWMWindowTypeSplash] = getatom(xc, "_NET_WM_WINDOW_TYPE_SPLASH");
	netatom[NetWMWindowTypeToolbar] = getatom(xc, "_NET_WM_WINDOW_TYPE_TOOLBAR");
	netatom[NetWMWindowTypeUtility] = getatom(xc, "_NET_WM_WINDOW_TYPE_UTILITY");

	/* assign the one and only seat */
	wlr_xwayland_set_seat(xwayland, seat);

	/* Set the default XWayland cursor to match the rest of dwl. */
	if ((xcursor = wlr_xcursor_manager_get_xcursor(cursor_mgr, "left_ptr", 1)))
		wlr_xwayland_set_cursor(xwayland,
				xcursor->images[0]->buffer, xcursor->images[0]->width * 4,
				xcursor->images[0]->width, xcursor->images[0]->height,
				xcursor->images[0]->hotspot_x, xcursor->images[0]->hotspot_y);

	xcb_disconnect(xc);
}
#endif

int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:h")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-s startup command]", argv[0]);
}
