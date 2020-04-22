/* monitors */
static const MonitorRule monrules[] = {
	/* name     scale */
	{ "X11-1",    1 },
	{ "eDP-1",    2 },
	{ "HDMI-A-1", 1 },
	/* defaults */
	{ NULL,       1 },
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
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,  movemouse,   {0} },
	{ MODKEY, BTN_RIGHT, resizemouse, {0} },
};
