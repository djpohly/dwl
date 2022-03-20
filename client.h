/*
 * Attempt to consolidate unavoidable suck into one file, away from dwl.c.  This
 * file is not meant to be pretty.  We use a .h file with static inline
 * functions instead of a separate .c module, or function pointers like sway, so
 * that they will simply compile out if the chosen #defines leave them unused.
 */

/* Leave these functions first; they're used in the others */
static inline int
client_is_x11(Client *c)
{
#ifdef XWAYLAND
	return c->type == X11Managed || c->type == X11Unmanaged;
#else
	return 0;
#endif
}

static inline struct wlr_surface *
client_surface(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->surface;
#endif
	return c->surface.xdg->surface;
}

/* The others */
static inline void
client_activate_surface(struct wlr_surface *s, int activated)
{
#ifdef XWAYLAND
	if (wlr_surface_is_xwayland_surface(s)) {
		wlr_xwayland_surface_activate(
				wlr_xwayland_surface_from_wlr_surface(s), activated);
		return;
	}
#endif
	if (wlr_surface_is_xdg_surface(s))
		wlr_xdg_toplevel_set_activated(
				wlr_xdg_surface_from_wlr_surface(s)->toplevel, activated);
}

static inline void
client_for_each_surface(Client *c, wlr_surface_iterator_func_t fn, void *data)
{
	wlr_surface_for_each_surface(client_surface(c), fn, data);
#ifdef XWAYLAND
	if (client_is_x11(c))
		return;
#endif
	wlr_xdg_surface_for_each_popup_surface(c->surface.xdg, fn, data);
}

static inline const char *
client_get_appid(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->class;
#endif
	return c->surface.xdg->toplevel->app_id;
}

static inline void
client_get_geometry(Client *c, struct wlr_box *geom)
{
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		geom->x = c->surface.xwayland->x;
		geom->y = c->surface.xwayland->y;
		geom->width = c->surface.xwayland->width;
		geom->height = c->surface.xwayland->height;
		return;
	}
#endif
	wlr_xdg_surface_get_geometry(c->surface.xdg, geom);
}

static inline const char *
client_get_title(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->title;
#endif
	return c->surface.xdg->toplevel->title;
}

static inline int
client_is_float_type(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		for (size_t i = 0; i < c->surface.xwayland->window_type_len; i++)
			if (c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeDialog] ||
					c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeSplash] ||
					c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeToolbar] ||
					c->surface.xwayland->window_type[i] == netatom[NetWMWindowTypeUtility])
				return 1;
#endif
	return 0;
}

static inline int
client_wants_fullscreen(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->fullscreen;
#endif
	return c->surface.xdg->toplevel->requested.fullscreen;
}

static inline int
client_is_unmanaged(Client *c)
{
#ifdef XWAYLAND
	return c->type == X11Unmanaged;
#endif
	return 0;
}

static inline void
client_send_close(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_xwayland_surface_close(c->surface.xwayland);
		return;
	}
#endif
	wlr_xdg_toplevel_send_close(c->surface.xdg->toplevel);
}

static inline void
client_set_fullscreen(Client *c, int fullscreen)
{
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_xwayland_surface_set_fullscreen(c->surface.xwayland, fullscreen);
		return;
	}
#endif
	wlr_xdg_toplevel_set_fullscreen(c->surface.xdg->toplevel, fullscreen);
}

static inline uint32_t
client_set_size(Client *c, uint32_t width, uint32_t height)
{
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_xwayland_surface_configure(c->surface.xwayland,
				c->geom.x, c->geom.y, width, height);
		return 0;
	}
#endif
	return wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, width, height);
}

static inline void
client_set_tiled(Client *c, uint32_t edges)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return;
#endif
	wlr_xdg_toplevel_set_tiled(c->surface.xdg->toplevel, edges);
}

static inline struct wlr_surface *
client_surface_at(Client *c, double cx, double cy, double *sx, double *sy)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return wlr_surface_surface_at(c->surface.xwayland->surface,
				cx, cy, sx, sy);
#endif
	return wlr_xdg_surface_surface_at(c->surface.xdg, cx, cy, sx, sy);
}

static inline void
client_min_size(Client *c, int *width, int *height)
{
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_xdg_toplevel_state *state;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		struct wlr_xwayland_surface_size_hints *size_hints;
		size_hints = c->surface.xwayland->size_hints;
		*width = size_hints->min_width;
		*height = size_hints->min_height;
		return;
	}
#endif
	toplevel = c->surface.xdg->toplevel;
	state = &toplevel->current;
	*width = state->min_width;
	*height = state->min_height;
}

static inline Client *
client_from_popup(struct wlr_xdg_popup *popup)
{
	struct wlr_xdg_surface *surface = popup->base;

	while (1) {
		switch (surface->role) {
		case WLR_XDG_SURFACE_ROLE_POPUP:
			if (!wlr_surface_is_xdg_surface(surface->popup->parent))
				return NULL;

			surface = wlr_xdg_surface_from_wlr_surface(surface->popup->parent);
			break;
		case WLR_XDG_SURFACE_ROLE_TOPLEVEL:
				return surface->data;
		case WLR_XDG_SURFACE_ROLE_NONE:
			return NULL;
		}
	}
}
