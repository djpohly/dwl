static const struct xkb_rule_names xkb_rules = {
	.rules = NULL,
	.model = NULL,
	.layout = "dvorak",
	.variant = NULL,
	.options = "ctrl:nocaps,altwin:swap_lalt_lwin,terminate:ctrl_alt_bksp",
};

#define MODKEY WLR_MODIFIER_ALT

static const Key keys[] = {
	{ MODKEY, XKB_KEY_Escape, quit,      {0} },
	{ MODKEY, XKB_KEY_F1,     focusnext, {0} },
};
