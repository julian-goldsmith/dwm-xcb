/* See LICENSE file for copyright and license details.
 *
 * dynamic window manager is designed like any other X client as well. It is
 * driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance.  Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dwm are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag.  Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */

#include <errno.h>
#include <locale.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_event.h>
#include <assert.h>
#include <stdbool.h>

#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif /* XINERAMA */

/* macros */
#define BUTTONMASK              (XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE)
#define CLEANMASK(mask)         (mask & ~(numlockmask|XCB_MOD_MASK_LOCK))
#define INRECT(X,Y,RX,RY,RW,RH) ((X) >= (RX) && (X) < (RX) + (RW) && (Y) >= (RY) && (Y) < (RY) + (RH))
#define ISVISIBLE(C)            ((C->tags & C->mon->tagset[C->mon->seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MAX(A, B)               ((A) > (B) ? (A) : (B))
#define MIN(A, B)               ((A) < (B) ? (A) : (B))
#define MOUSEMASK               (BUTTONMASK|XCB_EVENT_MASK_POINTER_MOTION)
#define WIDTH(X)                ((X)->w + 2 * (X)->bw)
#define HEIGHT(X)               ((X)->h + 2 * (X)->bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (textnw(X, strlen(X)) + dc.font.height)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast };        /* cursor */
enum { ColBorder, ColFG, ColBG, ColLast };              /* color */
enum { NetSupported, NetWMName, NetWMState,
       NetWMFullscreen, NetLast };                      /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMLast };        /* default atoms */
enum { ClkTagBar, ClkLtSymbol, ClkStatusText, ClkWinTitle,
       ClkClientWin, ClkRootWin, ClkLast };             /* clicks */

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int click;
	unsigned int mask;
	unsigned int button;
	void (*func)(const Arg *arg);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Client Client;
struct Client {
	char name[256];
	float mina, maxa;
	int x, y, w, h;
	int oldx, oldy, oldw, oldh;
	int basew, baseh, incw, inch, maxw, maxh, minw, minh;
	int bw, oldbw;
	unsigned int tags;
	bool isfixed, isfloating, isurgent, oldstate;
	Client *next;
	Client *snext;
	Monitor *mon;
	xcb_window_t win;
};

typedef struct {
	int x, y, w, h;
	uint32_t norm[ColLast];
	uint32_t sel[ColLast];
	xcb_gcontext_t gc;
	struct {
		int ascent;
		int descent;
		int height;
		xcb_font_t xfont;
		bool set;
	} font;
} DC; /* draw context */

