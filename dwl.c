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

#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define LENGTH(X)               (sizeof X / sizeof X[0])

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

typedef struct {
	struct wl_list link;
	struct wlr_xdg_surface *xdg_surface;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	bool mapped;
	int x, y;
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
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
} Monitor;

/* Used to move all of the data necessary to render a surface from the top-level
 * frame handler to the per-surface render function. */
struct render_data {
	struct wlr_output *output;
	struct timespec *when;
	int x, y;
};

/* function declarations */
static void axisnotify(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_input_device *device);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_input_device *device);
static void cursorframe(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static void focus(Client *c, struct wlr_surface *surface);
static void focusnext(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static bool keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static void maprequest(struct wl_listener *listener, void *data);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time);
static void motionrelative(struct wl_listener *listener, void *data);
static void movemouse(const Arg *arg);
static void moveresize(Client *c, unsigned int mode);
static void quit(const Arg *arg);
static void render(struct wlr_surface *surface, int sx, int sy, void *data);
static void rendermon(struct wl_listener *listener, void *data);
static void resizemouse(const Arg *arg);
static void setcursor(struct wl_listener *listener, void *data);
static void spawn(const Arg *arg);
static void unmapnotify(struct wl_listener *listener, void *data);
static Client * xytoclient(double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);
static bool xytosurface(Client *c, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);

/* variables */
static struct wl_display *wl_display;
static struct wlr_backend *backend;
static struct wlr_renderer *renderer;

static struct wlr_xdg_shell *xdg_shell;
static struct wl_listener new_xdg_surface;
static struct wl_list clients;

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
static Client *grabbed_client;
static double grab_x, grab_y;
static int grab_width, grab_height;

static struct wlr_output_layout *output_layout;
static struct wl_list mons;
static struct wl_listener new_output;

#include "config.h"

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
	/* This event is forwarded by the cursor when a pointer emits a button
	 * event. */
	struct wlr_event_pointer_button *event = data;
	/* Notify the client with pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
	double sx, sy;
	struct wlr_surface *surface;
	Client *c = xytoclient(cursor->x, cursor->y,
			&surface, &sx, &sy);
	if (event->state == WLR_BUTTON_RELEASED) {
		/* If you released any buttons, we exit interactive move/resize mode. */
		cursor_mode = CurNormal;
	} else {
		/* Focus that client if the button was _pressed_ */
		focus(c, surface);

		struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
		uint32_t mods = wlr_keyboard_get_modifiers(keyboard);
		for (int i = 0; i < LENGTH(buttons); i++) {
			if (event->button == buttons[i].button &&
					CLEANMASK(mods) == CLEANMASK(buttons[i].mod) &&
					buttons[i].func) {
				buttons[i].func(&buttons[i].arg);
			}
		}
	}
}

