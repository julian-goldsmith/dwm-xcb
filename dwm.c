/* See LICENSE file for copyright and license details.  */

#include "dwm.h"

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
#include <X11/keysym.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_event.h>
#include <assert.h>
#include <stdbool.h>

/* variables */
static const char broken[] = "broken";
char stext[256];
static int screen;
int sw, sh;           /* X display screen geometry width, height */
int bh, blw = 0;      /* bar geometry */
static unsigned int numlockmask = 0;
xcb_cursor_t cursor[CurLast];
DC dc;
Monitor *mons = NULL, *selmon = NULL;
xcb_window_t root;
static xcb_screen_t *xscreen = NULL;
static xcb_key_symbols_t *syms = NULL;
xcb_generic_error_t *err = NULL;
xcb_connection_t *conn = NULL;

/* EWMH atoms */
xcb_atom_t NetSupported;
xcb_atom_t NetWMName;
xcb_atom_t NetWMState;
xcb_atom_t NetWMFullscreen;

/* default atoms */
xcb_atom_t WMProtocols;
xcb_atom_t WMDelete;
xcb_atom_t WMState;

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

void _testerr(const char* file, const int line)
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

void _testcookie(xcb_void_cookie_t cookie, const char* file, const int line)
{
	err = xcb_request_check(conn, cookie);
	_testerr(file, line);
}

void clearevent(int response_type)
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
				if((ev->response_type & ~0x80) == handler->request) {
					handler->func(ev);
				}
			}	
		}
			
		free(ev);
	}
}

void arrange(Monitor *m) {
	if(m) {
		client_show_hide(m->stack);
		client_focus(NULL);
		arrangemon(m);
	} else {
		for(m = mons; m; m = m->next) {
			client_show_hide(m->stack);
		}

		client_focus(NULL);

		for(m = mons; m; m = m->next) {
			arrangemon(m);
		}
	}
}

void arrangemon(Monitor *m) {
	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));

	if(m->lt[m->sellt]->arrange) {
		m->lt[m->sellt]->arrange(m);
	}

	restack(m);
}

int buttonpress(xcb_generic_event_t *ev) {
	Monitor *m;
	xcb_button_press_event_t *e = (xcb_button_press_event_t*) ev;
	unsigned int click;
	unsigned int seltag = 0;

	/* focus monitor if necessary */
	if((m = wintomon(e->event)) && m != selmon) {
		client_unfocus(selmon->sel, true);
		selmon = m;
		client_focus(NULL);
	}

	if(e->event == selmon->barwin) {
		if(e->event_x < alltagswidth) {
			click = ClkTagBar;

			unsigned int* tagwidth = tagwidths;
			unsigned int tagx = 0;

			for (const char** tag = tags;
			     tag < tags + NUM_TAGS && e->event_x < tagx;
			     tag++, tagwidth++, seltag++)
			{
				tagx += *tagwidth;
			}
		} else if(e->event_x < alltagswidth + blw) {
			click = ClkLtSymbol;
		} else if(e->event_x > selmon->wx + selmon->ww - TEXTW(stext)) {
			click = ClkStatusText;
		} else {
			click = ClkWinTitle;
		}
	}
	else {
		Client *c = client_get_from_window(e->event);
		if (c) {
			client_focus(c);
			click = ClkClientWin;
		} else {
			click = ClkRootWin;
		}
	}

	for (const Button *button = buttons; button->func != NULL; button++)
	{
		if(click == button->click && button->button == e->detail &&
		   CLEANMASK(button->mask) == CLEANMASK(e->state))
		{
			Arg arg = button->arg;

			if (click == ClkTagBar) {
				arg.ui = 1 << seltag;
			}

			button->func(&arg);
		}
	}

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
			client_unmanage(m->stack, false);
	xcb_close_font(conn, dc.font.xfont);

	xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
	xcb_free_cursor(conn, cursor[CurNormal]);
	xcb_free_cursor(conn, cursor[CurResize]);
	xcb_free_cursor(conn, cursor[CurMove]);

	xcb_free_colors(conn, xscreen->default_colormap, 0, ColLast, dc.norm);
	xcb_free_colors(conn, xscreen->default_colormap, 0, ColLast, dc.sel);

	while(mons) {
		cleanupmon(mons);
	}

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

	if((c = client_get_from_window(ev->window))) {
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
				client_configure(c);
			if(ISVISIBLE(c))
			{
				uint32_t values[] = { c->x, c->y, c->w, c->h };
				testcookie(xcb_configure_window_checked(conn, c->win, XCB_CONFIG_WINDOW_X |
					XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
					XCB_CONFIG_WINDOW_HEIGHT, values));
			}
		}
		else
			client_configure(c);
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

Monitor* createmon() {
	Monitor *m = (Monitor*) malloc(sizeof(Monitor));
	m->tagset[0] = m->tagset[1] = 1;
	m->mfact = mfact;
	m->showbar = showbar;
	m->topbar = topbar;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % NUM_LAYOUTS];
	strncpy(m->ltsymbol, layouts[0].symbol, sizeof m->ltsymbol);
	return m;
}