typedef struct {
	unsigned int mod;
	xcb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

struct Monitor {
	char ltsymbol[16];
	float mfact;
	int num;
	int by;               /* bar geometry */
	int mx, my, mw, mh;   /* screen size */
	int wx, wy, ww, wh;   /* window area  */
	unsigned int seltags;
	unsigned int sellt;
	unsigned int tagset[2];
	bool showbar;
	bool topbar;
	Client *clients;
	Client *sel;
	Client *stack;
	Monitor *next;
	xcb_window_t barwin;
	const Layout *lt[2];
};

typedef struct {
	const char *class;
	const char *instance;
	const char *title;
	unsigned int tags;
	bool isfloating;
	int monitor;
} Rule;

/* function declarations */
static void applyrules(Client *c);
static bool applysizehints(Client *c, int *x, int *y, int *w, int *h, bool interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static int buttonpress(xcb_generic_event_t *e);
static void checkotherwm(void);
static void cleanup(void);
static void cleanupmon(Monitor *mon);
static void clearevent(int response_type);
static void clearurgent(Client *c);
static int clientmessage(xcb_generic_event_t *e);
static void configure(Client *c);
static int configurenotify(xcb_generic_event_t *e);
static int configurerequest(xcb_generic_event_t *e);
static Monitor *createmon(void);
static int destroynotify(xcb_generic_event_t *e);
static void detach(Client *c);
static void detachstack(Client *c);
static void die(const char *errstr, ...);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void drawsquare(bool filled, bool empty, bool invert, uint32_t col[ColLast], xcb_window_t w);
static void drawtext(const char *text, uint32_t col[ColLast], bool invert, xcb_window_t w);
static int enternotify(xcb_generic_event_t *e);
static int expose(xcb_generic_event_t *e);
static void focus(Client *c);
static int focusin(xcb_generic_event_t *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static uint32_t getcolor(const char *colstr);
static bool getrootptr(int *x, int *y);
static xcb_atom_t getstate(xcb_window_t w);
static bool gettextprop(xcb_window_t w, xcb_atom_t atom, char *text, unsigned int size);
static void grabbuttons(Client *c, bool focused);
static void grabkeys(void);
static void initfont(const char *fontstr);
static bool isprotodel(Client *c);
static int keypress(xcb_generic_event_t *e);
static void killclient(const Arg *arg);
static void manage(xcb_window_t w);
static int mappingnotify(xcb_generic_event_t *e);
static int maprequest(xcb_generic_event_t *e);
static void monocle(Monitor *m);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static Monitor *ptrtomon(int x, int y);
static int propertynotify(xcb_generic_event_t *e);
static void quit(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h, bool interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void scan(void);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup(void);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static int textnw(const char *text, unsigned int len);
static void tile(Monitor *);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, bool setfocus);
static void unmanage(Client *c, bool destroyed);
static int unmapnotify(xcb_generic_event_t *e);
static bool updategeom(void);
static void updatebarpos(Monitor *m);
static void updatebars(void);
static void updatenumlockmask(void);
static void updatesizehints(Client *c);
static void updatestatus(void);
static void updatetitle(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(xcb_window_t w);
static Monitor *wintomon(xcb_window_t w);
static void zoom(const Arg *arg);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static unsigned int numlockmask = 0;
static xcb_atom_t wmatom[WMLast], netatom[NetLast];
static xcb_cursor_t cursor[CurLast];
static xcb_connection_t *conn = NULL;
static DC dc;
static Monitor *mons = NULL, *selmon = NULL;
static xcb_window_t root;
static xcb_screen_t *xscreen = NULL;
static xcb_key_symbols_t *syms = NULL;
static xcb_generic_error_t *err = NULL;

typedef struct
{
	uint32_t request;
	//xcb_generic_event_handler_t func;
	int (*func)(xcb_generic_event_t *e);
} handler_func_t;

static const handler_func_t handler_funs[] = {
	{ XCB_BUTTON_PRESS, buttonpress },
	{ XCB_CLIENT_MESSAGE, clientmessage },
	{ XCB_CONFIGURE_REQUEST, configurerequest },
	{ XCB_CONFIGURE_NOTIFY, configurenotify },
	{ XCB_DESTROY_NOTIFY, destroynotify },
	{ XCB_ENTER_NOTIFY, enternotify },
	{ XCB_EXPOSE, expose },
	{ XCB_FOCUS_IN, focusin },
	{ XCB_KEY_PRESS, keypress },
	{ XCB_MAPPING_NOTIFY, mappingnotify },
	{ XCB_MAP_REQUEST, maprequest },
	{ XCB_PROPERTY_NOTIFY, propertynotify },
	{ XCB_UNMAP_NOTIFY, unmapnotify },
	{ XCB_NONE, NULL }
};

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

static void _testerr(const char* file, const int line)
{
	if(err)
	{
		fprintf(stderr, "%s:%d - request returned error %i, \"%s\"\n", file, line,
			(int)err->error_code, xcb_event_get_error_label(err->error_code));
		free(err);
		err = NULL;
		assert(0);
	}
}

#define testerr() _testerr(__FILE__, __LINE__);

static void _testcookie(xcb_void_cookie_t cookie, const char* file, const int line)
{
	err = xcb_request_check(conn, cookie);
	_testerr(file, line);
}

#define testcookie(cookie) _testcookie(cookie, __FILE__, __LINE__);

static void* malloc_safe(size_t size)
{
	void *ret;
	if(!(ret = malloc(size)))
		die("fatal: could not malloc() %u bytes\n", size);
	memset(ret, 0, size);
	return ret;
}

// FIXME: this was removed, why?
static void clearevent(int response_type)
{
	xcb_generic_event_t *ev;

	while((ev = xcb_poll_for_event(conn)))
	{
		if(ev->response_type == response_type)
		{
			free(ev);
			break;
		}
		else
		{
			for(const handler_func_t* handler = handler_funs; handler->func != NULL; handler++)
			{
				if((ev->response_type & ~0x80) == handler->request)
					handler->func(ev);
			}	
		}
			
		free(ev);
	}
}

/* function implementations */
void applyrules(Client *c) {
	const char *class, *instance;
	unsigned int i;
	const Rule *r;
	Monitor *m;
	xcb_icccm_get_wm_class_reply_t class_reply;

	/* rule matching */
	c->isfloating = 0;
	c->tags = 0;

	if(xcb_icccm_get_wm_class_reply(conn, xcb_icccm_get_wm_class(conn, c->win), &class_reply, &err))
	{
		class = class_reply.class_name ? class_reply.class_name : broken;
		instance = class_reply.instance_name ? class_reply.instance_name : broken;
		for(i = 0; i < LENGTH(rules); i++) {
			r = &rules[i];
			if((!r->title || strstr(c->name, r->title))
			&& (!r->class || strstr(class, r->class))
			&& (!r->instance || strstr(instance, r->instance)))
			{
				c->isfloating = r->isfloating;
				c->tags |= r->tags;
				for(m = mons; m && m->num != r->monitor; m = m->next);
				if(m)
					c->mon = m;
			}
		}

		xcb_icccm_get_wm_class_reply_wipe(&class_reply);
	}
	else
		testerr();

	c->tags = c->tags & TAGMASK ? c->tags & TAGMASK : c->mon->tagset[c->mon->seltags];
}

bool applysizehints(Client *c, int *x, int *y, int *w, int *h, bool interact) {
	bool baseismin;
	Monitor *m = c->mon;

	/* set minimum possible */
	*w = MAX(1, *w);
	*h = MAX(1, *h);
	if(interact) {
		if(*x > sw)
			*x = sw - WIDTH(c);
		if(*y > sh)
			*y = sh - HEIGHT(c);
		if(*x + *w + 2 * c->bw < 0)
			*x = 0;
		if(*y + *h + 2 * c->bw < 0)
			*y = 0;
	}
	else {
		if(*x > m->mx + m->mw)
			*x = m->mx + m->mw - WIDTH(c);
		if(*y > m->my + m->mh)
			*y = m->my + m->mh - HEIGHT(c);
		if(*x + *w + 2 * c->bw < m->mx)
			*x = m->mx;
		if(*y + *h + 2 * c->bw < m->my)
			*y = m->my;
	}
	if(*h < bh)
		*h = bh;
	if(*w < bh)
		*w = bh;
	if(resizehints || c->isfloating) {
		/* see last two sentences in ICCCM 4.1.2.3 */
		baseismin = c->basew == c->minw && c->baseh == c->minh;
		if(!baseismin) { /* temporarily remove base dimensions */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for aspect limits */
		if(c->mina > 0 && c->maxa > 0) {
			if(c->maxa < (float)*w / *h)
				*w = *h * c->maxa + 0.5;
			else if(c->mina < (float)*h / *w)
				*h = *w * c->mina + 0.5;
		}
		if(baseismin) { /* increment calculation requires this */
			*w -= c->basew;
			*h -= c->baseh;
		}
		/* adjust for increment value */
		if(c->incw)
			*w -= *w % c->incw;
		if(c->inch)
			*h -= *h % c->inch;
		/* restore base dimensions */
		*w += c->basew;
		*h += c->baseh;
		*w = MAX(*w, c->minw);
		*h = MAX(*h, c->minh);
		if(c->maxw)
			*w = MIN(*w, c->maxw);
		if(c->maxh)
			*h = MIN(*h, c->maxh);
	}
	return *x != c->x || *y != c->y || *w != c->w || *h != c->h;
}

void arrange(Monitor *m) {
	if(m)
		showhide(m->stack);
	else for(m = mons; m; m = m->next)
		showhide(m->stack);
	focus(NULL);
	if(m)
		arrangemon(m);
	else for(m = mons; m; m = m->next)
		arrangemon(m);
}

void arrangemon(Monitor *m) {
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof m->ltsymbol);
	if(m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	restack(m);
}

void attach(Client *c) {
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void attachstack(Client *c) {
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

int buttonpress(xcb_generic_event_t *ev) {
	unsigned int i, x, click;
	Arg arg = {0};
	Client *c;
	Monitor *m;
	xcb_button_press_event_t *e = (xcb_button_press_event_t*)ev;

	click = ClkRootWin;
	/* focus monitor if necessary */
	if((m = wintomon(e->event)) && m != selmon) {
		unfocus(selmon->sel, true);
		selmon = m;
		focus(NULL);
	}
	if(e->event == selmon->barwin) {
		i = x = 0;
		do {
			x += TEXTW(tags[i]);
		} while(e->event_x >= x && ++i < LENGTH(tags));
		if(i < LENGTH(tags)) {
			click = ClkTagBar;
			arg.ui = 1 << i;
		}
		else if(e->event_x < x + blw)
			click = ClkLtSymbol;
		else if(e->event_x > selmon->wx + selmon->ww - TEXTW(stext))
			click = ClkStatusText;
		else
			click = ClkWinTitle;
	}
	else if((c = wintoclient(e->event))) {
		focus(c);
		click = ClkClientWin;
	}
	for(i = 0; i < LENGTH(buttons); i++)
		if(click == buttons[i].click && buttons[i].func && buttons[i].button == e->detail
		&& CLEANMASK(buttons[i].mask) == CLEANMASK(e->state))
			buttons[i].func(click == ClkTagBar && buttons[i].arg.i == 0 ? &arg : &buttons[i].arg);

	return 0;
}

void checkotherwm(void) {
	/* this should cause an error if some other window manager is running */
	uint32_t values[] = { XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT };
	xcb_void_cookie_t wm_cookie = xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK, values);
	err = xcb_request_check(conn, wm_cookie);
	if(err)
	{
		fprintf(stderr, "error code: %i\n", err->error_code);
		free(err);
		die("dwm: another window manager is already running\n");
	}
}

void cleanup(void) {
	Arg a = {.ui = ~0};
	Layout foo = { "", NULL };
	Monitor *m;

	view(&a);
	selmon->lt[selmon->sellt] = &foo;
	for(m = mons; m; m = m->next)
		while(m->stack)
			unmanage(m->stack, false);
	xcb_close_font(conn, dc.font.xfont);

	xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	xcb_free_cursor(conn, cursor[CurNormal]);
	xcb_free_cursor(conn, cursor[CurResize]);
	xcb_free_cursor(conn, cursor[CurMove]);

	xcb_free_colors(conn, xscreen->default_colormap, 0, ColLast, dc.norm);
	xcb_free_colors(conn, xscreen->default_colormap, 0, ColLast, dc.sel);

	while(mons)
		cleanupmon(mons);
	xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT, 
		XCB_CURRENT_TIME);
	xcb_flush(conn);
}

void cleanupmon(Monitor *mon) {
	Monitor *m;

	if(mon == mons)
		mons = mons->next;
	else {
		for(m = mons; m && m->next != mon; m = m->next);
		m->next = mon->next;
	}

	xcb_unmap_window(conn, mon->barwin);
	xcb_destroy_window(conn, mon->barwin);
	free(mon);
}

void clearurgent(Client *c) {
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t wmh_cookie;

	wmh_cookie = xcb_icccm_get_wm_hints_unchecked(conn, c->win);
	c->isurgent = false;
	if(!xcb_icccm_get_wm_hints_reply(conn, wmh_cookie, &wmh, NULL))
		return;
	wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
	xcb_icccm_set_wm_hints(conn, c->win, &wmh);
}

void configure(Client *c) {
	xcb_configure_notify_event_t config_event;
	config_event.response_type = XCB_CONFIGURE_NOTIFY;
	config_event.event = c->win;
	config_event.window = c->win;
	config_event.x = c->x;
	config_event.y = c->y;
	config_event.width = c->w;
	config_event.height = c->h;
	config_event.border_width = c->bw;
	config_event.above_sibling = XCB_NONE;
	config_event.override_redirect = false;
	xcb_send_event(conn, false, c->win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char*)&config_event);
}

int configurenotify(xcb_generic_event_t *e) {
	Monitor *m;
	xcb_configure_notify_event_t *ev = (xcb_configure_notify_event_t*)e;

	if(ev->window == root) {
		sw = ev->width;
		sh = ev->height;
		if(updategeom()) {
			updatebars();
			for(m = mons; m; m = m->next)
			{
				uint32_t values[] = { m->wx, m->by, m->ww, bh  };
				xcb_configure_window(conn, m->barwin, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | 
					XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
			}
			arrange(NULL);
		}
		xcb_flush(conn);
	}

	return 0;
}

int configurerequest(xcb_generic_event_t *e) {
	Client *c;
	Monitor *m;
	xcb_configure_request_event_t *ev = (xcb_configure_request_event_t*)e;

	if((c = wintoclient(ev->window))) {
		if(ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
			c->bw = ev->border_width;
		else if(c->isfloating || !selmon->lt[selmon->sellt]->arrange) {
			m = c->mon;
			if(ev->value_mask & XCB_CONFIG_WINDOW_X)
				c->x = m->mx + ev->x;
			if(ev->value_mask & XCB_CONFIG_WINDOW_Y)
				c->y = m->my + ev->y;
			if(ev->value_mask & XCB_CONFIG_WINDOW_WIDTH)
				c->w = ev->width;
			if(ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
				c->h = ev->height;
			if((c->x + c->w) > m->mx + m->mw && c->isfloating)
				c->x = m->mx + (m->mw / 2 - c->w / 2); /* center in x direction */
			if((c->y + c->h) > m->my + m->mh && c->isfloating)
				c->y = m->my + (m->mh / 2 - c->h / 2); /* center in y direction */
			if((ev->value_mask & (XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y)) && 
				!(ev->value_mask & (XCB_CONFIG_WINDOW_WIDTH|
					XCB_CONFIG_WINDOW_HEIGHT)))
				configure(c);
			if(ISVISIBLE(c))
			{
				uint32_t values[] = { c->x, c->y, c->w, c->h };
				testcookie(xcb_configure_window_checked(conn, c->win, XCB_CONFIG_WINDOW_X |
					XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
					XCB_CONFIG_WINDOW_HEIGHT, values));
			}
		}
		else
			configure(c);
	}
	else {
		// long, but fairly robust
		uint32_t mask = 0x00000000;
		xcb_params_configure_window_t params;
		if(ev->value_mask & XCB_CONFIG_WINDOW_X)
			XCB_AUX_ADD_PARAM(&mask, &params, x, ev->x);
		if(ev->value_mask & XCB_CONFIG_WINDOW_Y)
			XCB_AUX_ADD_PARAM(&mask, &params, y, ev->y);
		if(ev->value_mask & XCB_CONFIG_WINDOW_WIDTH)
			XCB_AUX_ADD_PARAM(&mask, &params, width, ev->width);
		if(ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
			XCB_AUX_ADD_PARAM(&mask, &params, height, ev->height);
		if(ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
			XCB_AUX_ADD_PARAM(&mask, &params, border_width, ev->border_width);
		if(ev->value_mask & XCB_CONFIG_WINDOW_SIBLING)
			XCB_AUX_ADD_PARAM(&mask, &params, sibling, ev->sibling);
		if(ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
			XCB_AUX_ADD_PARAM(&mask, &params, stack_mode, ev->stack_mode);
		xcb_aux_configure_window(conn, ev->window, mask, &params);
	}
	xcb_flush(conn);

	return 0;
}

Monitor* createmon(void) {
	Monitor *m;

	m = (Monitor*)malloc_safe(sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	return m;
}

int destroynotify(xcb_generic_event_t *e) {
	Client *c;
	xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)e;
	printf("destroynotify\n");

	if((c = wintoclient(ev->window)))
		unmanage(c, true);

	return 0;
}

void detach(Client *c) {
	Client **tc;

	for(tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void detachstack(Client *c) {
	Client **tc, *t;

	for(tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if(c == c->mon->sel) {
		for(t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

void die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

Monitor* dirtomon(int dir) {
	Monitor *m = NULL;

	if(dir > 0) {
		if(!(m = selmon->next))
			m = mons;
	}
	else {
		if(selmon == mons)
			for(m = mons; m->next; m = m->next);
		else
			for(m = mons; m->next != selmon; m = m->next);
	}
	return m;
}

void drawbar(Monitor *m) {
	int x;
	unsigned int i, occ = 0, urg = 0;
	uint32_t *col;
	Client *c;

	for(c = m->clients; c; c = c->next) {
		occ |= c->tags;
		if(c->isurgent)
			urg |= c->tags;
	}
	dc.x = 0;
	for(i = 0; i < LENGTH(tags); i++) {
		dc.w = TEXTW(tags[i]);
		col = m->tagset[m->seltags] & 1 << i ? dc.sel : dc.norm;
		drawtext(tags[i], col, urg & 1 << i, m->barwin);
		if((m == selmon && selmon->sel && selmon->sel->tags & 1 << i) || occ & 1 << i)
			drawsquare(m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
		        	   occ & 1 << i, urg & 1 << i, col, m->barwin);
		dc.x += dc.w;
	}
	dc.w = blw = TEXTW(m->ltsymbol);
	drawtext(m->ltsymbol, dc.norm, false, m->barwin);
	dc.x += dc.w;
	x = dc.x;
	if(m == selmon) { /* status is only drawn on selected monitor */
		dc.w = TEXTW(stext);
		dc.x = m->ww - dc.w;
		if(dc.x < x) {
			dc.x = x;
			dc.w = m->ww - x;
		}
		drawtext(stext, dc.norm, false, m->barwin);
	}
	else
		dc.x = m->ww;
	if((dc.w = dc.x - x) > bh) {
		dc.x = x;
		if(m->sel) {
			col = m == selmon ? dc.sel : dc.norm;
			drawtext(m->sel->name, col, false, m->barwin);
			drawsquare(m->sel->isfixed, m->sel->isfloating, false, col, m->barwin);
		}
		else
			drawtext(NULL, dc.norm, false, m->barwin);
	}
	xcb_flush(conn);
}

void drawbars(void) {
	Monitor *m;

	for(m = mons; m; m = m->next)
		drawbar(m);
}

void drawsquare(bool filled, bool empty, bool invert, uint32_t col[ColLast], xcb_window_t w) {
	int x;
	xcb_rectangle_t r = { dc.x, dc.y, dc.w, dc.h };

	uint32_t values[] = { col[invert ? ColBG : ColFG], col[invert ? ColFG : ColBG] };
	xcb_change_gc(conn, dc.gc, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, values);

	x = (dc.font.ascent + dc.font.descent + 2) / 4;
	r.x = dc.x + 1;
	r.y = dc.y + 1;

	if(filled) {
		r.width = r.height = x + 1;
		xcb_poly_fill_rectangle(conn, w, dc.gc, 1, &r);
	}
	else if(empty) {
		r.width = r.height = x;
		xcb_poly_rectangle(conn, w, dc.gc, 1, &r);
	}
}

void drawtext(const char *text, uint32_t col[ColLast], bool invert, xcb_window_t w) {
	char buf[256];
	int i, x, y, h, len, olen;
	xcb_rectangle_t r = { dc.x, dc.y, dc.w, dc.h };

	xcb_change_gc(conn, dc.gc, XCB_GC_FOREGROUND, (uint32_t*)&col[invert ? ColFG : ColBG]);
	xcb_poly_fill_rectangle(conn, w, dc.gc, 1, &r);
	if(!text)
		return;
	olen = strlen(text);
	h = dc.font.ascent + dc.font.descent;
	y = dc.y + (dc.h / 2) - (h / 2) + dc.font.ascent;
	x = dc.x + (h / 2);
	/* shorten text if necessary */
	for(len = MIN(olen, sizeof buf); len && textnw(text, len) > dc.w - h; len--);
	if(!len)
		return;
	memcpy(buf, text, len);
	if(len < olen)
		for(i = len; i && i > len - 3; buf[--i] = '.');
	uint32_t values[] = { col[invert ? ColBG : ColFG], col[invert ? ColFG : ColBG] };
	xcb_change_gc(conn, dc.gc, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, values);
	xcb_image_text_8(conn, len, w, dc.gc, x, y, buf);
}

int enternotify(xcb_generic_event_t *e) {
	//Client *c;
	Monitor *m;
	xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t*)e;

	if((ev->mode != XCB_NOTIFY_MODE_NORMAL || ev->detail == XCB_NOTIFY_DETAIL_INFERIOR) && ev->event != root)
		return 0;
	if((m = wintomon(ev->event)) && m != selmon) {
		unfocus(selmon->sel, true);
		selmon = m;
	}

	focus(wintoclient(ev->event));

	return 0;
}

int expose(xcb_generic_event_t *e) {
	Monitor *m;
	xcb_expose_event_t *ev = (xcb_expose_event_t*)e;

	if(ev->count == 0 && (m = wintomon(ev->window)))
		drawbar(m);

	return 0;
}

void focus(Client *c) {
	if(!c || !ISVISIBLE(c))
		for(c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	if(selmon->sel)
		unfocus(selmon->sel, false);
	if(c) {
		if(c->mon != selmon)
			selmon = c->mon;
		if(c->isurgent)
			clearurgent(c);
		detachstack(c);
		attachstack(c);
		grabbuttons(c, true);
		xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, (uint32_t*)&dc.sel[ColBorder]);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
	}
	else
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
	selmon->sel = c;
	drawbars();
}

int focusin(xcb_generic_event_t *e) { /* there are some broken focus acquiring clients */
	xcb_focus_in_event_t *ev = (xcb_focus_in_event_t*)e;

	if(selmon->sel && ev->event != selmon->sel->win)
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, selmon->sel->win, 
			XCB_CURRENT_TIME);

	return 0;
}

void focusmon(const Arg *arg) {
	Monitor *m = NULL;

	if(!mons->next)
		return;
	if((m = dirtomon(arg->i)) == selmon)
		return;
	unfocus(selmon->sel, true);
	selmon = m;
	focus(NULL);
}

void focusstack(const Arg *arg) {
	Client *c = NULL, *i;

	if(!selmon->sel)
		return;
	if(arg->i > 0) {
		for(c = selmon->sel->next; c && !ISVISIBLE(c); c = c->next);
		if(!c)
			for(c = selmon->clients; c && !ISVISIBLE(c); c = c->next);
	}
	else {
		for(i = selmon->clients; i != selmon->sel; i = i->next)
			if(ISVISIBLE(i))
				c = i;
		if(!c)
			for(; i; i = i->next)
				if(ISVISIBLE(i))
					c = i;
	}
	if(c) {
		focus(c);
		restack(selmon);
	}
}

uint32_t getcolor(const char *colstr) 
{
	uint32_t pixel;
	uint16_t red, green, blue;
	xcb_colormap_t cmap = xscreen->default_colormap;
	char colcopy[strlen(colstr)+1];

	strcpy(colcopy, colstr);

	if(xcb_aux_parse_color(colcopy, &red, &green, &blue))
	{
		xcb_alloc_color_reply_t *reply;

		if((reply = xcb_alloc_color_reply(conn, 
			xcb_alloc_color(conn, cmap, red, green, blue),
			&err)))
		{
			pixel = reply->pixel;
			free(reply);
		}
		else
		{
			testerr();
			pixel = xscreen->black_pixel;
		}
	}
	else
	{
		xcb_alloc_named_color_reply_t *reply;

		if((reply = xcb_alloc_named_color_reply(conn,
			xcb_alloc_named_color(conn, cmap, strlen(colstr), colstr), &err)))
		{
			pixel = reply->pixel;
			free(reply);
		}
		else
		{
			testerr();
			pixel = xscreen->black_pixel;
		}
	}

	return pixel;
}

bool getrootptr(int *x, int *y) {
	xcb_query_pointer_reply_t *reply = xcb_query_pointer_reply(conn, xcb_query_pointer(conn, root), &err);
	testerr();

	*x = reply->root_x;
	*y = reply->root_y;

	free(reply);

	return true;
}

xcb_atom_t getstate(xcb_window_t w) {
	xcb_atom_t result = -1;

	xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, 
		w, wmatom[WMState], XCB_ATOM_ATOM, 0, 0);
	xcb_get_property_reply_t *reply = xcb_get_property_reply(conn, cookie, &err);
	testerr();

	if(!reply) 
		return -1;
	
	if(!xcb_get_property_value_length(reply))
	{
		free(reply);
		return -1;
	}

	result = *(xcb_atom_t*)xcb_get_property_value(reply);
	free(reply);
	return result;
}

bool gettextprop(xcb_window_t w, xcb_atom_t atom, char *text, unsigned int size) {
	xcb_icccm_get_text_property_reply_t reply;
	if(!xcb_icccm_get_text_property_reply(conn, xcb_icccm_get_text_property(conn, w, atom), &reply, &err))
	{
		return false;
	}

	if(err)
	{
		testerr();
		return false;
	}

	// TODO: encoding
	if(!reply.name || !reply.name_len)
		return false;

	strncpy(text, reply.name, MIN(reply.name_len+1, size));
	text[MIN(reply.name_len + 1, size)-1] = '\0';
	xcb_icccm_get_text_property_reply_wipe(&reply);
	
	return true;
}

void grabbuttons(Client *c, bool focused) {
	unsigned int i, j;
	unsigned int modifiers[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask|XCB_MOD_MASK_LOCK };

	updatenumlockmask();
	xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, c->win, XCB_GRAB_ANY);
	if(focused) {
		for(i = 0; i < LENGTH(buttons); i++)
			if(buttons[i].click == ClkClientWin)
				for(j = 0; j < LENGTH(modifiers); j++)
					xcb_grab_button(conn, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
						XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
						buttons[i].button, buttons[i].mask | modifiers[j]);
	}
	else
		xcb_grab_button(conn, false, c->win, BUTTONMASK, XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_SYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
			XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
}

void grabkeys(void)
{
	updatenumlockmask();

	unsigned int i, j;
	unsigned int modifiers[] = { 0, XCB_MOD_MASK_LOCK, numlockmask, numlockmask | XCB_MOD_MASK_LOCK };
	xcb_keycode_t *code;

	xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	for(i = 0; i < LENGTH(keys); i++)
	{
		code = xcb_key_symbols_get_keycode(syms, keys[i].keysym);
		if(code)
			for(j = 0; j < LENGTH(modifiers); j++)
				xcb_grab_key(conn, true, root, keys[i].mod | modifiers[j],
					*code, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
		free(code);
	}
}

void initfont(const char *fontstr) {
	dc.font.xfont = xcb_generate_id(conn);
	testcookie(xcb_open_font_checked(conn, dc.font.xfont, strlen(fontstr), fontstr));
	
	xcb_query_font_reply_t *fontreply = xcb_query_font_reply(conn, xcb_query_font(conn, dc.font.xfont), &err);
	testerr();

	dc.font.ascent = fontreply->font_ascent;
	dc.font.descent = fontreply->font_descent;
	dc.font.height = dc.font.ascent + dc.font.descent;

	free(fontreply);
}

bool isprotodel(Client *c) {
	int i;
	bool ret = false;
	xcb_icccm_get_wm_protocols_reply_t proto_reply;

	if(xcb_icccm_get_wm_protocols_reply(conn, xcb_icccm_get_wm_protocols_unchecked(conn, c->win, wmatom[WMProtocols]), &proto_reply, NULL)) {
		for(i = 0; !ret && i < proto_reply.atoms_len; i++)
			if(proto_reply.atoms[i] == wmatom[WMDelete])
				ret = true;
		xcb_icccm_get_wm_protocols_reply_wipe(&proto_reply);
	}
	return ret;
}

#ifdef XINERAMA
static bool isuniquegeom(XineramaScreenInfo *unique, size_t len, XineramaScreenInfo *info) {
	unsigned int i;

	for(i = 0; i < len; i++)
		if(unique[i].x_org == info->x_org && unique[i].y_org == info->y_org
		&& unique[i].width == info->width && unique[i].height == info->height)
			return false;
	return true;
}
#endif /* XINERAMA */

int keypress(xcb_generic_event_t *e) {
	unsigned int i;
	xcb_keysym_t keysym;
	xcb_key_press_event_t *ev = (xcb_key_press_event_t*)e;

	keysym = xcb_key_press_lookup_keysym(syms, ev, 0);
	for(i = 0; i < LENGTH(keys); i++)
	{
		if(keysym == keys[i].keysym
		&& CLEANMASK(keys[i].mod) == CLEANMASK(ev->state)
		&& keys[i].func)
		{
			keys[i].func(&(keys[i].arg));
		}
	}

	return 0;
}

void killclient(const Arg *arg) {
	if(!selmon->sel)
		return;
	if(isprotodel(selmon->sel)) {
		xcb_client_message_event_t ev;
		ev.response_type = XCB_CLIENT_MESSAGE;
		ev.window = selmon->sel->win;
		ev.format = 32;
		ev.data.data32[0] = wmatom[WMDelete];
		ev.data.data32[1] = XCB_TIME_CURRENT_TIME;
		ev.type = wmatom[WMProtocols];
		testcookie(xcb_send_event_checked(conn, false, selmon->sel->win, 
			XCB_EVENT_MASK_NO_EVENT, (const char*)&ev));
	} else {
		xcb_grab_server(conn);
		xcb_set_close_down_mode(conn, XCB_CLOSE_DOWN_DESTROY_ALL);
		xcb_kill_client(conn, selmon->sel->win);
		xcb_ungrab_server(conn);
		xcb_flush(conn);
	}
}

void manage(xcb_window_t w)
{
	Client *c, *t = NULL;
	xcb_window_t trans = XCB_WINDOW_NONE;

	c = (Client*)malloc_safe(sizeof(Client));
	c->win = w;
	updatetitle(c);

	xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(conn, w);
	
	xcb_window_t trans_reply = XCB_NONE;
	xcb_icccm_get_wm_transient_for_reply(conn, xcb_icccm_get_wm_transient_for(conn, w), &trans_reply, &err);
	testerr();

	if(trans_reply != XCB_NONE)
		t = wintoclient(trans_reply);
	if(t) {
		c->mon = t->mon;
		c->tags = t->tags;
	}
	else {
		c->mon = selmon;
		applyrules(c);
	}

	/* geometry */
	xcb_get_geometry_reply_t *geom_reply = xcb_get_geometry_reply(conn, geom_cookie, &err);
	testerr();
	c->x = c->oldx = geom_reply->x + c->mon->wx;
	c->y = c->oldy = geom_reply->y + c->mon->wy;
	c->w = c->oldw = geom_reply->width;
	c->h = c->oldh = geom_reply->height;
	c->oldbw = geom_reply->border_width;
	if(c->w == c->mon->mw && c->h == c->mon->mh) {
		c->isfloating = 1;
		c->x = c->mon->mx;
		c->y = c->mon->my;
		c->bw = 0;
	}
	else {
		if(c->x + WIDTH(c) > c->mon->mx + c->mon->mw)
			c->x = c->mon->mx + c->mon->mw - WIDTH(c);
		if(c->y + HEIGHT(c) > c->mon->my + c->mon->mh)
			c->y = c->mon->my + c->mon->mh - HEIGHT(c);
		c->x = MAX(c->x, c->mon->mx);
		/* only fix client y-offset, if the client center might cover the bar */
		c->y = MAX(c->y, ((c->mon->by == 0) && (c->x + (c->w / 2) >= c->mon->wx)
		           && (c->x + (c->w / 2) < c->mon->wx + c->mon->ww)) ? bh : c->mon->my);
		c->bw = borderpx;
	}
	
	uint32_t cw_values[] = { dc.norm[ColBorder],
		XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE |
		XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY };
	xcb_change_window_attributes(conn, w, XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK, cw_values);
	configure(c); /* propagates border_width, if size doesn't change */
	updatesizehints(c);
	grabbuttons(c, false);
	if(!c->isfloating)
		c->isfloating = c->oldstate = trans != XCB_WINDOW_NONE || c->isfixed;
	attach(c);
	attachstack(c);
	uint32_t config_values[] = { c->x + 2 * sw, c->y, c->w, c->h, c->bw, XCB_STACK_MODE_ABOVE };
	xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
			     XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
			     XCB_CONFIG_WINDOW_BORDER_WIDTH | 
			     (c->isfloating) ? XCB_CONFIG_WINDOW_STACK_MODE : 0, config_values);
	xcb_map_window(conn, c->win);
	setclientstate(c, XCB_ICCCM_WM_STATE_NORMAL);
	arrange(c->mon);
}

int mappingnotify(xcb_generic_event_t *e) {
	xcb_mapping_notify_event_t *ev = (xcb_mapping_notify_event_t*)e;

	xcb_refresh_keyboard_mapping(syms, ev);
	if(ev->request == XCB_MAPPING_NOTIFY)
		grabkeys();

	return 0;
}

int maprequest(xcb_generic_event_t *e) {
	xcb_map_request_event_t *ev = (xcb_map_request_event_t*)e;

	xcb_get_window_attributes_reply_t *ga_reply =
		xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, ev->window), &err);
	testerr();

	printf("map %i\n", ev->window);

	if(!ga_reply)
		return 0;
	if(ga_reply->override_redirect)
		return 0;
	if(!wintoclient(ev->window))
		manage(ev->window);
	free(ga_reply);

	return 0;
}

void monocle(Monitor *m) {
	unsigned int n = 0;
	Client *c;

	for(c = m->clients; c; c = c->next)
		if(ISVISIBLE(c))
			n++;
	if(n > 0) /* override layout symbol */
		snprintf(m->ltsymbol, sizeof m->ltsymbol, "[%d]", n);
	for(c = nexttiled(m->clients); c; c = nexttiled(c->next))
		resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, false);
}

void movemouse(const Arg *arg) {
	int x, y, ocx, ocy, nx, ny;
	Client *c;
	Monitor *m;
	xcb_generic_event_t *ev;
	bool done = false;

	if(!(c = selmon->sel))
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;

	free(xcb_grab_pointer_reply(conn, xcb_grab_pointer(conn, false, root, MOUSEMASK,
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE,
		cursor[CurMove], XCB_TIME_CURRENT_TIME), &err));
	if(err)
		return;
	if(!getrootptr(&x, &y))
		return;

	do
	{
		ev = xcb_wait_for_event(conn);

		if(ev->response_type == XCB_MOTION_NOTIFY)
		{
			xcb_motion_notify_event_t* e = (xcb_motion_notify_event_t*)ev;
			clearevent(XCB_MOTION_NOTIFY);		/* clear out the extras */
			nx = ocx + (e->event_x - x);
			ny = ocy + (e->event_y - y);
			if(snap && nx >= selmon->wx && nx <= selmon->wx + selmon->ww
			   && ny >= selmon->wy && ny <= selmon->wy + selmon->wh) {
				if(abs(selmon->wx - nx) < snap)
					nx = selmon->wx;
				else if(abs((selmon->wx + selmon->ww) - (nx + WIDTH(c))) < snap)
					nx = selmon->wx + selmon->ww - WIDTH(c);
				if(abs(selmon->wy - ny) < snap)
					ny = selmon->wy;
				else if(abs((selmon->wy + selmon->wh) - (ny + HEIGHT(c))) < snap)
					ny = selmon->wy + selmon->wh - HEIGHT(c);
				if(!c->isfloating && selmon->lt[selmon->sellt]->arrange
				   && (abs(nx - c->x) > snap || abs(ny - c->y) > snap))
					togglefloating(NULL);
			}

			if(!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, nx, ny, c->w, c->h, true);
		}
		else if(ev->response_type == XCB_BUTTON_RELEASE)
		{
			done = true;
		}
		else
		{
			//xcb_event_handle(handlers, ev);
			// FIXME: do something
		}
		free(ev);
	} while(!done);
	xcb_ungrab_pointer(conn, XCB_TIME_CURRENT_TIME);
	if((m = ptrtomon(c->x + c->w / 2, c->y + c->h / 2)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

Client* nexttiled(Client *c) {
	for(; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

Monitor* ptrtomon(int x, int y) {
	Monitor *m;

	for(m = mons; m; m = m->next)
		if(INRECT(x, y, m->wx, m->wy, m->ww, m->wh))
			return m;
	return selmon;
}

int propertynotify(xcb_generic_event_t *e) {
	Client *c;
	xcb_property_notify_event_t *ev = (xcb_property_notify_event_t*)e;

	if((ev->window == root) && (ev->atom == XCB_ATOM_WM_NAME))
		updatestatus();
	else if(ev->state == XCB_PROPERTY_DELETE)
		return 0; 	// ignore
	else if((c = wintoclient(ev->window))) {
		if(ev->atom == XCB_ATOM_WM_TRANSIENT_FOR)
		{
			// get WM_TRANSIENT_FOR
			xcb_window_t trans = XCB_NONE;
			xcb_get_property_cookie_t cookie = xcb_icccm_get_wm_transient_for(conn, c->win);
			xcb_get_property_reply_t* reply = xcb_get_property_reply(conn, cookie, &err);
			testerr();
			xcb_icccm_get_wm_transient_for_from_reply(&trans, reply);

			if(trans != XCB_NONE && !c->isfloating && (c->isfloating = (wintoclient(trans) != NULL)))
				arrange(c->mon);
		}
		else if(ev->atom == XCB_ATOM_WM_NORMAL_HINTS)
		{
			updatesizehints(c);
		}
		else if(ev->atom == XCB_ATOM_WM_HINTS)
		{
			updatewmhints(c);
			drawbars();
		}
		else if(ev->atom == XCB_ATOM_WM_NAME || ev->atom == netatom[NetWMName]) {
			updatetitle(c);
			if(c == c->mon->sel)
				drawbar(c->mon);
		}
	}

	return 0;
}

int clientmessage(xcb_generic_event_t *e) {
	xcb_client_message_event_t *cme = (xcb_client_message_event_t*)e;
	Client *c;

	if((c = wintoclient(cme->window)) && 
		(cme->type == netatom[NetWMState] && 
		 cme->data.data32[1] == netatom[NetWMFullscreen]))
	{
		if(cme->data.data32[0]) {
			xcb_change_property(conn, XCB_PROP_MODE_REPLACE, cme->window, 
				netatom[NetWMState], XCB_ATOM, 32, 1, 
				(unsigned char*)&netatom[NetWMFullscreen]);
			c->oldstate = c->isfloating;
			c->oldbw = c->bw;
			c->bw = 0;
			c->isfloating = 1;
			resizeclient(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
			uint32_t values[] = { XCB_STACK_MODE_ABOVE };
			xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE, values);
		}
		else {
			xcb_change_property(conn, XCB_PROP_MODE_REPLACE, cme->window,
				netatom[NetWMState], XCB_ATOM, 32, 0, (unsigned char*)0);
			c->isfloating = c->oldstate;
			c->bw = c->oldbw;
			c->x = c->oldx;
			c->y = c->oldy;
			c->w = c->oldw;
			c->h = c->oldh;
			resizeclient(c, c->x, c->y, c->w, c->h);
			arrange(c->mon);
		}
	}

	return 0;
}

void quit(const Arg *arg) {
	cleanup();
	xcb_disconnect(conn);
	exit(0);
}

void resize(Client *c, int x, int y, int w, int h, bool interact) {
	if(applysizehints(c, &x, &y, &w, &h, interact))
		resizeclient(c, x, y, w, h);
}

void resizeclient(Client *c, int x, int y, int w, int h) {
	uint32_t values[] = { x, y, w, h, c->bw };
	xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | 
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
		values);
	c->oldx = c->x; c->x = x;
	c->oldy = c->y; c->y = y;
	c->oldw = c->w; c->w = w;
	c->oldh = c->h; c->h = h;
	configure(c);
	xcb_flush(conn);
}

void resizemouse(const Arg *arg) {
	int ocx, ocy;
	int nw, nh;
	Client *c;
	Monitor *m;
	xcb_generic_event_t *ev;
	bool done = false;

	if(!(c = selmon->sel))
		return;
	restack(selmon);
	ocx = c->x;
	ocy = c->y;

	free(xcb_grab_pointer_reply(conn, xcb_grab_pointer(conn, false, root, MOUSEMASK, 
		XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE,
		cursor[CurResize], XCB_TIME_CURRENT_TIME), &err));
	if(err)
		return;

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);

	do
	{
		ev = xcb_wait_for_event(conn);

		if(ev->response_type == XCB_MOTION_NOTIFY)
		{
			xcb_motion_notify_event_t* m_ev = (xcb_motion_notify_event_t*)ev;
			clearevent(XCB_MOTION_NOTIFY);		/* clear out the extras */
			nw = MAX(m_ev->event_x - ocx - 2 * c->bw + 1, 1);
			nh = MAX(m_ev->event_y - ocy - 2 * c->bw + 1, 1);
			if(snap && nw >= selmon->wx && nw <= selmon->wx + selmon->ww
				&& nh >= selmon->wy && nh <= selmon->wy + selmon->wh)
			{
				if(!c->isfloating && selmon->lt[selmon->sellt]->arrange
				&& (abs(nw - c->w) > snap || abs(nh - c->h) > snap))
					togglefloating(NULL);
			}

			if(!selmon->lt[selmon->sellt]->arrange || c->isfloating)
				resize(c, c->x, c->y, nw, nh, true);
		}
		else if(ev->response_type == XCB_BUTTON_RELEASE)
		{
			done = true;
		}
		else
		{
			//xcb_event_handle(handlers, ev);
			// FIXME: do something here
		}
		free(ev);
	} while(!done);

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	xcb_ungrab_pointer(conn, XCB_TIME_CURRENT_TIME);

	clearevent(XCB_ENTER_NOTIFY);

	if((m = ptrtomon(c->x + c->w / 2, c->y + c->h / 2)) != selmon) {
		sendmon(c, m);
		selmon = m;
		focus(NULL);
	}
}

void restack(Monitor *m) {
	Client *c;

	drawbar(m);
	if(!m->sel)
		return;
	if(m->sel->isfloating || !m->lt[m->sellt]->arrange)
	{
		uint32_t values[] = { XCB_STACK_MODE_ABOVE };
		xcb_configure_window(conn, m->sel->win, XCB_CONFIG_WINDOW_STACK_MODE, values);
	}
	if(m->lt[m->sellt]->arrange) {
		uint32_t values[] = { m->barwin, XCB_STACK_MODE_BELOW };
		for(c = m->stack; c; c = c->snext)
			if(!c->isfloating && ISVISIBLE(c)) {
				xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_SIBLING |
					XCB_CONFIG_WINDOW_STACK_MODE, values);
				values[0] = c->win;
			}
	}
	xcb_flush(conn);

	clearevent(XCB_ENTER_NOTIFY);
}

void scan(void) {
	unsigned int i, num;
	xcb_window_t *wins = NULL;

	xcb_query_tree_reply_t *query_reply = xcb_query_tree_reply(conn, xcb_query_tree(conn, root), &err);
	num = query_reply->children_len;
	wins = xcb_query_tree_children(query_reply);

	for(i = 0; i < num; i++) {
		xcb_get_window_attributes_reply_t *ga_reply =
			xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, wins[i]), &err);
		testerr();

		if(ga_reply->override_redirect) 
			continue;

		xcb_window_t trans_reply = XCB_NONE;
		xcb_icccm_get_wm_transient_for_reply(conn, xcb_icccm_get_wm_transient_for(conn, wins[i]), &trans_reply, &err);
		testerr();
		
		if(trans_reply != XCB_NONE)
			continue;

		if(ga_reply->map_state == XCB_MAP_STATE_VIEWABLE ||
			getstate(wins[i]) == XCB_ICCCM_WM_STATE_ICONIC)
			manage(wins[i]);

		free(ga_reply);
	}
	for(i = 0; i < num; i++) { /* now the transients */
		xcb_get_window_attributes_reply_t *ga_reply =
			xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, wins[i]), &err);
		testerr();

		xcb_window_t trans_reply = XCB_NONE;
		xcb_icccm_get_wm_transient_for_reply(conn, xcb_icccm_get_wm_transient_for(conn, wins[i]), &trans_reply, &err);
		testerr();

		if(trans_reply != XCB_NONE && (ga_reply->map_state == XCB_MAP_STATE_VIEWABLE
		   || getstate(wins[i]) == XCB_ICCCM_WM_STATE_ICONIC))
			manage(wins[i]);

		free(ga_reply);
	}
	if(query_reply)
		free(query_reply);	// this frees the whole thing, including wins
}

void sendmon(Client *c, Monitor *m) {
	if(c->mon == m)
		return;
	unfocus(c, true);
	detach(c);
	detachstack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	attach(c);
	attachstack(c);
	focus(NULL);
	arrange(NULL);
}

void setclientstate(Client *c, long state) {
	long data[] = { state, XCB_ATOM_NONE };

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win, wmatom[WMState], 
		wmatom[WMState], 32, 2, (unsigned char*)data);
}

void setlayout(const Arg *arg) {
	if(!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if(arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);
	if(selmon->sel)
		arrange(selmon);
	else
		drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutly */
void setmfact(const Arg *arg) {
	float f;

	if(!arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0 ? arg->f + selmon->mfact : arg->f - 1.0;
	if(f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}


#define SETUP_NUM_ATOMS (WMLast + NetLast)

static const struct
{
	const char* name;
	int number;
	bool isnet;
} setup_atoms[SETUP_NUM_ATOMS] = 
{
	{ "WM_PROTOCOLS",  WMProtocols, false },
	{ "WM_DELETE_WINDOW", WMDelete, false },
	{ "WM_STATE", WMState, false }, 
	{ "_NET_SUPPORTED", NetSupported, true },
	{ "_NET_WM_NAME", NetWMName, true },
	{ "_NET_WM_STATE", NetWMState, true },
	{ "_NET_WM_STATE_FULLSCREEN", NetWMFullscreen, true }
};

void setup(void)
{
	/* clean up any zombies immediately */
	sigchld(0);

	initfont(font);

	sw = xscreen->width_in_pixels;
	sh = xscreen->height_in_pixels;
	bh = dc.h = dc.font.height + 2;
	updategeom();

	/* init atoms */
	xcb_intern_atom_cookie_t atom_cookie[SETUP_NUM_ATOMS];
	for(int i = 0; i < SETUP_NUM_ATOMS; i++)
	{
		atom_cookie[i] = xcb_intern_atom(conn, 0, 
			strlen(setup_atoms[i].name), setup_atoms[i].name);
	}

	for(int i = 0; i < SETUP_NUM_ATOMS; i++)
	{
		xcb_intern_atom_reply_t *reply;

		if((reply = xcb_intern_atom_reply(conn, atom_cookie[i], &err)))
		{
			if(setup_atoms[i].isnet)
				netatom[setup_atoms[i].number] = reply->atom;
			else
				wmatom[setup_atoms[i].number] = reply->atom;
			free(reply);
		}
		else
			testerr();
	}

	/* init cursors */
	xcb_font_t cursor_font = xcb_generate_id(conn);
	xcb_open_font(conn, cursor_font, strlen("cursor"), "cursor");
#define CURSOR(id, XC) cursor[id] = xcb_generate_id(conn); \
	xcb_create_glyph_cursor(conn, cursor[id], cursor_font, cursor_font, XC, \
		XC+1, 0, 0, 0, 65535, 63353, 63353);
	CURSOR(CurNormal, XC_left_ptr);
	CURSOR(CurResize, XC_sizing);
	CURSOR(CurMove, XC_fleur);
#undef CURSOR
	xcb_close_font(conn, cursor_font);
	
	/* init appearance */
	dc.norm[ColBorder] = getcolor(normbordercolor);
	dc.norm[ColBG] = getcolor(normbgcolor);
	dc.norm[ColFG] = getcolor(normfgcolor);
	dc.sel[ColBorder] = getcolor(selbordercolor);
	dc.sel[ColBG] = getcolor(selbgcolor);
	dc.sel[ColFG] = getcolor(selfgcolor);
	
	dc.gc = xcb_generate_id(conn);
	uint32_t values[] = { 1, XCB_LINE_STYLE_SOLID, XCB_CAP_STYLE_BUTT, XCB_JOIN_STYLE_MITER, dc.font.xfont };
	xcb_create_gc(conn, dc.gc, root, XCB_GC_LINE_WIDTH | XCB_GC_LINE_STYLE | XCB_GC_CAP_STYLE | 
		XCB_GC_JOIN_STYLE | dc.font.set ? 0 : XCB_GC_FONT, values); 
	dc.font.set = true;

	/* init bars */
	updatebars();
	updatestatus();

	/* EWMH support per view */
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, 
		netatom[NetSupported], XCB_ATOM, 32, NetLast, (unsigned char*)netatom);

	/* select for events */
	uint32_t cw_values[] = 
	{ 
		XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | 
		XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | 
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_ENTER_WINDOW |
		XCB_EVENT_MASK_LEAVE_WINDOW |
		XCB_EVENT_MASK_STRUCTURE_NOTIFY |
		XCB_EVENT_MASK_PROPERTY_CHANGE,
		cursor[CurNormal]
	};
	testcookie(xcb_change_window_attributes_checked(conn, root, XCB_CW_EVENT_MASK | XCB_CW_CURSOR, cw_values));

	syms = xcb_key_symbols_alloc(conn);
	grabkeys();
}

void showhide(Client *c) {
	if(!c)
		return;
	if(ISVISIBLE(c)) { /* show clients top down */
		uint32_t values[] = { c->x, c->y };
		xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
		if(!c->mon->lt[c->mon->sellt]->arrange || c->isfloating)
			resize(c, c->x, c->y, c->w, c->h, false);
		showhide(c->snext);
	}
	else { /* hide clients bottom up */
		showhide(c->snext);
		uint32_t values[] = { c->x + 2 * sw, c->y };
		xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
	}
}


void sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

void sigint(int unused)
{
	exit(0);
}

void spawn(const Arg *arg) {
	if(fork() == 0) {
		if(conn)
			close(xcb_get_file_descriptor(conn));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "dwm: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(0);
	}
}

void tag(const Arg *arg) {
	if(selmon->sel && arg->ui & TAGMASK) {
		selmon->sel->tags = arg->ui & TAGMASK;
		arrange(selmon);
	}
}

void tagmon(const Arg *arg) {
	if(!selmon->sel || !mons->next)
		return;
	sendmon(selmon->sel, dirtomon(arg->i));
}

int textnw(const char *text, unsigned int len) {
	xcb_char2b_t *text_copy = (xcb_char2b_t*)malloc_safe(len*2);
	for(int i = 0; i < len; i++)
		text_copy[i].byte2 = text[i];
	xcb_query_text_extents_reply_t *reply = xcb_query_text_extents_reply(conn,	// FIXME: we should probably use iconv or something instead of casting
		xcb_query_text_extents(conn, dc.gc, len, text_copy), &err);
	testerr();
	free(reply);
	return reply->overall_width;
}

void tile(Monitor *m) {
	int x, y, h, w, mw;
	unsigned int i, n;
	Client *c;

	for(n = 0, c = nexttiled(m->clients); c; c = nexttiled(c->next), n++);
	if(n == 0)
		return;
	/* master */
	c = nexttiled(m->clients);
	mw = m->mfact * m->ww;
	resize(c, m->wx, m->wy, (n == 1 ? m->ww : mw) - 2 * c->bw, m->wh - 2 * c->bw, false);
	if(--n == 0)
		return;
	/* tile stack */
	x = (m->wx + mw > c->x + c->w) ? c->x + c->w + 2 * c->bw : m->wx + mw;
	y = m->wy;
	w = (m->wx + mw > c->x + c->w) ? m->wx + m->ww - x : m->ww - mw;
	h = m->wh / n;
	if(h < bh)
		h = m->wh;
	for(i = 0, c = nexttiled(c->next); c; c = nexttiled(c->next), i++) {
		resize(c, x, y, w - 2 * c->bw, /* remainder */ ((i + 1 == n)
		       ? m->wy + m->wh - y - 2 * c->bw : h - 2 * c->bw), false);
		if(h != m->wh)
			y = c->y + HEIGHT(c);
	}
}

void togglebar(const Arg *arg) {
	selmon->showbar = !selmon->showbar;
	updatebarpos(selmon);
	uint32_t values[] = { selmon->wx, selmon->by, selmon->ww, bh };
	xcb_configure_window(conn, selmon->barwin, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
	arrange(selmon);
}

void togglefloating(const Arg *arg) {
	if(!selmon->sel)
		return;
	selmon->sel->isfloating = !selmon->sel->isfloating || selmon->sel->isfixed;
	if(selmon->sel->isfloating)
		resize(selmon->sel, selmon->sel->x, selmon->sel->y,
		       selmon->sel->w, selmon->sel->h, false);
	arrange(selmon);
}

void toggletag(const Arg *arg) {
	unsigned int newtags;

	if(!selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if(newtags) {
		selmon->sel->tags = newtags;
		arrange(selmon);
	}
}

void toggleview(const Arg *arg) {
	unsigned int newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);

	if(newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		arrange(selmon);
	}
}

void unfocus(Client *c, bool setfocus) {
	if(!c)
		return;
	grabbuttons(c, false);
	xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, 
		(uint32_t*)&dc.norm[ColBorder]);
	if(setfocus)
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, 
			c->win, XCB_CURRENT_TIME);
}

void unmanage(Client *c, bool destroyed) {
	Monitor *m = c->mon;

	/* The server grab construct avoids race conditions. */
	detach(c);
	detachstack(c);
	printf("unmanage %i, %i\n", destroyed, c->win);
	if(!destroyed) {
		uint32_t values[] = { c->oldbw };
		xcb_grab_server(conn);
		xcb_configure_window_checked(conn, c->win, 
			XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
		xcb_ungrab_button_checked(conn, XCB_BUTTON_INDEX_ANY, c->win, 
			XCB_GRAB_ANY);
		setclientstate(c, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(conn);
		xcb_ungrab_server(conn);
	}
	free(c);
	focus(NULL);
	arrange(m);
}

int unmapnotify(xcb_generic_event_t *e) {
	Client *c;
	xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t*)e;

	printf("unmapnotify %i\n", ev->window);
	if((c = wintoclient(ev->window)))
		unmanage(c, false);

	return 0;
}

void updatebars(void) {
	Monitor *m;
	uint32_t values[] = { XCB_BACK_PIXMAP_NONE, dc.norm[ColBG], true, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_EXPOSURE, cursor[CurNormal] };
	for(m = mons; m; m = m->next) {
		m->barwin = xcb_generate_id(conn);
		xcb_create_window(conn, XCB_COPY_FROM_PARENT, m->barwin,
			root, m->wx, m->by, m->ww, bh, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			xscreen->root_visual, XCB_CW_BACK_PIXMAP | XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
			XCB_CW_EVENT_MASK | XCB_CW_CURSOR, values);
		xcb_map_window(conn, m->barwin);
	}
}

void updatebarpos(Monitor *m) {
	m->wy = m->my;
	m->wh = m->mh;
	if(m->showbar) {
		m->wh -= bh;
		m->by = m->topbar ? m->wy : m->wy + m->wh;
		m->wy = m->topbar ? m->wy + bh : m->wy;
	}
	else
		m->by = -bh;
}

bool updategeom(void) {
	bool dirty = false;

#ifdef XINERAMA
	if(XineramaIsActive(dpy)) {
		int i, j, n, nn;
		Client *c;
		Monitor *m;
		XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
		XineramaScreenInfo *unique = NULL;

		info = XineramaQueryScreens(dpy, &nn);
		for(n = 0, m = mons; m; m = m->next, n++);
		/* only consider unique geometries as separate screens */
		if(!(unique = (XineramaScreenInfo *)malloc(sizeof(XineramaScreenInfo) * nn)))
			die("fatal: could not malloc() %u bytes\n", sizeof(XineramaScreenInfo) * nn);
		for(i = 0, j = 0; i < nn; i++)
			if(isuniquegeom(unique, j, &info[i]))
				memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
		XFree(info);
		nn = j;
		if(n <= nn) {
			for(i = 0; i < (nn - n); i++) { /* new monitors available */
				for(m = mons; m && m->next; m = m->next);
				if(m)
					m->next = createmon();
				else
					mons = createmon();
			}
			for(i = 0, m = mons; i < nn && m; m = m->next, i++)
				if(i >= n
				|| (unique[i].x_org != m->mx || unique[i].y_org != m->my
				    || unique[i].width != m->mw || unique[i].height != m->mh))
				{
					dirty = true;
					m->num = i;
					m->mx = m->wx = unique[i].x_org;
					m->my = m->wy = unique[i].y_org;
					m->mw = m->ww = unique[i].width;
					m->mh = m->wh = unique[i].height;
					updatebarpos(m);
				}
		}
		else { /* less monitors available nn < n */
			for(i = nn; i < n; i++) {
				for(m = mons; m && m->next; m = m->next);
				while(m->clients) {
					dirty = true;
					c = m->clients;
					m->clients = c->next;
					detachstack(c);
					c->mon = mons;
					attach(c);
					attachstack(c);
				}
				if(m == selmon)
					selmon = mons;
				cleanupmon(m);
			}
		}
		free(unique);
	}
	else
#endif /* XINERAMA */
	/* default monitor setup */
	{
		if(!mons)
			mons = createmon();
		if(mons->mw != sw || mons->mh != sh) {
			dirty = true;
			mons->mw = mons->ww = sw;
			mons->mh = mons->wh = sh;
			updatebarpos(mons);
		}
	}
	if(dirty) {
		selmon = mons;
		selmon = wintomon(root);
	}
	return dirty;
}

void updatenumlockmask(void)
{
	/* taken from i3 */
	xcb_get_modifier_mapping_reply_t* reply =
		xcb_get_modifier_mapping_reply(conn, xcb_get_modifier_mapping(conn), &err);
	testerr();
	xcb_keycode_t *codes = xcb_get_modifier_mapping_keycodes(reply);
	xcb_keycode_t target, *temp;
	unsigned int i, j;

	if((temp = xcb_key_symbols_get_keycode(syms, XK_Num_Lock)))
	{
		target = *temp;
		free(temp);
	}
	else
		return;

	for(i = 0; i < 8; i++)
		for(j = 0; j < reply->keycodes_per_modifier; j++)
			if(codes[i * reply->keycodes_per_modifier + j] == target)
				numlockmask = (1 << i);

	free(reply);
}

void updatesizehints(Client *c)
{
	xcb_size_hints_t hints;

	if(xcb_icccm_get_wm_normal_hints_reply(conn, xcb_icccm_get_wm_normal_hints(conn, c->win), &hints, NULL))
		/* size is uninitialized, ensure that size.flags aren't used */
		hints.flags = XCB_ICCCM_SIZE_HINT_P_SIZE;
	if(hints.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
		c->basew = hints.base_width;
		c->baseh = hints.base_height;
	}
	else if(hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		c->basew = hints.min_width;
		c->baseh = hints.min_height;
	}
	else
		c->basew = c->baseh = 0;
	if(hints.flags & XCB_ICCCM_SIZE_HINT_P_RESIZE_INC) {
		c->incw = hints.width_inc;
		c->inch = hints.height_inc;
	}
	else
		c->incw = c->inch = 0;
	if(hints.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) {
		c->maxw = hints.max_width;
		c->maxh = hints.max_height;
	}
	else
		c->maxw = c->maxh = 0;
	if(hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE) {
		c->minw = hints.min_width;
		c->minh = hints.min_height;
	}
	else if(hints.flags & XCB_ICCCM_SIZE_HINT_BASE_SIZE) {
		c->minw = hints.base_width;
		c->minh = hints.base_height;
	}
	else
		c->minw = c->minh = 0;
	if(hints.flags & XCB_ICCCM_SIZE_HINT_P_ASPECT) {
		c->mina = (float)hints.min_aspect_den / hints.min_aspect_num;
		c->maxa = (float)hints.max_aspect_num / hints.max_aspect_den;
	}
	else
		c->maxa = c->mina = 0.0;
	c->isfixed = (c->maxw && c->minw && c->maxh && c->minh
	             && c->maxw == c->minw && c->maxh == c->minh);
}

void updatetitle(Client *c) {
	if(!gettextprop(c->win, netatom[NetWMName], c->name, sizeof c->name))
	{
		if(!gettextprop(c->win, XCB_ATOM_WM_NAME, c->name, sizeof c->name))
			strcpy(c->name, broken);	// old broken hack?  why was it like that?
	}
}

void updatestatus(void) {
	if(!gettextprop(root, XCB_ATOM_WM_NAME, stext, sizeof(stext)))
		strcpy(stext, "dwm-"VERSION);
	drawbar(selmon);
}

void updatewmhints(Client *c) {
	xcb_icccm_wm_hints_t wmh;

	if(!xcb_icccm_get_wm_hints_reply(conn, xcb_icccm_get_wm_hints(conn, c->win), &wmh, &err))
		return;
	testerr();

	if(c == selmon->sel && wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) {
		wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
		xcb_icccm_set_wm_hints(conn, c->win, &wmh);
	}
	else
		c->isurgent = (wmh.flags & XCB_ICCCM_WM_HINT_X_URGENCY) ? true : false;
}

void view(const Arg *arg) {
	if((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if(arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	arrange(selmon);
}

Client* wintoclient(xcb_window_t w) {
	Client *c;
	Monitor *m;

	for(m = mons; m; m = m->next)
		for(c = m->clients; c; c = c->next)
			if(c->win == w)
				return c;
	return NULL;
}

Monitor* wintomon(xcb_window_t w) {
	int x, y;
	Client *c;
	Monitor *m;

	if(w == root && getrootptr(&x, &y))
		return ptrtomon(x, y);
	for(m = mons; m; m = m->next)
		if(w == m->barwin)
			return m;
	if((c = wintoclient(w)))
		return c->mon;
	return selmon;
}

void zoom(const Arg *arg) {
	Client *c = selmon->sel;

	if(!selmon->lt[selmon->sellt]->arrange
	|| selmon->lt[selmon->sellt]->arrange == monocle
	|| (selmon->sel && selmon->sel->isfloating))
		return;
	if(c == nexttiled(selmon->clients))
		if(!c || !(c = nexttiled(c->next)))
			return;
	detach(c);
	attach(c);
	focus(c);
	arrange(c->mon);
}

void run(void) {
	xcb_generic_event_t *event;

	xcb_flush(conn);
	
	while((event = xcb_wait_for_event(conn)))
	{
		if(event->response_type == 0)
		{
			xcb_generic_error_t *error;

			error = event;
			fprintf(stderr, "previous request returned error %i, \"%s\" major code %i, minor code %i resource %i seq number %i\n",
				(int)error->error_code, xcb_event_get_error_label(error->error_code),
				(uint32_t) error->major_code, (uint32_t) error->minor_code,
				(uint32_t) error->resource_id, (uint32_t) error->sequence);
		} else {
			for(const handler_func_t* handler = handler_funs; 
				handler->func != NULL; handler++)
			{
				if((event->response_type & ~0x80) == handler->request)
					handler->func(event);
			}	
		}

		free(event);
	}
}

int main(int argc, char *argv[]) {
	signal(SIGINT, sigint);

	if(argc == 2 && !strcmp("-v", argv[1]))
		die("dwm-"VERSION", © 2006-2010 dwm engineers, see LICENSE for details\n");
	else if(argc != 1)
		die("usage: dwm [-v]\n");
	if(!setlocale(LC_CTYPE, ""))		// FIXME: X11 locale type?
		fputs("warning: no locale support\n", stderr);
	if(!(conn = xcb_connect(NULL, &screen)))
		die("dwm: cannot open display\n");

	/* init screen (to get root) */
	xscreen = xcb_aux_get_screen(conn, 0);
	root = xscreen->root;
	
	checkotherwm();
	setup();
	scan();

	run();

	return 0;
}
