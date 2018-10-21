/* See LICENSE file for copyright and license details. */
#include "dwm.h"
#include <X11/keysym.h>

/* appearance */
const char font[] = "fixed";
const char normbordercolor[] = "#cccccc";
const char normbgcolor[]     = "#cccccc";
const char normfgcolor[]     = "#000000";
const char selbordercolor[]  = "#0066ff";
const char selbgcolor[]      = "#0066ff";
const char selfgcolor[]      = "#ffffff";

const unsigned int borderpx  = 1;        /* border pixel of windows */
const unsigned int snap      = 32;       /* snap pixel */
const bool showbar           = true;     /* false means no bar */
const bool topbar            = true;     /* false means bottom bar */

/* tagging */
const char *tags[NUM_TAGS] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* layout(s) */
const float mfact      = 0.55; /* factor of master area size [0.05..0.95] */
const bool resizehints = true; /* True means respect size hints in tiled resizals */

const Layout layouts[NUM_LAYOUTS] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* key definitions */
#define MODKEY XCB_MOD_MASK_4											/* MOD_MASK_1 is Alt.  MOD_MASK_4 is Windows/Command key. */

#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY,      view,           {.ui = 1 << TAG} }, \
	{ MODKEY|XCB_MOD_MASK_CONTROL,  KEY,      toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|XCB_MOD_MASK_SHIFT,    KEY,      tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|XCB_MOD_MASK_CONTROL|XCB_MOD_MASK_SHIFT, KEY, toggletag, {.ui = 1 << TAG} },

/* commands */
const char *dmenucmd[] = { "dmenu_run", "-fn", font, "-nb", normbgcolor, "-nf", normfgcolor, "-sb", selbgcolor, "-sf", selfgcolor, NULL };
const char *termcmd[]  = { "st", NULL };

const Key keys[] = {
	/* modifier                     key        function        argument */
	{ MODKEY,                       XK_p,      spawn,          {.v = dmenucmd } },
	{ MODKEY|XCB_MOD_MASK_SHIFT,    XK_Return, spawn,          {.v = termcmd } },
	{ MODKEY,                       XK_b,      togglebar,      {0} },
	{ MODKEY,                       XK_j,      focusstack,     {.i = +1 } },
	{ MODKEY,                       XK_k,      focusstack,     {.i = -1 } },
	{ MODKEY,                       XK_h,      setmfact,       {.f = -0.05} },
	{ MODKEY,                       XK_l,      setmfact,       {.f = +0.05} },
	{ MODKEY,                       XK_Return, zoom,           {0} },
	{ MODKEY,                       XK_Tab,    view,           {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT,    XK_c,      killclient,     {0} },
	{ MODKEY,                       XK_t,      setlayout,      {.v = &layouts[0]} },
	{ MODKEY,                       XK_f,      setlayout,      {.v = &layouts[1]} },
	{ MODKEY,                       XK_m,      setlayout,      {.v = &layouts[2]} },
	{ MODKEY,                       XK_space,  setlayout,      {0} },
	{ MODKEY|XCB_MOD_MASK_SHIFT,    XK_space,  togglefloating, {0} },
	{ MODKEY,                       XK_0,      view,           {.ui = ~0 } },
	{ MODKEY|XCB_MOD_MASK_SHIFT,    XK_0,      tag,            {.ui = ~0 } },
	{ MODKEY,                       XK_comma,  focusmon,       {.i = -1 } },
	{ MODKEY,                       XK_period, focusmon,       {.i = +1 } },
	{ MODKEY|XCB_MOD_MASK_SHIFT,    XK_comma,  tagmon,         {.i = -1 } },
	{ MODKEY|XCB_MOD_MASK_SHIFT,    XK_period, tagmon,         {.i = +1 } },
	TAGKEYS(                        XK_1,                      0)
	TAGKEYS(                        XK_2,                      1)
	TAGKEYS(                        XK_3,                      2)
	TAGKEYS(                        XK_4,                      3)
	TAGKEYS(                        XK_5,                      4)
	TAGKEYS(                        XK_6,                      5)
	TAGKEYS(                        XK_7,                      6)
	TAGKEYS(                        XK_8,                      7)
	TAGKEYS(                        XK_9,                      8)
	{ MODKEY|XCB_MOD_MASK_SHIFT,    XK_q,      quit,           {0} },
	{ 0,				0,	   NULL,	   {0} },
};

/* button definitions */
/* click can be ClkLtSymbol, ClkStatusText, ClkWinTitle, ClkClientWin, or ClkRootWin */
const Button buttons[] = {
	/* click                event mask      button                    function        argument */
	{ ClkLtSymbol,          0,              XCB_BUTTON_INDEX_1,        setlayout,      {0} },
	{ ClkLtSymbol,          0,              XCB_BUTTON_INDEX_3,        setlayout,      {.v = &layouts[2]} },
	{ ClkWinTitle,          0,              XCB_BUTTON_INDEX_2,        zoom,           {0} },
	{ ClkStatusText,        0,              XCB_BUTTON_INDEX_2,        spawn,          {.v = termcmd } },
	{ ClkClientWin,         MODKEY,         XCB_BUTTON_INDEX_1,        movemouse,      {0} },
	{ ClkClientWin,         MODKEY,         XCB_BUTTON_INDEX_2,        togglefloating, {0} },
	{ ClkClientWin,         MODKEY,         XCB_BUTTON_INDEX_3,        resizemouse,    {0} },
	{ ClkTagBar,            0,              XCB_BUTTON_INDEX_1,        view,           {0} },
	{ ClkTagBar,            0,              XCB_BUTTON_INDEX_3,        toggleview,     {0} },
	{ ClkTagBar,            MODKEY,         XCB_BUTTON_INDEX_1,        tag,            {0} },
	{ ClkTagBar,            MODKEY,         XCB_BUTTON_INDEX_3,        toggletag,      {0} },
	{ 0,			0,	   	0,	   		   NULL,	   {0} },
};