int destroynotify(xcb_generic_event_t *e) {
	Client *c;
	xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)e;
	printf("destroynotify\n");

	if((c = client_get_from_window(ev->window)))
		client_unmanage(c, true);

	return 0;
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

int enternotify(xcb_generic_event_t *e) {
	Monitor *m;
	xcb_enter_notify_event_t *ev = (xcb_enter_notify_event_t*)e;

	if((ev->mode != XCB_NOTIFY_MODE_NORMAL || ev->detail == XCB_NOTIFY_DETAIL_INFERIOR) && ev->event != root)
		return 0;
	if((m = wintomon(ev->event)) && m != selmon) {
		client_unfocus(selmon->sel, true);
		selmon = m;
	}

	client_focus(client_get_from_window(ev->event));

	return 0;
}

int expose(xcb_generic_event_t *e) {
	Monitor *m;
	xcb_expose_event_t *ev = (xcb_expose_event_t*)e;

	if(ev->count == 0 && (m = wintomon(ev->window)))
		draw_bar(m);

	return 0;
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
	client_unfocus(selmon->sel, true);
	selmon = m;
	client_focus(NULL);
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
		client_focus(c);
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
		w, WMState, XCB_ATOM_ATOM, 0, 0);
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
	updatenumlockmask();
	xcb_ungrab_button(conn, XCB_BUTTON_INDEX_ANY, c->win, XCB_GRAB_ANY);

	if (focused) {
		for (const Button *button = buttons; button->func != NULL; button++) {
			if(button->click == ClkClientWin) {
				xcb_grab_button(conn, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
					XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
					buttons->button, buttons->mask);
				xcb_grab_button(conn, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
					XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
					buttons->button, buttons->mask | XCB_MOD_MASK_LOCK);
				xcb_grab_button(conn, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
					XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
					buttons->button, buttons->mask | numlockmask);
				xcb_grab_button(conn, false, c->win, BUTTONMASK, XCB_GRAB_MODE_SYNC,
					XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
					buttons->button, buttons->mask | numlockmask | XCB_MOD_MASK_LOCK);
			}
		}
	}
	else
	{
		xcb_grab_button(conn, false, c->win, BUTTONMASK, XCB_GRAB_MODE_ASYNC,
			XCB_GRAB_MODE_SYNC, XCB_WINDOW_NONE, XCB_CURSOR_NONE,
			XCB_BUTTON_INDEX_ANY, XCB_BUTTON_MASK_ANY);
	}
}

