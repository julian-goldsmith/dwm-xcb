#include "dwm.h"
#include <stdio.h>
#include <string.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_event.h>

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

int destroynotify(xcb_generic_event_t *e) {
	Client *c;
	xcb_destroy_notify_event_t *ev = (xcb_destroy_notify_event_t*)e;
	printf("destroynotify\n");

	if((c = client_get_from_window(ev->window)))
		client_unmanage(c, true);

	return 0;
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
			client_update_size_hints(c);
		}
		else if(ev->atom == XCB_ATOM_WM_HINTS)
		{
			updatewmhints(c);
			draw_bars();
		}
		else if(ev->atom == XCB_ATOM_WM_NAME || ev->atom == NetWMName) {
			client_update_title(c);

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

int unmapnotify(xcb_generic_event_t *e) {
	Client *c;
	xcb_unmap_notify_event_t *ev = (xcb_unmap_notify_event_t*)e;

	printf("unmapnotify %i\n", ev->window);
	if((c = client_get_from_window(ev->window)))
		client_unmanage(c, false);

	return 0;
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
			handle_clear_event(XCB_MOTION_NOTIFY);		/* clear out the extras */
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

void resizemouse(const Arg *arg) {
	int ocx, ocy;
	int nw, nh;
	Client *c;
	Monitor *m;
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

	do {
		xcb_generic_event_t *ev = xcb_wait_for_event(conn);

		if(ev->response_type == XCB_MOTION_NOTIFY) {
			xcb_motion_notify_event_t* m_ev = (xcb_motion_notify_event_t*)ev;
			handle_clear_event(XCB_MOTION_NOTIFY);							/* clear out the extras */
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
		} else if(ev->response_type == XCB_BUTTON_RELEASE) {
			done = true;
		} else {
			//xcb_event_handle(handlers, ev);
			// FIXME: do something here
		}

		free(ev);
	} while(!done);

	xcb_warp_pointer(conn, XCB_WINDOW_NONE, c->win, 0, 0, 0, 0, c->w + c->bw - 1, c->h + c->bw - 1);
	xcb_ungrab_pointer(conn, XCB_TIME_CURRENT_TIME);

	handle_clear_event(XCB_ENTER_NOTIFY);

	if((m = ptrtomon(c->x + c->w / 2, c->y + c->h / 2)) != selmon) {
		client_send_to_monitor(c, m);
		selmon = m;
		client_focus(NULL);
	}
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

void view(const Arg *arg) {
	if((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if(arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	arrange(selmon);
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

void handle_event(xcb_generic_event_t* event) {
	for(const handler_func_t* handler = handler_funs; handler->func != NULL; handler++) {
		if((event->response_type & ~0x80) == handler->request) {
			handler->func(event);
		}
	}	
}

void handle_event_loop() {
	xcb_generic_event_t *event;
	
	while ((event = xcb_wait_for_event(conn))) {
		if (event->response_type == 0) {
			xcb_generic_error_t *error = (xcb_generic_error_t*) event;

			fprintf(stderr, "previous request returned error %i, \"%s\" major code %i, minor code %i resource %i seq number %i\n",
				(int)error->error_code, xcb_event_get_error_label(error->error_code),
				(uint32_t) error->major_code, (uint32_t) error->minor_code,
				(uint32_t) error->resource_id, (uint32_t) error->sequence);
		} else {
			handle_event(event);
		}

		free(event);
	}
}

void handle_clear_event(int response_type) {
	xcb_generic_event_t *ev;

	while((ev = xcb_poll_for_event(conn))) {
		if(ev->response_type == response_type) {
			free(ev);
			return;
		} else {
			handle_event(ev);
		}
			
		free(ev);
	}
}