void
createkeyboard(struct wlr_input_device *device)
{
	Keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->device = device;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_map_new_from_names(context, &xkb_rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(device->keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keypressmod;
	wl_signal_add(&device->keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keypress;
	wl_signal_add(&device->keyboard->events.key, &keyboard->key);

	wlr_seat_set_keyboard(seat, device);

	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&keyboards, &keyboard->link);
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is rasied by the backend when a new output (aka a display or
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
		if (!wlr_output_commit(wlr_output)) {
			return;
		}
	}

	/* Allocates and configures our state for this output */
	Monitor *m = calloc(1, sizeof(*m));
	m->wlr_output = wlr_output;
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
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when wlr_xdg_shell receives a new xdg surface from a
	 * client, either a toplevel (application window) or popup. */
	struct wlr_xdg_surface *xdg_surface = data;
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	/* Allocate a Client for this surface */
	Client *c = calloc(1, sizeof(*c));
	c->xdg_surface = xdg_surface;

	/* Listen to the various events it can emit */
	c->map.notify = maprequest;
	wl_signal_add(&xdg_surface->events.map, &c->map);
	c->unmap.notify = unmapnotify;
	wl_signal_add(&xdg_surface->events.unmap, &c->unmap);
	c->destroy.notify = destroynotify;
	wl_signal_add(&xdg_surface->events.destroy, &c->destroy);

	/* Add it to the list of clients. */
	wl_list_insert(&clients, &c->link);
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
	wl_list_remove(&c->link);
	free(c);
}

void
focus(Client *c, struct wlr_surface *surface)
{
	/* Note: this function only deals with keyboard focus. */
	if (c == NULL) {
		return;
	}
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	if (prev_surface == surface) {
		/* Don't re-focus an already focused surface. */
		return;
	}
	if (prev_surface) {
		/*
		 * Deactivate the previously focused surface. This lets the client know
		 * it no longer has focus and the client will repaint accordingly, e.g.
		 * stop displaying a caret.
		 */
		struct wlr_xdg_surface *previous = wlr_xdg_surface_from_wlr_surface(
					seat->keyboard_state.focused_surface);
		wlr_xdg_toplevel_set_activated(previous, false);
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	/* Move the client to the front */
	wl_list_remove(&c->link);
	wl_list_insert(&clients, &c->link);
	/* Activate the new surface */
	wlr_xdg_toplevel_set_activated(c->xdg_surface, true);
	/*
	 * Tell the seat to have the keyboard enter this surface. wlroots will keep
	 * track of this and automatically send key events to the appropriate
	 * clients without additional work on your part.
	 */
	wlr_seat_keyboard_notify_enter(seat, c->xdg_surface->surface,
		keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
}

void
focusnext(const Arg *arg)
{
	/* Cycle to the next client */
	if (wl_list_length(&clients) < 2) {
		return;
	}
	Client *c = wl_container_of(clients.next, c, link);
	Client *n = wl_container_of(c->link.next, n, link);
	focus(n, n->xdg_surface->surface);
	/* Move the previous client to the end of the list */
	wl_list_remove(&c->link);
	wl_list_insert(clients.prev, &c->link);
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
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
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
	for (int i = 0; i < LENGTH(keys); i++) {
		if (sym == keys[i].keysym &&
				CLEANMASK(mods) == CLEANMASK(keys[i].mod) &&
				keys[i].func) {
			keys[i].func(&keys[i].arg);
			handled = true;
		}
	}
	return handled;
}

void
keypress(struct wl_listener *listener, void *data)
{
	/* This event is raised when a key is pressed or released. */
	Keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct wlr_event_keyboard_key *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			keyboard->device->keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t mods = wlr_keyboard_get_modifiers(keyboard->device->keyboard);
	if (event->state == WLR_KEY_PRESSED) {
		/* On _press_, attempt to process a compositor keybinding. */
		for (int i = 0; i < nsyms; i++) {
			handled = keybinding(mods, syms[i]) || handled;
		}
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(seat, keyboard->device);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	Keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(seat, keyboard->device);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
		&keyboard->device->keyboard->modifiers);
}

void
maprequest(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *c = wl_container_of(listener, c, map);
	c->mapped = true;
	focus(c, c->xdg_surface->surface);
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
	/* If the mode is non-passthrough, delegate to those functions. */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position. */
		grabbed_client->x = cursor->x - grab_x;
		grabbed_client->y = cursor->y - grab_y;
		return;
	} else if (cursor_mode == CurResize) {
		/*
		 * Note that I took some shortcuts here. In a more fleshed-out
		 * compositor, you'd wait for the client to prepare a buffer at
		 * the new size, then commit any movement that was prepared.
		 */
		double dx = cursor->x - grab_x;
		double dy = cursor->y - grab_y;
		wlr_xdg_toplevel_set_size(grabbed_client->xdg_surface,
				grab_width + dx, grab_height + dy);
		return;
	}

	/* Otherwise, find the client under the pointer and send the event along. */
	double sx, sy;
	struct wlr_surface *surface = NULL;
	Client *c = xytoclient(cursor->x, cursor->y,
			&surface, &sx, &sy);
	if (!c) {
		/* If there's no client under the cursor, set the cursor image to a
		 * default. This is what makes the cursor image appear when you move it
		 * around the screen, not over any clients. */
		wlr_xcursor_manager_set_cursor_image(
				cursor_mgr, "left_ptr", cursor);
	}
	if (surface) {
		bool focus_changed = seat->pointer_state.focused_surface != surface;
		/*
		 * "Enter" the surface if necessary. This lets the client know that the
		 * cursor has entered one of its surfaces.
		 *
		 * Note that this gives the surface "pointer focus", which is distinct
		 * from keyboard focus. You get pointer focus by moving the pointer over
		 * a window.
		 */
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		if (!focus_changed) {
			/* The enter event contains coordinates, so we only need to notify
			 * on motion if the focus did not change. */
			wlr_seat_pointer_notify_motion(seat, time, sx, sy);
		}
	} else {
		/* Clear pointer focus so future button events and such are not sent to
		 * the last client to have the cursor over it. */
		wlr_seat_pointer_clear_focus(seat);
	}
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
	double sx, sy;
	struct wlr_surface *surface;
	Client *c = xytoclient(cursor->x, cursor->y,
			&surface, &sx, &sy);
	if (!c) {
		return;
	}
	moveresize(c, CurMove);
}

void
moveresize(Client *c, unsigned int mode)
{
	/* This function sets up an interactive move or resize operation, where the
	 * compositor stops propagating pointer events to clients and instead
	 * consumes them itself, to move or resize windows. */
	struct wlr_surface *focused_surface =
		seat->pointer_state.focused_surface;
	if (c->xdg_surface->surface != focused_surface) {
		/* Deny move/resize requests from unfocused clients. */
		return;
	}
	grabbed_client = c;
	cursor_mode = mode;
	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(c->xdg_surface, &geo_box);
	if (mode == CurMove) {
		grab_x = cursor->x - c->x;
		grab_y = cursor->y - c->y;
	} else {
		grab_x = cursor->x + geo_box.x;
		grab_y = cursor->y + geo_box.y;
	}
	grab_width = geo_box.width;
	grab_height = geo_box.height;
}

void
quit(const Arg *arg)
{
	wl_display_terminate(wl_display);
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
	if (texture == NULL) {
		return;
	}

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
	struct wlr_box box = {
		.x = ox * output->scale,
		.y = oy * output->scale,
		.width = surface->current.width * output->scale,
		.height = surface->current.height * output->scale,
	};

	/*
	 * Those familiar with OpenGL are also familiar with the role of matricies
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
	wlr_matrix_project_box(matrix, &box, transform, 0,
		output->transform_matrix);

	/* This takes our matrix, the texture, and an alpha, and performs the actual
	 * rendering on the GPU. */
	wlr_render_texture_with_matrix(renderer, texture, matrix, 1);

	/* This lets the client know that we've displayed that frame and it can
	 * prepare another one now if it likes. */
	wlr_surface_send_frame_done(surface, rdata->when);
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
	if (!wlr_output_attach_render(m->wlr_output, NULL)) {
		return;
	}
	/* The "effective" resolution can change if you rotate your outputs. */
	int width, height;
	wlr_output_effective_resolution(m->wlr_output, &width, &height);
	/* Begin the renderer (calls glViewport and some other GL sanity checks) */
	wlr_renderer_begin(renderer, width, height);

	float color[4] = {0.3, 0.3, 0.3, 1.0};
	wlr_renderer_clear(renderer, color);

	/* Each subsequent window we render is rendered on top of the last. Because
	 * our client list is ordered front-to-back, we iterate over it backwards. */
	Client *c;
	wl_list_for_each_reverse(c, &clients, link) {
		if (!c->mapped) {
			/* An unmapped client should not be rendered. */
			continue;
		}
		struct render_data rdata = {
			.output = m->wlr_output,
			.when = &now,
			.x = c->x,
			.y = c->y,
		};
		/* This calls our render function for each surface among the
		 * xdg_surface's toplevel and popups. */
		wlr_xdg_surface_for_each_surface(c->xdg_surface,
				render, &rdata);
	}

	/* Hardware cursors are rendered by the GPU on a separate plane, and can be
	 * moved around without re-rendering what's beneath them - which is more
	 * efficient. However, not all hardware supports hardware cursors. For this
	 * reason, wlroots provides a software fallback, which we ask it to render
	 * here. wlr_cursor handles configuring hardware vs software cursors for you,
	 * and this function is a no-op when hardware cursors are in use. */
	wlr_output_render_software_cursors(m->wlr_output, NULL);

	/* Conclude rendering and swap the buffers, showing the final frame
	 * on-screen. */
	wlr_renderer_end(renderer);
	wlr_output_commit(m->wlr_output);
}

void
resizemouse(const Arg *arg)
{
	double sx, sy;
	struct wlr_surface *surface;
	Client *c = xytoclient(cursor->x, cursor->y,
			&surface, &sx, &sy);
	if (!c) {
		return;
	}
	struct wlr_box geo_box;
	wlr_xdg_surface_get_geometry(c->xdg_surface, &geo_box);
	wlr_cursor_warp_closest(cursor, NULL,
			c->x + geo_box.x + geo_box.width,
			c->y + geo_box.y + geo_box.height);
	moveresize(c, CurResize);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is rasied by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		seat->pointer_state.focused_client;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
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
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	c->mapped = false;
}

Client *
xytoclient(double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy)
{
	/* This iterates over all of our surfaces and attempts to find one under the
	 * cursor. This relies on clients being ordered from top-to-bottom. */
	Client *c;
	wl_list_for_each(c, &clients, link) {
		if (xytosurface(c, lx, ly, surface, sx, sy)) {
			return c;
		}
	}
	return NULL;
}

bool
xytosurface(Client *c, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy)
{
	/*
	 * XDG toplevels may have nested surfaces, such as popup windows for context
	 * menus or tooltips. This function tests if any of those are underneath the
	 * coordinates lx and ly (in output Layout Coordinates). If so, it sets the
	 * surface pointer to that wlr_surface and the sx and sy coordinates to the
	 * coordinates relative to that surface's top-left corner.
	 */
	double client_sx = lx - c->x;
	double client_sy = ly - c->y;

	struct wlr_surface_state *state = &c->xdg_surface->surface->current;

	double _sx, _sy;
	struct wlr_surface *_surface = NULL;
	_surface = wlr_xdg_surface_surface_at(
			c->xdg_surface, client_sx, client_sy, &_sx, &_sy);

	if (_surface != NULL) {
		*sx = _sx;
		*sy = _sy;
		*surface = _surface;
		return true;
	}

	return false;
}

int
main(int argc, char *argv[])
{
	wlr_log_init(WLR_DEBUG, NULL);
	char *startup_cmd = NULL;
	pid_t startup_pid = -1;

	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	wl_display = wl_display_create();
	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. The NULL argument here optionally allows you
	 * to pass in a custom renderer if wlr_renderer doesn't meet your needs. The
	 * backend uses the renderer, for example, to fall back to software cursors
	 * if the backend does not support hardware cursors (some older GPUs
	 * don't). */
	backend = wlr_backend_autocreate(wl_display, NULL);

	/* If we don't provide a renderer, autocreate makes a GLES2 renderer for us.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	renderer = wlr_backend_get_renderer(backend);
	wlr_renderer_init_wl_display(renderer, wl_display);

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. */
	wlr_compositor_create(wl_display, renderer);
	wlr_data_device_manager_create(wl_display);

	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create();

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	new_output.notify = createmon;
	wl_signal_add(&backend->events.new_output, &new_output);

	/* Set up our list of clients and the xdg-shell. The xdg-shell is a Wayland
	 * protocol which is used for application windows. For more detail on
	 * shells, refer to my article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	wl_list_init(&clients);
	xdg_shell = wlr_xdg_shell_create(wl_display);
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
	 * HiDPI support). We add a cursor theme at scale factor 1 to begin with. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(cursor_mgr, 1);

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
	seat = wlr_seat_create(wl_display, "seat0");
	request_cursor.notify = setcursor;
	wl_signal_add(&seat->events.request_set_cursor,
			&request_cursor);

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(wl_display);
	if (!socket) {
		wlr_backend_destroy(backend);
		return 1;
	}

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend)) {
		wlr_backend_destroy(backend);
		wl_display_destroy(wl_display);
		return 1;
	}

	/* Set the WAYLAND_DISPLAY environment variable to our socket and run the
	 * startup command if requested. */
	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd) {
		startup_pid = fork();
		if (startup_pid < 0) {
			perror("startup: fork");
			wl_display_destroy(wl_display);
			return 1;
		}
		if (startup_pid == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
			perror("startup: execl");
			wl_display_destroy(wl_display);
			return 1;
		}
	}
	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);
	wl_display_run(wl_display);

	if (startup_cmd) {
		kill(startup_pid, SIGTERM);
		waitpid(startup_pid, NULL, 0);
	}

	/* Once wl_display_run returns, we shut down the server. */
	wl_display_destroy_clients(wl_display);
	wl_display_destroy(wl_display);
	return 0;
}
