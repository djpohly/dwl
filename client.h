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
	struct wlr_xdg_surface *surface;
#ifdef XWAYLAND
	struct wlr_xwayland_surface *xsurface;
	if (wlr_surface_is_xwayland_surface(s)
			&& (xsurface = wlr_xwayland_surface_from_wlr_surface(s))) {
		wlr_xwayland_surface_activate(xsurface, activated);
		return;
	}
#endif
	if (wlr_surface_is_xdg_surface(s)
			&& (surface = wlr_xdg_surface_from_wlr_surface(s))
			&& surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		wlr_xdg_toplevel_set_activated(surface, activated);
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

static inline void
client_get_size_hints(Client *c, struct wlr_box *max, struct wlr_box *min)
{
	struct wlr_xdg_toplevel *toplevel;
	struct wlr_xdg_toplevel_state *state;
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		struct wlr_xwayland_surface_size_hints *size_hints;
		size_hints = c->surface.xwayland->size_hints;
		if (size_hints) {
			max->width = size_hints->max_width;
			max->height = size_hints->max_height;
			min->width = size_hints->min_width;
			min->height = size_hints->min_height;
		}
		return;
	}
#endif
	toplevel = c->surface.xdg->toplevel;
	state = &toplevel->current;
	max->width = state->max_width;
	max->height = state->max_height;
	min->width = state->min_width;
	min->height = state->min_height;
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
	struct wlr_box min = {0}, max = {0};
	client_get_size_hints(c, &max, &min);

#ifdef XWAYLAND
	if (client_is_x11(c)) {
		struct wlr_xwayland_surface *surface = c->surface.xwayland;
		if (surface->modal)
			return 1;

		for (size_t i = 0; i < surface->window_type_len; i++)
			if (surface->window_type[i] == netatom[NetWMWindowTypeDialog]
					|| surface->window_type[i] == netatom[NetWMWindowTypeSplash]
					|| surface->window_type[i] == netatom[NetWMWindowTypeToolbar]
					|| surface->window_type[i] == netatom[NetWMWindowTypeUtility])
				return 1;

		return ((min.width > 0 || min.height > 0 || max.width > 0 || max.height > 0)
			&& (min.width == max.width || min.height == max.height))
			|| c->surface.xwayland->parent;
	}
#endif

	return ((min.width > 0 || min.height > 0 || max.width > 0 || max.height > 0)
		&& (min.width == max.width || min.height == max.height))
		|| c->surface.xdg->toplevel->parent;
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
	wlr_xdg_toplevel_send_close(c->surface.xdg);
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
	wlr_xdg_toplevel_set_fullscreen(c->surface.xdg, fullscreen);
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
	return wlr_xdg_toplevel_set_size(c->surface.xdg, width, height);
}

static inline void
client_set_tiled(Client *c, uint32_t edges)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return;
#endif
	wlr_xdg_toplevel_set_tiled(c->surface.xdg, edges);
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
client_restack_surface(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		wlr_xwayland_surface_restack(c->surface.xwayland, NULL,
				XCB_STACK_MODE_ABOVE);
#endif
	return;
}

static inline Client *
client_from_wlr_surface(struct wlr_surface *s)
{
	struct wlr_xdg_surface *surface;

#ifdef XWAYLAND
	struct wlr_xwayland_surface *xsurface;
	if (s->role_data && wlr_surface_is_xwayland_surface(s)
			&& (xsurface = wlr_xwayland_surface_from_wlr_surface(s)))
		return xsurface->data;
#endif
	if (s->role_data && wlr_surface_is_xdg_surface(s)
			&& (surface = wlr_xdg_surface_from_wlr_surface(s))
			&& surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL)
		return surface->data;

	return NULL;
}

static inline void *
toplevel_from_popup(struct wlr_xdg_popup *popup)
{
	struct wlr_xdg_surface *surface = popup->base;

	while (1) {
		switch (surface->role) {
		case WLR_XDG_SURFACE_ROLE_POPUP:
			if (wlr_surface_is_layer_surface(surface->popup->parent))
				return wlr_layer_surface_v1_from_wlr_surface(surface->popup->parent)->data;
			else if (!wlr_surface_is_xdg_surface(surface->popup->parent))
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
