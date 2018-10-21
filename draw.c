#include "dwm.h"
#include <string.h>
#include <stdlib.h>
#include <X11/cursorfont.h>

unsigned int tagwidths[NUM_TAGS];
unsigned int alltagswidth;

void draw_init_font(const char *fontstr) {
	/* init font */
	dc.font.xfont = xcb_generate_id(conn);
	testcookie(xcb_open_font_checked(conn, dc.font.xfont, strlen(fontstr), fontstr));
	
	xcb_query_font_reply_t *fontreply = xcb_query_font_reply(conn, xcb_query_font(conn, dc.font.xfont), &err);
	testerr();

	dc.font.ascent = fontreply->font_ascent;
	dc.font.descent = fontreply->font_descent;
	dc.font.height = dc.font.ascent + dc.font.descent;
	bh = dc.h = dc.font.height + 2;

	free(fontreply);
}

void draw_init_tags() {
	unsigned int* tagwidth = tagwidths;
	for (const char** tag = tags; tag < tags + NUM_TAGS; tag++, tagwidth++)
	{
		*tagwidth = TEXTW(*tag);
		alltagswidth += *tagwidth;
	}
}

void draw_init() {
	draw_init_font(font);

	/* init cursors */
	xcb_font_t cursor_font = xcb_generate_id(conn);
	xcb_open_font(conn, cursor_font, strlen("cursor"), "cursor");

	cursor[CurNormal] = xcb_generate_id(conn);
	xcb_create_glyph_cursor(conn, cursor[CurNormal], cursor_font, cursor_font, XC_left_ptr,
		XC_left_ptr+1, 0, 0, 0, 65535, 63353, 63353);

	cursor[CurResize] = xcb_generate_id(conn);
	xcb_create_glyph_cursor(conn, cursor[CurResize], cursor_font, cursor_font, XC_sizing,
		XC_sizing+1, 0, 0, 0, 65535, 63353, 63353);

	cursor[CurMove] = xcb_generate_id(conn);
	xcb_create_glyph_cursor(conn, cursor[CurMove], cursor_font, cursor_font, XC_fleur,
		XC_fleur+1, 0, 0, 0, 65535, 63353, 63353);

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

	draw_init_tags();
}

int textnw(const char *text, unsigned int len) {
	// FIXME: Handle character encoding.
	xcb_char2b_t *text_copy = (xcb_char2b_t*) malloc(len * 2);
	for(int i = 0; i < len; i++) {
		text_copy[i].byte2 = text[i];
	}

	xcb_query_text_extents_cookie_t cookie = xcb_query_text_extents(conn, dc.gc, len, text_copy);
	xcb_query_text_extents_reply_t *reply = xcb_query_text_extents_reply(conn, cookie, &err);
		
	testerr();

	int width = reply->overall_width;
	free(reply);
	return width;
}

void draw_text(const char *text, uint32_t col[ColLast], bool invert, xcb_window_t w) {
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

void draw_bar(Monitor *m) {
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
		draw_text(tags[i], col, urg & 1 << i, m->barwin);
		if((m == selmon && selmon->sel && selmon->sel->tags & 1 << i) || occ & 1 << i)
			draw_square(m == selmon && selmon->sel && selmon->sel->tags & 1 << i,
		        	   occ & 1 << i, urg & 1 << i, col, m->barwin);
		dc.x += dc.w;
	}
	dc.w = blw = TEXTW(m->ltsymbol);
	draw_text(m->ltsymbol, dc.norm, false, m->barwin);
	dc.x += dc.w;
	x = dc.x;
	if(m == selmon) { /* status is only drawn on selected monitor */
		dc.w = TEXTW(stext);
		dc.x = m->ww - dc.w;
		if(dc.x < x) {
			dc.x = x;
			dc.w = m->ww - x;
		}
		draw_text(stext, dc.norm, false, m->barwin);
	}
	else
		dc.x = m->ww;
	if((dc.w = dc.x - x) > bh) {
		dc.x = x;
		if(m->sel) {
			col = m == selmon ? dc.sel : dc.norm;
			draw_text(m->sel->name, col, false, m->barwin);
			draw_square(m->sel->isfixed, m->sel->isfloating, false, col, m->barwin);
		}
		else
			draw_text(NULL, dc.norm, false, m->barwin);
	}
	xcb_flush(conn);
}

void draw_bars(void) {
	Monitor *m;

	for(m = mons; m; m = m->next)
		draw_bar(m);
}

void draw_square(bool filled, bool empty, bool invert, uint32_t col[ColLast], xcb_window_t w) {
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