void grabkeys(void)
{
	updatenumlockmask();
	xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);

	for (const Key* key = keys; key->func != NULL; key++)
	{
		xcb_keycode_t *code = xcb_key_symbols_get_keycode(syms, key->keysym);

		if(code)
		{
			xcb_grab_key(conn, true, root, key->mod,
				*code, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
			xcb_grab_key(conn, true, root, key->mod | XCB_MOD_MASK_LOCK,
				*code, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
			xcb_grab_key(conn, true, root, key->mod | numlockmask,
				*code, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
			xcb_grab_key(conn, true, root, key->mod | numlockmask | XCB_MOD_MASK_LOCK,
				*code, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

			free(code);
		}
	}
}

int keypress(xcb_generic_event_t *e) {
	xcb_key_press_event_t *ev = (xcb_key_press_event_t*) e;
	xcb_keysym_t keysym = xcb_key_press_lookup_keysym(syms, ev, 0);

	for (const Key* key = keys; key->func != NULL; key++)
	{
		if(keysym == key->keysym && CLEANMASK(key->mod) == CLEANMASK(ev->state))
		{
			key->func(&key->arg);
		}
	}

	return 0;
}

void killclient(const Arg *arg) {
	if(!selmon->sel)
		return;
	if(client_is_proto_del(selmon->sel)) {
		xcb_client_message_event_t ev;
		ev.response_type = XCB_CLIENT_MESSAGE;
		ev.window = selmon->sel->win;
		ev.format = 32;
		ev.data.data32[0] = WMDelete;
		ev.data.data32[1] = XCB_TIME_CURRENT_TIME;
		ev.type = WMProtocols;
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

	c = (Client*) malloc(sizeof(Client));
	c->win = w;
	updatetitle(c);

	xcb_get_geometry_cookie_t geom_cookie = xcb_get_geometry(conn, w);
	
	xcb_window_t trans_reply = XCB_NONE;
	xcb_icccm_get_wm_transient_for_reply(conn, xcb_icccm_get_wm_transient_for(conn, w), &trans_reply, &err);
	testerr();

	if(trans_reply != XCB_NONE)
		t = client_get_from_window(trans_reply);
	if(t) {
		c->mon = t->mon;
		c->tags = t->tags;
	}
	else {
		c->mon = selmon;
		c->isfloating = 0;
		c->tags = c->mon->tagset[c->mon->seltags];
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
	client_configure(c); /* propagates border_width, if size doesn't change */
	updatesizehints(c);
	grabbuttons(c, false);
	if(!c->isfloating)
		c->isfloating = c->oldstate = trans != XCB_WINDOW_NONE || c->isfixed;
	client_attach(c);
	client_attach_stack(c);
	uint32_t config_values[] = { c->x + 2 * sw, c->y, c->w, c->h, c->bw, XCB_STACK_MODE_ABOVE };
	xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
			     XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT |
			     XCB_CONFIG_WINDOW_BORDER_WIDTH | 
			     (c->isfloating) ? XCB_CONFIG_WINDOW_STACK_MODE : 0, config_values);
	xcb_map_window(conn, c->win);
	client_set_state(c, XCB_ICCCM_WM_STATE_NORMAL);
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
	if(!client_get_from_window(ev->window))
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
	for(c = client_next_tiled(m->clients); c; c = client_next_tiled(c->next))
		client_resize(c, m->wx, m->wy, m->ww - 2 * c->bw, m->wh - 2 * c->bw, false);
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
				client_resize(c, nx, ny, c->w, c->h, true);
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
		client_send_to_monitor(c, m);
		selmon = m;
		client_focus(NULL);
	}
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
	else if((c = client_get_from_window(ev->window))) {
		if(ev->atom == XCB_ATOM_WM_TRANSIENT_FOR)
		{
			// get WM_TRANSIENT_FOR
			xcb_window_t trans = XCB_NONE;
			xcb_get_property_cookie_t cookie = xcb_icccm_get_wm_transient_for(conn, c->win);
			xcb_get_property_reply_t* reply = xcb_get_property_reply(conn, cookie, &err);
			testerr();
			xcb_icccm_get_wm_transient_for_from_reply(&trans, reply);

			if(trans != XCB_NONE && !c->isfloating && (c->isfloating = (client_get_from_window(trans) != NULL)))
				arrange(c->mon);
		}
		else if(ev->atom == XCB_ATOM_WM_NORMAL_HINTS)
		{
			updatesizehints(c);
		}
		else if(ev->atom == XCB_ATOM_WM_HINTS)
		{
			updatewmhints(c);
			draw_bars();
		}
		else if(ev->atom == XCB_ATOM_WM_NAME || ev->atom == NetWMName) {
			updatetitle(c);

			if(c == c->mon->sel) {
				draw_bar(c->mon);
			}
		}
	}

	return 0;
}

int clientmessage(xcb_generic_event_t *e) {
	xcb_client_message_event_t *cme = (xcb_client_message_event_t*)e;
	Client *c;

	if((c = client_get_from_window(cme->window)) && 
		(cme->type == NetWMState && 
		 cme->data.data32[1] == NetWMFullscreen))
	{
		if(cme->data.data32[0]) {
			xcb_change_property(conn, XCB_PROP_MODE_REPLACE, cme->window, 
				NetWMState, XCB_ATOM, 32, 1, 
				(unsigned char*)& NetWMFullscreen);							// FIXME: How does this work?
			c->oldstate = c->isfloating;
			c->oldbw = c->bw;
			c->bw = 0;
			c->isfloating = 1;
			client_resize_client(c, c->mon->mx, c->mon->my, c->mon->mw, c->mon->mh);
			uint32_t values[] = { XCB_STACK_MODE_ABOVE };
			xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE, values);
		}
		else {
			xcb_change_property(conn, XCB_PROP_MODE_REPLACE, cme->window,
				NetWMState, XCB_ATOM, 32, 0, (unsigned char*) 0);
			c->isfloating = c->oldstate;
			c->bw = c->oldbw;
			c->x = c->oldx;
			c->y = c->oldy;
			c->w = c->oldw;
			c->h = c->oldh;
			client_resize_client(c, c->x, c->y, c->w, c->h);
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
				client_resize(c, c->x, c->y, nw, nh, true);
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
		client_send_to_monitor(c, m);
		selmon = m;
		client_focus(NULL);
	}
}

void restack(Monitor *m) {
	draw_bar(m);

	if(!m->sel) {
		return;
	}

	if(m->sel->isfloating || !m->lt[m->sellt]->arrange) {
		uint32_t values[] = { XCB_STACK_MODE_ABOVE };
		xcb_configure_window(conn, m->sel->win, XCB_CONFIG_WINDOW_STACK_MODE, values);
	}

	if(m->lt[m->sellt]->arrange) {
		for(Client *c = m->stack; c; c = c->snext) {
			uint32_t values[] = { m->barwin, XCB_STACK_MODE_BELOW };

			if(!c->isfloating && ISVISIBLE(c)) {
				xcb_configure_window(conn, c->win,
					XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE,
					values);
				values[0] = c->win;
			}
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

void setlayout(const Arg *arg) {
	if(!arg || !arg->v || arg->v != selmon->lt[selmon->sellt]) {
		selmon->sellt ^= 1;
	}

	if(arg && arg->v) {
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	}

	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof selmon->ltsymbol);

	if(selmon->sel) {
		arrange(selmon);
	} else {
		draw_bar(selmon);
	}
}

/* arg > 1.0 will set mfact absolutely */
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

xcb_atom_t setup_atom(const char* name) {
	xcb_intern_atom_cookie_t atom_cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(conn, atom_cookie, &err);
	xcb_atom_t atom;

	if(reply) {
		atom = reply->atom;
		free(reply);
	} else {
		testerr();
	}

	return atom;
}

void setup_atoms() {
	WMProtocols = setup_atom("WM_PROTOCOLS");
	WMDelete = setup_atom("WM_DELETE_WINDOW");
	WMState = setup_atom("WM_STATE");
	NetSupported = setup_atom("_NET_SUPPORTED");
	NetWMName = setup_atom("_NET_WM_NAME");
	NetWMState = setup_atom("_NET_WM_STATE");
	NetWMFullscreen = setup_atom("_NET_WM_STATE_FULLSCREEN");
}

void setup(void)
{
	/* clean up any zombies immediately */
	sigchld(0);

	draw_init();

	sw = xscreen->width_in_pixels;
	sh = xscreen->height_in_pixels;
	updategeom();

	setup_atoms();

	/* init bars */
	updatebars();
	updatestatus();

	/* EWMH support per view */
	xcb_atom_t supported[] = { NetSupported, NetWMName, NetWMState, NetWMFullscreen };
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root, NetSupported, XCB_ATOM, 32,
		sizeof(supported) / sizeof(xcb_atom_t), (unsigned char*) supported);

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

void sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR) {
		die("Can't install SIGCHLD handler");
	}

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
	client_send_to_monitor(selmon->sel, dirtomon(arg->i));
}

void tile(Monitor *m) {
	int x, y, h, w, mw;
	unsigned int i, n;
	Client *c;

	for(n = 0, c = client_next_tiled(m->clients); c; c = client_next_tiled(c->next), n++);
	if(n == 0)
		return;
	/* master */
	c = client_next_tiled(m->clients);
	mw = m->mfact * m->ww;
	client_resize(c, m->wx, m->wy, (n == 1 ? m->ww : mw) - 2 * c->bw, m->wh - 2 * c->bw, false);
	if(--n == 0)
		return;
	/* tile stack */
	x = (m->wx + mw > c->x + c->w) ? c->x + c->w + 2 * c->bw : m->wx + mw;
	y = m->wy;
	w = (m->wx + mw > c->x + c->w) ? m->wx + m->ww - x : m->ww - mw;
	h = m->wh / n;
	if(h < bh)
		h = m->wh;
	for(i = 0, c = client_next_tiled(c->next); c; c = client_next_tiled(c->next), i++) {
		client_resize(c, x, y, w - 2 * c->bw, /* remainder */ ((i + 1 == n)
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
		client_resize(selmon->sel, selmon->sel->x, selmon->sel->y,
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

int unmapnotify(xcb_generic_event_t *e) {
	Client *c;
	xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t*)e;

	printf("unmapnotify %i\n", ev->window);
	if((c = client_get_from_window(ev->window)))
		client_unmanage(c, false);

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

	/* default monitor setup */
	if(!mons)
		mons = createmon();
	if(mons->mw != sw || mons->mh != sh) {
		dirty = true;
		mons->mw = mons->ww = sw;
		mons->mh = mons->wh = sh;
		updatebarpos(mons);
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
	if(!gettextprop(c->win, NetWMName, c->name, sizeof c->name)) {
		if(!gettextprop(c->win, XCB_ATOM_WM_NAME, c->name, sizeof c->name)) {
			strcpy(c->name, broken);
		}
	}
}

void updatestatus(void) {
	if(!gettextprop(root, XCB_ATOM_WM_NAME, stext, sizeof(stext))) {
		strcpy(stext, "dwm-"VERSION);
	}

	draw_bar(selmon);
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

Monitor* wintomon(xcb_window_t w) {
	int x, y;
	if(w == root && getrootptr(&x, &y)) {
		return ptrtomon(x, y);
	}

	Monitor *m;
	for(m = mons; m; m = m->next) {
		if(w == m->barwin) {
			return m;
		}
	}

	Client *c;
	if((c = client_get_from_window(w))) {
		return c->mon;
	}

	return selmon;
}

void zoom(const Arg *arg) {
	Client *c = selmon->sel;

	if(!selmon->lt[selmon->sellt]->arrange
		|| selmon->lt[selmon->sellt]->arrange == monocle
		|| (selmon->sel && selmon->sel->isfloating)) {

		return;
	}

	if(c == client_next_tiled(selmon->clients)) {
		if(!c || !(c = client_next_tiled(c->next))) {
			return;
		}
	}

	client_detach(c);
	client_attach(c);
	client_focus(c);
	arrange(c->mon);
}

void run(void) {
	xcb_generic_event_t *event;

	xcb_flush(conn);
	
	while((event = xcb_wait_for_event(conn)))
	{
		if(event->response_type == 0)
		{
			xcb_generic_error_t *error = (xcb_generic_error_t*) event;

			fprintf(stderr, "previous request returned error %i, \"%s\" major code %i, minor code %i resource %i seq number %i\n",
				(int)error->error_code, xcb_event_get_error_label(error->error_code),
				(uint32_t) error->major_code, (uint32_t) error->minor_code,
				(uint32_t) error->resource_id, (uint32_t) error->sequence);
		} else {
			for(const handler_func_t* handler = handler_funs; handler->func != NULL; handler++)
			{
				if((event->response_type & ~0x80) == handler->request) {
					handler->func(event);
				}
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
