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
	{ "X11-1",    0.5,  1,      1,    &layouts[0] },
	{ "eDP-1",    0.5,  1,      2,    &layouts[0] },
	{ "HDMI-A-1", 0.5,  1,      1,    &layouts[0] },
	/* defaults (required) */
	{ NULL,       0.5,  1,      1,    &layouts[0] },
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	.rules = NULL,
	.model = NULL,
	.layout = "dvorak",
	.variant = NULL,
	.options = "ctrl:nocaps,altwin:swap_lalt_lwin,terminate:ctrl_alt_bksp",
};

#define MODKEY WLR_MODIFIER_ALT

/* commands */
static const char *termcmd[]  = { "kitty", "-o", "linux_display_server=wayland", NULL };

static const Key keys[] = {
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Return, spawn,     {.v = termcmd } },
	{ MODKEY,                    XKB_KEY_Escape, quit,      {0} },
	{ MODKEY,                    XKB_KEY_F1,     focusnext, {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_L,      setlayout, {.v = &layouts[0]} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_F,      setlayout, {.v = &layouts[1]} },
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,  movemouse,   {0} },
	{ MODKEY, BTN_RIGHT, resizemouse, {0} },
};
