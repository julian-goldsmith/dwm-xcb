#ifndef DWM_H
#define DWM_H

#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

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

typedef struct
{
	uint32_t request;
	//xcb_generic_event_handler_t func;
	int (*func)(xcb_generic_event_t *e);
} handler_func_t;

bool client_apply_size_hints(Client *c, int *x, int *y, int *w, int *h, bool interact);
void client_attach(Client *c);
void client_attach_stack(Client *c);
void client_clear_urgent(Client *c);
void client_configure(Client *c);
void client_detach(Client *c);
void client_detach_stack(Client *c);
void client_focus(Client *c);
void client_unfocus(Client *c, bool setfocus);
void client_show_hide(Client *c);
void client_resize(Client *c, int x, int y, int w, int h, bool interact);
void client_resize_client(Client *c, int x, int y, int w, int h);
void client_set_state(Client *c, long state);
Client *client_next_tiled(Client *c);
bool client_is_proto_del(Client *c);

void drawbars(void);
void grabbuttons(Client *c, bool focused);

extern xcb_connection_t* conn;
extern DC dc;
extern Monitor *mons, *selmon;
extern xcb_window_t root;
extern int sw, sh;           /* X display screen geometry width, height */
extern int bh, blw;      /* bar geometry */
extern xcb_atom_t wmatom[WMLast], netatom[NetLast];

#endif
