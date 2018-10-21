#include "dwm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb_icccm.h>

bool client_apply_size_hints(Client *c, int *x, int *y, int *w, int *h, bool interact) {
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
	if(c->isfloating) {
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

void client_attach(Client *c) {
	c->next = c->mon->clients;
	c->mon->clients = c;
}

void client_attach_stack(Client *c) {
	c->snext = c->mon->stack;
	c->mon->stack = c;
}

void client_clear_urgent(Client *c) {
	xcb_icccm_wm_hints_t wmh;
	xcb_get_property_cookie_t wmh_cookie;

	wmh_cookie = xcb_icccm_get_wm_hints_unchecked(conn, c->win);
	c->isurgent = false;
	if(!xcb_icccm_get_wm_hints_reply(conn, wmh_cookie, &wmh, NULL))
		return;
	wmh.flags &= ~XCB_ICCCM_WM_HINT_X_URGENCY;
	xcb_icccm_set_wm_hints(conn, c->win, &wmh);
}

void client_configure(Client *c) {
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

void client_detach(Client *c) {
	Client **tc;

	for(tc = &c->mon->clients; *tc && *tc != c; tc = &(*tc)->next);
	*tc = c->next;
}

void client_detach_stack(Client *c) {
	Client **tc, *t;

	for(tc = &c->mon->stack; *tc && *tc != c; tc = &(*tc)->snext);
	*tc = c->snext;

	if(c == c->mon->sel) {
		for(t = c->mon->stack; t && !ISVISIBLE(t); t = t->snext);
		c->mon->sel = t;
	}
}

void client_focus(Client *c) {
	if(!c || !ISVISIBLE(c)) {
		for(c = selmon->stack; c && !ISVISIBLE(c); c = c->snext);
	}

	if(selmon->sel) {
		client_unfocus(selmon->sel, false);
	}

	if(c) {
		if(c->mon != selmon) {
			selmon = c->mon;
		}

		if(c->isurgent) {
			client_clear_urgent(c);
		}

		client_detach_stack(c);
		client_attach_stack(c);
		grabbuttons(c, true);
		xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, (uint32_t*)&dc.sel[ColBorder]);
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
	}
	else {
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, root, XCB_CURRENT_TIME);
	}

	selmon->sel = c;
	draw_bars();
}

void client_unfocus(Client *c, bool setfocus) {
	if(!c)
		return;
	grabbuttons(c, false);
	xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, 
		(uint32_t*)&dc.norm[ColBorder]);
	if(setfocus)
		xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, 
			c->win, XCB_CURRENT_TIME);
}

void client_show_hide(Client *c) {
	if(!c)
		return;
	if(ISVISIBLE(c)) { /* show clients top down */
		uint32_t values[] = { c->x, c->y };
		xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
		if(!c->mon->lt[c->mon->sellt]->arrange || c->isfloating)
			client_resize(c, c->x, c->y, c->w, c->h, false);
		client_show_hide(c->snext);
	}
	else { /* hide clients bottom up */
		client_show_hide(c->snext);
		uint32_t values[] = { c->x + 2 * sw, c->y };
		xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
	}
}

void client_resize(Client *c, int x, int y, int w, int h, bool interact) {
	if(client_apply_size_hints(c, &x, &y, &w, &h, interact))
		client_resize_client(c, x, y, w, h);
}

void client_resize_client(Client *c, int x, int y, int w, int h) {
	uint32_t values[] = { x, y, w, h, c->bw };
	xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | 
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
		values);
	c->oldx = c->x; c->x = x;
	c->oldy = c->y; c->y = y;
	c->oldw = c->w; c->w = w;
	c->oldh = c->h; c->h = h;
	client_configure(c);
	xcb_flush(conn);
}

void client_set_state(Client *c, long state) {
	long data[] = { state, XCB_ATOM_NONE };

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, c->win, WMState,
		WMState, 32, 2, (unsigned char*)data);
}

Client* client_next_tiled(Client *c) {
	for(; c && (c->isfloating || !ISVISIBLE(c)); c = c->next);
	return c;
}

bool client_is_proto_del(Client *c) {
	int i;
	bool ret = false;
	xcb_icccm_get_wm_protocols_reply_t proto_reply;

	if(xcb_icccm_get_wm_protocols_reply(conn, xcb_icccm_get_wm_protocols_unchecked(conn, c->win, WMProtocols), &proto_reply, NULL)) {
		for(i = 0; !ret && i < proto_reply.atoms_len; i++)
			if(proto_reply.atoms[i] == WMDelete)
				ret = true;
		xcb_icccm_get_wm_protocols_reply_wipe(&proto_reply);
	}
	return ret;
}

void client_unmanage(Client *c, bool destroyed) {
	Monitor *m = c->mon;

	/* The server grab construct avoids race conditions. */
	client_detach(c);
	client_detach_stack(c);
	printf("unmanage %i, %i\n", destroyed, c->win);
	if(!destroyed) {
		uint32_t values[] = { c->oldbw };
		xcb_grab_server(conn);
		xcb_configure_window_checked(conn, c->win, 
			XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
		xcb_ungrab_button_checked(conn, XCB_BUTTON_INDEX_ANY, c->win, 
			XCB_GRAB_ANY);
		client_set_state(c, XCB_ICCCM_WM_STATE_WITHDRAWN);
		xcb_flush(conn);
		xcb_ungrab_server(conn);
	}
	free(c);
	client_focus(NULL);
	arrange(m);
}

void client_send_to_monitor(Client *c, Monitor *m) {
	if(c->mon == m)
		return;
	client_unfocus(c, true);
	client_detach(c);
	client_detach_stack(c);
	c->mon = m;
	c->tags = m->tagset[m->seltags]; /* assign tags of target monitor */
	client_attach(c);
	client_attach_stack(c);
	client_focus(NULL);
	arrange(NULL);
}

Client* client_get_from_window(xcb_window_t w) {
	Client *c;
	Monitor *m;

	for(m = mons; m; m = m->next)
		for(c = m->clients; c; c = c->next)
			if(c->win == w)
				return c;
	return NULL;
}
