/*
 * Test helper window for sniptotop integration tests.
 *
 * Creates a 200x200 window at 400,300 filled with a solid color.
 * Prints the window ID to stdout on startup.
 *
 * Signals:
 *   SIGUSR1 - cycle fill color (red -> blue -> green -> red)
 *   SIGUSR2 - destroy window and exit
 */

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static xcb_connection_t *conn;
static xcb_window_t win;
static xcb_gcontext_t gc;
static xcb_screen_t *scr;

static volatile sig_atomic_t got_usr1 = 0;
static volatile sig_atomic_t got_usr2 = 0;

static uint32_t colors[] = { 0xff0000, 0x0000ff, 0x00ff00 };
static int color_idx = 0;

static void handle_usr1(int sig) { (void)sig; got_usr1 = 1; }
static void handle_usr2(int sig) { (void)sig; got_usr2 = 1; }

static void fill_window(uint32_t color)
{
	uint32_t vals[1] = { color };
	xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, vals);
	xcb_rectangle_t rect = { 0, 0, 200, 200 };
	xcb_poly_fill_rectangle(conn, win, gc, 1, &rect);
	xcb_flush(conn);
}

int main(void)
{
	int screen_num;
	xcb_generic_event_t *ev;

	signal(SIGUSR1, handle_usr1);
	signal(SIGUSR2, handle_usr2);

	conn = xcb_connect(NULL, &screen_num);
	if (xcb_connection_has_error(conn)) {
		fprintf(stderr, "helper: cannot connect to X\n");
		return 1;
	}

	const xcb_setup_t *setup = xcb_get_setup(conn);
	xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < screen_num; i++)
		xcb_screen_next(&iter);
	scr = iter.data;

	win = xcb_generate_id(conn);
	uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	uint32_t vals[2];
	vals[0] = colors[0];
	vals[1] = XCB_EVENT_MASK_EXPOSURE;
	xcb_create_window(conn, XCB_COPY_FROM_PARENT, win, scr->root,
		400, 300, 200, 200, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT,
		mask, vals);

	/* Set WM_NAME */
	const char *title = "sniptotop-test-target";
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		strlen(title), title);

	/* Set _NET_WM_NAME */
	xcb_intern_atom_cookie_t c1 = xcb_intern_atom(conn, 0,
		strlen("_NET_WM_NAME"), "_NET_WM_NAME");
	xcb_intern_atom_cookie_t c2 = xcb_intern_atom(conn, 0,
		strlen("UTF8_STRING"), "UTF8_STRING");
	xcb_intern_atom_reply_t *r1 = xcb_intern_atom_reply(conn, c1, NULL);
	xcb_intern_atom_reply_t *r2 = xcb_intern_atom_reply(conn, c2, NULL);
	if (r1 && r2) {
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
			r1->atom, r2->atom, 8,
			strlen(title), title);
	}
	free(r1);
	free(r2);

	gc = xcb_generate_id(conn);
	uint32_t gc_vals[1] = { colors[0] };
	xcb_create_gc(conn, gc, win, XCB_GC_FOREGROUND, gc_vals);

	xcb_map_window(conn, win);
	xcb_flush(conn);

	printf("%u\n", win);
	fflush(stdout);

	/* Event loop */
	while (1) {
		if (got_usr2) {
			xcb_destroy_window(conn, win);
			xcb_flush(conn);
			usleep(100000);
			xcb_disconnect(conn);
			return 0;
		}

		if (got_usr1) {
			got_usr1 = 0;
			color_idx = (color_idx + 1) % 3;
			fill_window(colors[color_idx]);
		}

		ev = xcb_poll_for_event(conn);
		if (ev) {
			uint8_t rt = ev->response_type & ~0x80;
			if (rt == XCB_EXPOSE)
				fill_window(colors[color_idx]);
			free(ev);
		}

		usleep(50000); /* 50ms poll */
	}
}
