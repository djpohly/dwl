/*
 * Attempt to consolidate unavoidable suck into one file, away from dwl.c.  This
 * file is not meant to be pretty.  We use a .h file with static inline
 * functions instead of a separate .c module, or function pointers like sway, so
 * that they will simply compile out if the chosen #defines leave them unused.
 */

/* Leave this function first; it's used in the others */
static inline int
client_is_x11(Client *c)
{
#ifdef XWAYLAND
	return c->type == X11Managed || c->type == X11Unmanaged;
#else
	return 0;
#endif
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
				wlr_xdg_surface_from_wlr_surface(s), activated);
}

static inline void
client_for_each_surface(Client *c, wlr_surface_iterator_func_t fn, void *data)
{
#ifdef XWAYLAND
	if (client_is_x11(c)) {
		wlr_surface_for_each_surface(c->surface.xwayland->surface,
				fn, data);
		return;
	}
#endif
	wlr_xdg_surface_for_each_surface(c->surface.xdg, fn, data);
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

static inline struct wlr_surface *
client_surface(Client *c)
{
#ifdef XWAYLAND
	if (client_is_x11(c))
		return c->surface.xwayland->surface;
#endif
	return c->surface.xdg->surface;
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
