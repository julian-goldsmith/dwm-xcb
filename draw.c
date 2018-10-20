#include "dwm.h"
#include <string.h>

unsigned int tagwidths[NUM_TAGS];
unsigned int alltagswidth;

void draw_init()
{
	unsigned int* tagwidth = tagwidths;
	for (const char** tag = tags; tag < tags + NUM_TAGS; tag++, tagwidth++)
	{
		*tagwidth = TEXTW(*tag);
		alltagswidth += *tagwidth;
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
