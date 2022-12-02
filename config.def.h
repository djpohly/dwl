static const int sloppyfocus        = 1;
static const unsigned int borderpx  = 1;
static const int lockfullscreen     = 1;
static const float rootcolor[]      = {0.109, 0.109, 0.109, 1.0};
static const float bordercolor[]    = {0.5, 0.5, 0.5, 1.0};
static const float focuscolor[]     = {1.0, 0.0, 0.0, 1.0};
static const float fullscreen_bg[]  = {0.1, 0.1, 0.1, 1.0};
static const char *tags[] = { "1", "2", "3", "4" };

static const Rule rules[] = {
	/* app_id     title       tags mask     isfloating   monitor */
	{ "firefox",  NULL,       2 << 4,       0,           -1 },
};
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },
	{ "><>",      NULL },
};
static const MonitorRule monrules[] = {
	/* name mfact nmaster scale layout rotate/reflect */
	{ "DP-1", 0.50, 1, 1, &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL },
};
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	.options = NULL,
	.layout = "tr",
};
static const int repeat_rate = 25;
static const int repeat_delay = 600;
#define MODKEY WLR_MODIFIER_LOGO
#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,            view,            {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL,  KEY,            toggleview,      {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,           tag,             {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_CTRL|WLR_MODIFIER_SHIFT,SKEY,toggletag, {.ui = 1 << TAG} }
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }
static const char *termcmd[] = { "foot", NULL };
static const char *menucmd[] = { "dmenu-wl_run", "-nb", "#282828", "-nf", "#928374", "-sb", "#3c3836", "-sf", "#a89984", NULL };
static const Key keys[] = {
	/* modifier                  key                 function        argument */
	{ MODKEY,                    XKB_KEY_p,          spawn,          {.v = menucmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Return,     spawn,          {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_j,          focusstack,     {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,          focusstack,     {.i = -1} },
	{ MODKEY,                    XKB_KEY_i,          incnmaster,     {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,          incnmaster,     {.i = -1} },
	{ MODKEY,                    XKB_KEY_h,          setmfact,       {.f = -0.05} },
	{ MODKEY,                    XKB_KEY_l,          setmfact,       {.f = +0.05} },
	{ MODKEY,                    XKB_KEY_Return,     zoom,           {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_C,          killclient,     {0} },
	{ MODKEY,                    XKB_KEY_t,          setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                    XKB_KEY_f,          setlayout,      {.v = &layouts[1]} },
	{ MODKEY,                    XKB_KEY_space,      setlayout,      {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_space,      togglefloating, {0} },
	{ MODKEY,                    XKB_KEY_e,         togglefullscreen, {0} },
	TAGKEYS(          XKB_KEY_1, XKB_KEY_exclam,                     0),
	TAGKEYS(          XKB_KEY_2, XKB_KEY_apostrophe,                 1),
	TAGKEYS(          XKB_KEY_3, XKB_KEY_caret,                      2),
	TAGKEYS(          XKB_KEY_4, XKB_KEY_plus,                       3),
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Q,          quit,           {0} },
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};
static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
};