/* appearance */
static const float rootcolor[]      = {0.3, 0.3, 0.3, 1.0};

/* layout(s) */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },
	{ "><>",      NULL },    /* no layout function means floating behavior */
};

/* monitors */
static const MonitorRule monrules[] = {
	/* name       mfact nmaster scale layout */
	/* example of a HiDPI laptop monitor:
	{ "eDP-1",    0.5,  1,      2,    &layouts[0] },
	*/
	/* defaults */
	{ NULL,       0.55, 1,      1,    &layouts[0] },
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
};

#define MODKEY WLR_MODIFIER_ALT

/* commands */
static const char *termcmd[]  = { "kitty", "-o", "linux_display_server=wayland", NULL };

static const Key keys[] = {
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Return, spawn,     {.v = termcmd } },
	{ MODKEY,                    XKB_KEY_j,      focusnext, {0} },
	{ MODKEY,                    XKB_KEY_t,      setlayout, {.v = &layouts[0]} },
	{ MODKEY,                    XKB_KEY_f,      setlayout, {.v = &layouts[1]} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q,      quit,      {0} },
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,  movemouse,   {0} },
	{ MODKEY, BTN_RIGHT, resizemouse, {0} },
};
