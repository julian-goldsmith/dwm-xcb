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
void client_unmanage(Client *c, bool destroyed);
void client_send_to_monitor(Client *c, Monitor *m);
Client *client_get_from_window(xcb_window_t w);
void client_update_title(Client *c);
void client_update_size_hints(Client *c);

void draw_init();
void draw_bars(void);
void draw_bar(Monitor *m);
void draw_square(bool filled, bool empty, bool invert, uint32_t col[ColLast], xcb_window_t w);
void draw_text(const char *text, uint32_t col[ColLast], bool invert, xcb_window_t w);

// FIXME: Rename these.
void grabbuttons(Client *c, bool focused);
void arrange(Monitor *m);
void arrangemon(Monitor *m);
int buttonpress(xcb_generic_event_t *e);
void checkotherwm(void);
void cleanup(void);
void cleanupmon(Monitor *mon);
int clientmessage(xcb_generic_event_t *e);
int configurenotify(xcb_generic_event_t *e);
int configurerequest(xcb_generic_event_t *e);
Monitor *createmon(void);
int destroynotify(xcb_generic_event_t *e);
void die(const char *errstr, ...);
Monitor *dirtomon(int dir);
int enternotify(xcb_generic_event_t *e);
int expose(xcb_generic_event_t *e);
int focusin(xcb_generic_event_t *e);
void focusmon(const Arg *arg);
void focusstack(const Arg *arg);
uint32_t getcolor(const char *colstr);
bool getrootptr(int *x, int *y);
xcb_atom_t getstate(xcb_window_t w);
bool gettextprop(xcb_window_t w, xcb_atom_t atom, char *text, unsigned int size);
void grabkeys(void);
void initfont(const char *fontstr);
int keypress(xcb_generic_event_t *e);
void killclient(const Arg *arg);
void manage(xcb_window_t w);
int mappingnotify(xcb_generic_event_t *e);
int maprequest(xcb_generic_event_t *e);
void monocle(Monitor *m);
void movemouse(const Arg *arg);
Monitor *ptrtomon(int x, int y);
int propertynotify(xcb_generic_event_t *e);
void quit(const Arg *arg);
void resizemouse(const Arg *arg);
void restack(Monitor *m);
void scan(void);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void setup(void);
void sigchld(int unused);
void spawn(const Arg *arg);
void tag(const Arg *arg);
void tagmon(const Arg *arg);
int textnw(const char *text, unsigned int len);
void tile(Monitor *);
void togglebar(const Arg *arg);
void togglefloating(const Arg *arg);
void toggletag(const Arg *arg);
void toggleview(const Arg *arg);
int unmapnotify(xcb_generic_event_t *e);
bool updategeom(void);
void updatebarpos(Monitor *m);
void updatebars(void);
void updatenumlockmask(void);
void updatestatus(void);
void updatewmhints(Client *c);
void view(const Arg *arg);
Monitor *wintomon(xcb_window_t w);
void zoom(const Arg *arg);

void handle_clear_event(int response_type);
void handle_event_loop();

extern xcb_connection_t* conn;
extern xcb_generic_error_t *err;
extern DC dc;
extern Monitor *mons, *selmon;
extern xcb_window_t root;
extern int sw, sh;												/* X display screen geometry width, height */
extern int bh, blw;												/* bar geometry */
extern char stext[256];
extern unsigned int numlockmask;
extern xcb_key_symbols_t *syms;

/* EWMH atoms */
extern xcb_atom_t NetSupported;
extern xcb_atom_t NetWMName;
extern xcb_atom_t NetWMState;
extern xcb_atom_t NetWMFullscreen;

/* default atoms */
extern xcb_atom_t WMProtocols;
extern xcb_atom_t WMDelete;
extern xcb_atom_t WMState;

/* appearance */
extern const char font[];
extern const char normbordercolor[];
extern const char normbgcolor[];
extern const char normfgcolor[];
extern const char selbordercolor[];
extern const char selbgcolor[];
extern const char selfgcolor[];
extern xcb_cursor_t cursor[CurLast];

extern const unsigned int borderpx;
extern const unsigned int snap;
extern const bool showbar;
extern const bool topbar;

/* tagging */
#define NUM_TAGS 9
extern const char *tags[NUM_TAGS];
extern unsigned int tagwidths[NUM_TAGS];
extern unsigned int alltagswidth;

/* layout(s) */
extern const float mfact;
extern const bool resizehints;

#define NUM_LAYOUTS 3
extern const Layout layouts[NUM_LAYOUTS];

/* commands */
extern const char *dmenucmd[];
extern const char *termcmd[];

extern const Key keys[];
extern const Button buttons[];

void _testerr(const char* file, const int line);
#define testerr() _testerr(__FILE__, __LINE__);

void _testcookie(xcb_void_cookie_t cookie, const char* file, const int line);
#define testcookie(cookie) _testcookie(cookie, __FILE__, __LINE__);

#endif
