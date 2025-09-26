#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <xcb/xcb.h>
#include <xcb/damage.h>
#include <xcb/xproto.h>
#include <xcb/xcb_icccm.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <yaml.h>
#include <assert.h>
#include <sys/time.h>

int debug = 0;

const char *program_name = "sniptotop";
const char *class_name = "sniptotop;SnipToTop";
int border_width = 2;

/* global variables */
xcb_connection_t *c;
xcb_screen_t *screen;
int damage_notify_event;
xcb_window_t top_window;
static xcb_font_t cursor_font;

int nwindows = 0;
#define MAX_WINDOWS 1000
typedef enum {
	WIN_TYPE_TOP,
	WIN_TYPE_VIEW,
	WIN_TYPE_TARGET,
} win_type_t;
struct {
	xcb_window_t window;
	win_type_t type;
	void *ctx;
} windows[MAX_WINDOWS];

struct view_ctx;
typedef struct {
	xcb_window_t target;
	xcb_window_t wm_target;
	struct view_ctx *first_view;
	xcb_damage_damage_t damage;
	char *name;
} target_ctx_t;

typedef struct view_ctx {
	target_ctx_t *t;
	xcb_window_t window;
	xcb_gcontext_t gc;
	int cap_x;
	int cap_y;
	int cap_width;
	int cap_height;
	int button3_pressed;
	int move_offset_x;
	int move_offset_y;
	struct view_ctx *next_view;
} view_ctx_t;

typedef enum {
	TST_IDLE,
	TST_PRE_SELECT,
	TST_SELECT,
} top_state_t;

typedef struct {
	xcb_window_t window;
	xcb_gcontext_t gc;
	top_state_t state;
	xcb_window_t sel_target;
	xcb_window_t sel_wm_target;
	char *sel_name;
	int sel_x1;
	int sel_y1;
	int sel_x2;
	int sel_y2;
} top_ctx_t;

typedef struct {
    uint32_t   flags;
    uint32_t   functions;
    uint32_t   decorations;
    int32_t    input_mode;
    uint32_t   status;
} motif_hints_t;

xcb_atom_t get_atom(xcb_connection_t *c, const char *name);
xcb_window_t find_wm_window(xcb_window_t win);

void
deb(const char *msg, ...)
{
	struct timeval tv;
	struct tm tm;
	if (!debug)
		return;
	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &tm);
	printf("%02d:%02d:%02d.%06ld ",
		tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec);
	va_list args;
	va_start (args, msg);
	vprintf(msg, args);
	va_end(args);
	fflush(stdout);
}

void
fail(const char *msg, ...)
{
	va_list args;
	va_start (args, msg);
	vfprintf(stderr, msg, args);
	va_end(args);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

int
add_window(xcb_window_t w, win_type_t type, void *ctx)
{
	if (nwindows >= MAX_WINDOWS)
		fail("Too many windows");
	windows[nwindows].window = w;
	windows[nwindows].type = type;
	windows[nwindows].ctx = ctx;
	nwindows++;
	return 0;
}

int
rem_window(xcb_window_t w)
{
	int i;

	for (i = 0; i < nwindows; i++) {
		if (windows[i].window == w) {
			break;
		}
	}
	if (i == nwindows)
		fail("rem_window: window 0x%x not found", w);
	windows[i] = windows[nwindows - 1];
	nwindows--;
	return 0;
}

int
find_window(xcb_window_t win)
{
	for (int i = 0; i < nwindows; i++) {
		if (windows[i].window == win) {
			deb("found window 0x%x type %d\n",
				win, windows[i].type);
			return i;
		}
	}
	return -1;
}

xcb_cursor_t
get_cursor(uint16_t ch)
{
	xcb_cursor_t cursor = xcb_generate_id(c);
	xcb_create_glyph_cursor(c, cursor, cursor_font, cursor_font,
		ch, ch + 1, 0, 0, 0, 0xffff, 0xffff, 0xffff);

	return cursor;
}

/*
 * find the subwindow in which WM_STATE property is set
 */
xcb_window_t
find_wm_window(xcb_window_t win)
{
	xcb_generic_error_t *err;
	xcb_atom_t atom_wm_state = get_atom(c, "WM_STATE");
	xcb_get_property_cookie_t prop_cookie;
	xcb_get_property_reply_t *prop_reply;
	xcb_window_t *children;

	prop_cookie = xcb_get_property(c, 0, win, atom_wm_state,
		XCB_ATOM_ANY, 0, 0);
	prop_reply = xcb_get_property_reply(c, prop_cookie, &err);
	if (prop_reply) {
		xcb_atom_t reply_type = prop_reply->type;
		free (prop_reply);
		if (reply_type != XCB_NONE) {
			deb("found WM_STATE property on window 0x%x\n", win);
			return win;
		}
	}

	xcb_query_tree_cookie_t tree_cookie;
	xcb_query_tree_reply_t *tree_reply;

	tree_cookie = xcb_query_tree(c, win);
	tree_reply = xcb_query_tree_reply(c, tree_cookie, &err);
	if (!tree_reply) {
		deb("Failed to query tree\n");
		return XCB_WINDOW_NONE;
	}
	deb("window 0x%x has %d children\n", win, tree_reply->children_len);
	int children_len = xcb_query_tree_children_length(tree_reply);
	if (children_len == 0) {
		free(tree_reply);
		return XCB_WINDOW_NONE; // no children
	}
	children = xcb_query_tree_children(tree_reply);
	if (children == NULL) {
		free(tree_reply);
		return XCB_WINDOW_NONE;
	}
	for (int i = 0; i < children_len; i++) {
		deb("child %d: 0x%x\n", i, children[i]);
		win = find_wm_window(children[i]);
		if (win != XCB_WINDOW_NONE)
			break;
	}
	free(tree_reply);

	return win;
}

xcb_atom_t
get_atom(xcb_connection_t *c, const char *name)
{
	xcb_generic_error_t *e;
	xcb_atom_t a;

	xcb_intern_atom_cookie_t cookie = xcb_intern_atom(c, 0,
		strlen(name), name);
	xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, cookie, &e);

	a = r->atom;

	free(r);

	return a;
}

void
initialize_xcb(void)
{
	const xcb_setup_t *setup;

	c = xcb_connect(NULL, NULL);
	if (!c)
		fail("Could not open Display\n");

	setup  = xcb_get_setup(c);
	screen = xcb_setup_roots_iterator(setup).data;

	cursor_font = xcb_generate_id(c);
	xcb_open_font(c, cursor_font, 6, "cursor");
}

void
initialize_xdamage(void)
{
	xcb_generic_error_t *err;
	xcb_query_extension_cookie_t qe_c;
	xcb_query_extension_reply_t *qe_r;
	xcb_damage_query_version_cookie_t dv_c;
	xcb_damage_query_version_reply_t *dv_r;

	// Check for XDamage extension
	char *ext_name = "DAMAGE";
	qe_c = xcb_query_extension(c, strlen(ext_name), ext_name);
	qe_r = xcb_query_extension_reply(c, qe_c, &err);
	if (!qe_r || !qe_r->present) {
		fail("XDamage extension not supported by X server, "
			"fallback possible but not yet implemented\n");
	}
	deb("damage extension supported, major opcode %d, "
		"first event %d, first error %d\n",
		qe_r->major_opcode, qe_r->first_event, qe_r->first_error);
	damage_notify_event = qe_r->first_event;

	// Version handshake
	dv_c = xcb_damage_query_version(c, 1, 1);
	dv_r = xcb_damage_query_version_reply(c, dv_c, &err);
	if (!dv_r) {
		fail("XDamage extension not supported by X server, "
			"fallback possible but not yet implemented\n");
	}
	deb("damage extension supported, version %d.%d\n",
		dv_r->major_version, dv_r->minor_version);
}

void
initialize_top_window(void)
{
	uint32_t mask;
	uint32_t values[20];

	top_window = xcb_generate_id(c);
	mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	values[0] = screen->white_pixel;
	values[1] = XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE;
	xcb_create_window(c, XCB_COPY_FROM_PARENT, top_window,
		screen->root, 0, 0,
		150, 150,
		0,
		InputOutput, XCB_COPY_FROM_PARENT,
		mask, values
	);

	xcb_change_property(c, XCB_PROP_MODE_REPLACE, top_window,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		strlen(program_name), program_name);

	char cn[100];
	strcpy(cn, class_name);
	int class_len = strlen(class_name);
	*strchr(cn, ';') = '\0';

	xcb_change_property(c, XCB_PROP_MODE_REPLACE, top_window,
		XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
		class_len + 1, cn);


	xcb_gcontext_t gc = xcb_generate_id(c);
	values[0] = screen->black_pixel;
	values[1] = screen->white_pixel;
	xcb_create_gc(c, gc, top_window,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
		values);

	xcb_map_window(c, top_window);

	top_ctx_t *t = malloc(sizeof(top_ctx_t));
	t->window = top_window;
	t->gc = gc;
	t->state = TST_IDLE;
	add_window(top_window, WIN_TYPE_TOP, t);
}

int
create_view(xcb_window_t window, xcb_window_t wm_window, char *name,
	int x1, int y1, int x2, int y2)
{
	xcb_generic_error_t *err;
	xcb_get_window_attributes_cookie_t attr_cookie;
	xcb_get_window_attributes_reply_t *win_attrs;
	xcb_get_geometry_cookie_t geom_cookie;
	xcb_get_geometry_reply_t *win_geom;
	int cap_width;
	int cap_height;
	int cap_x;
	int cap_y;
	int mask;
	uint32_t values[20];
	int t_ix;
	target_ctx_t *t;
	xcb_atom_t atom_motif_hints = get_atom(c, "_MOTIF_WM_HINTS");

	attr_cookie = xcb_get_window_attributes(c, window);
	win_attrs = xcb_get_window_attributes_reply(c, attr_cookie, &err);
	if (!win_attrs) {
		deb("Failed to get window attributes\n");
		return 1;
	}

	geom_cookie = xcb_get_geometry(c, window);
	win_geom = xcb_get_geometry_reply(c, geom_cookie, &err);
	if (!win_geom) {
		deb("Failed to get window geometry\n");
		return 1;
	}

	deb("Window Geometry: x=%d, y=%d, width=%d, height=%d "
		"border width %d depth %d\n",
		win_geom->x, win_geom->y, win_geom->width, win_geom->height,
		win_geom->border_width, win_geom->depth);

	cap_width = x2 - x1;
	cap_height = y2 - y1;
	cap_x = x1 - win_geom->x;
	cap_y = y1 - win_geom->y;

	uint32_t black = 0xff000000;
	uint32_t grey = 0xff808080;
	deb("black: 0x%08x white: 0x%08x\n",
		screen->black_pixel, screen->white_pixel);
	xcb_window_t new_window = xcb_generate_id(c);
	mask = XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL |
		XCB_CW_OVERRIDE_REDIRECT |
		XCB_CW_EVENT_MASK |
		XCB_CW_COLORMAP;
	values[0] = black;
	values[1] = black;
	values[2] = 0; // override-redirect
	values[3] = XCB_EVENT_MASK_EXPOSURE |
		XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_KEY_PRESS |
		XCB_EVENT_MASK_BUTTON_3_MOTION;
	values[4] = win_attrs->colormap;
	int n_border_width = 2;
	int width = cap_width + 2 * n_border_width;
	int height = cap_height + 2 * n_border_width;
	int x = 500;
	int y = 500;
	xcb_create_window(c, win_geom->depth, new_window,
		screen->root, x, y,
		width,
		height,
		0, // border width
		InputOutput, win_attrs->visual,
		mask, values
	);
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, new_window,
		XCB_ATOM_WM_TRANSIENT_FOR, XCB_ATOM_WINDOW, 32, 1,
		&top_window);
	values[0] = get_atom(c, "_NET_WM_STATE_ABOVE");
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, new_window,
		get_atom(c, "_NET_WM_STATE"), XCB_ATOM_ATOM, 32, 1,
		&values);

	xcb_size_hints_t hints;

	xcb_icccm_size_hints_set_min_size(&hints, width, height);
	xcb_icccm_size_hints_set_max_size(&hints, width, height);

	xcb_icccm_set_wm_size_hints(c, new_window, XCB_ATOM_WM_NORMAL_HINTS,
		&hints);

	motif_hints_t m_hints;

	m_hints.flags = 2;
	m_hints.functions = 0;
	m_hints.decorations = 0;
	m_hints.input_mode = 0;
	m_hints.status = 0;

	xcb_change_property (c,
		XCB_PROP_MODE_REPLACE,
		new_window,
		atom_motif_hints,
		atom_motif_hints,
		32,
		5,
		&m_hints);

	xcb_map_window(c, new_window);

	xcb_gcontext_t copy_gc = xcb_generate_id(c);
	values[0] = grey;
	values[1] = black;
	values[2] = XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS;
	xcb_create_gc(c, copy_gc, window,
		XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_SUBWINDOW_MODE,
		values);

	view_ctx_t *v = calloc(sizeof(view_ctx_t), 1);
	v->window = new_window;
	v->gc = copy_gc;
	v->cap_x = cap_x;
	v->cap_y = cap_y;
	v->cap_width = cap_width;
	v->cap_height = cap_height;
	v->button3_pressed = 0;
	v->move_offset_x = 0;
	v->move_offset_y = 0;
	add_window(new_window, WIN_TYPE_VIEW, v);

	t_ix = find_window(window);
	if (t_ix == -1) {
		values[0] = XCB_EVENT_MASK_STRUCTURE_NOTIFY |
			XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
		xcb_change_window_attributes(c, window, XCB_CW_EVENT_MASK,
			values);

		 // Create a damage object
		xcb_damage_damage_t damage = xcb_generate_id(c);
		xcb_damage_create(c, damage, window,
			XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);

		t = calloc(sizeof(target_ctx_t), 1);
		t->target = window;
		t->wm_target = wm_window;
		t->damage = damage;
		t->name = name;
		add_window(window, WIN_TYPE_TARGET, t);
	} else {
		t = windows[t_ix].ctx;
		assert(t->wm_target == wm_window);
		free(name); // we already have a name
	}
	v->t = t;
	v->next_view = t->first_view;
	t->first_view = v;

	return 0;
}

void
destroy_view(view_ctx_t *v)
{
	target_ctx_t *t = v->t;
	view_ctx_t *prev = NULL;
	view_ctx_t *cur = t->first_view;

	// remove from target's view list
	while (cur != NULL) {
		if (cur == v) {
			if (prev)
				prev->next_view = cur->next_view;
			else
				t->first_view = cur->next_view;
			break;
		}
		prev = cur;
		cur = cur->next_view;
	}
	if (!cur)
		fail("internal error, view not found in target's list");

	xcb_free_gc(c, v->gc);
	rem_window(v->window);
	xcb_destroy_window(c, v->window);

	// if no more views for this target, free target as well
	if (t->first_view == NULL) {
		uint32_t eventmask = 0;
		xcb_change_window_attributes(c, t->target, XCB_CW_EVENT_MASK,
			&eventmask);
		deb("No more views for target window 0x%x\n", t->target);
		free(t->name);
		xcb_damage_destroy(c, t->damage);
		rem_window(t->target);
		free(t);
	}
	free(v);
}

void
redraw_view(view_ctx_t *v)
{
	xcb_void_cookie_t v_cookie;
	xcb_generic_error_t *error;

	deb("Redrawing view window 0x%x from target 0x%x "
		"capture area %d,%d %dx%d\n",
		v->window, v->t->target,
		v->cap_x, v->cap_y,
		v->cap_width, v->cap_height);
	v_cookie = xcb_copy_area_checked(c,
		v->t->target,
		v->window,
		v->gc,
		v->cap_x, v->cap_y,
		border_width, border_width,
		v->cap_width, v->cap_height);

	if ((error = xcb_request_check(c, v_cookie))) {
		fail("Error code %d major %d minor %d\n",
			error->error_code,
			error->major_code,
			error->minor_code);
	}
}

void
handle_top_event(xcb_generic_event_t *e, void *ctx)
{
	top_ctx_t *t = ctx;
	xcb_generic_error_t *err;
	xcb_grab_pointer_cookie_t gr_c;
	xcb_grab_pointer_reply_t *gr_r;

	int rt = e->response_type & ~0x80;

	if (rt == XCB_EXPOSE) {
		xcb_expose_event_t *ev = (void *)e;

		deb("Top window exposed. Region to be redrawn at "
			"location (%d,%d), with dimension (%d,%d)\n",
		ev->x, ev->y, ev->width, ev->height);

		int dim = ev->width < ev->height ?
			ev->width : ev->height;
		dim = dim / 4;
		xcb_point_t cross_1[2] = {
			{.x = dim, .y = 2 * dim},
			{.x = 3 * dim, .y = 2 * dim},
		};
		xcb_point_t cross_2[2] = {
			{.x = 2 * dim, .y = dim},
			{.x = 2 * dim, .y = 3 * dim},
		};
		xcb_poly_line(c, XCB_COORD_MODE_ORIGIN,
			t->window, t->gc, 2, cross_1);
		xcb_poly_line(c, XCB_COORD_MODE_ORIGIN,
			t->window, t->gc, 2, cross_2);
		xcb_arc_t circle = {
			.x = dim - dim / 6,
			.y = dim - dim / 6,
			.width = 2 * dim + dim / 3,
			.height = 2 * dim + dim / 3,
			.angle1 = 0,
			.angle2 = 360 * 64,
		};
		xcb_poly_arc(c, t->window, t->gc, 1, &circle);
	} else if (t->state == TST_IDLE && rt == XCB_BUTTON_PRESS) {
		t->state = TST_PRE_SELECT;
	} else if (t->state == TST_PRE_SELECT && rt == XCB_BUTTON_RELEASE) {
		xcb_cursor_t cursor;

		deb("Starting window selection\n");
		t->state = TST_SELECT;

		cursor = get_cursor(XC_crosshair);
		gr_c = xcb_grab_pointer(c, False, screen->root,
			 XCB_EVENT_MASK_BUTTON_PRESS |
			 XCB_EVENT_MASK_BUTTON_RELEASE |
			 XCB_EVENT_MASK_BUTTON_MOTION,
			 XCB_GRAB_MODE_SYNC,
			 XCB_GRAB_MODE_ASYNC,
			 screen->root,
			 cursor,
			 XCB_TIME_CURRENT_TIME);
		gr_r = xcb_grab_pointer_reply (c, gr_c, &err);
		if (gr_r->status != XCB_GRAB_STATUS_SUCCESS)
			fail("grabbing mouse failed");
		/*
		 * events will be delivered for the root window,
		 * so add it here as well
		 */
		add_window(screen->root, WIN_TYPE_TOP, t);
	} else if (t->state == TST_SELECT && rt == XCB_BUTTON_PRESS) {
		xcb_button_press_event_t *bp = (void *)e;
		xcb_cursor_t cursor;

		if (bp->detail == XCB_BUTTON_INDEX_1) {
			deb("button press, root %d child %d root_xy %d,%d\n",
				bp->root, bp->child, bp->root_x, bp->root_y);
			t->sel_x1 = bp->root_x;
			t->sel_y1 = bp->root_y;

			t->sel_target = bp->child; /* window selected */
			if (t->sel_target == XCB_WINDOW_NONE ||
			    t->sel_target == screen->root) {
				t->state = TST_IDLE;
				return;
			}
			t->sel_wm_target = find_wm_window(t->sel_target);
			if (t->sel_wm_target != XCB_WINDOW_NONE) {
				deb("raise window 0x%x\n",
					t->sel_wm_target);
				uint32_t values[1];
				values[0] = XCB_STACK_MODE_ABOVE;
				xcb_configure_window(c, t->sel_wm_target,
					XCB_CONFIG_WINDOW_STACK_MODE, values);
			}
			/*
			 * get window name
			 */
			xcb_get_property_cookie_t pr_c;
			xcb_get_property_reply_t *pr_r;
			xcb_atom_t atom_wm_name = get_atom(c, "WM_NAME");
			pr_c = xcb_get_property(c, 0, t->sel_wm_target,
				atom_wm_name, XCB_ATOM_ANY, 0, 100);
			pr_r = xcb_get_property_reply(c, pr_c, &err);
			if (!pr_r) {
				deb("Failed to get window name\n");
				t->state = TST_IDLE;
				return;
			}
			int len = xcb_get_property_value_length(pr_r);
			if (len == 0) {
				deb("Window with no name\n");
				t->state = TST_IDLE;
				return;
			}

			char *name = malloc(len + 1);
			memcpy(name, xcb_get_property_value(pr_r), len);
			name[len] = '\0';
			t->sel_name = name;
			deb("Selected window 0x%x name '%s'\n",
				t->sel_target, name);
			free (pr_r);

			/*
			 * regrab the pointer, now confining it to the
			 * target window
			 */
			xcb_ungrab_pointer(c, XCB_TIME_CURRENT_TIME);
			cursor = get_cursor(XC_crosshair);
			gr_c = xcb_grab_pointer(c, False, screen->root,
				 XCB_EVENT_MASK_BUTTON_PRESS |
				 XCB_EVENT_MASK_BUTTON_RELEASE |
				 XCB_EVENT_MASK_BUTTON_MOTION,
				 XCB_GRAB_MODE_SYNC,
				 XCB_GRAB_MODE_ASYNC,
				 t->sel_target,
				 cursor,
				 XCB_TIME_CURRENT_TIME);
			gr_r = xcb_grab_pointer_reply (c, gr_c, &err);
			if (gr_r->status != XCB_GRAB_STATUS_SUCCESS)
				fail("grabbing mouse failed");
#if 0
			xcb_window_t overlay = xcb_generate_id(dpy);
			uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT |
				XCB_CW_EVENT_MASK;
			uint32_t values[3];
			values[0] = screen->black_pixel;
			values[1] = 1; // override-redirect
			values[2] = XCB_EVENT_MASK_EXPOSURE;
			xcb_create_window(dpy, XCB_COPY_FROM_PARENT, overlay,
				target_win, 0, 0,
				100, 100,
				0,
				InputOutput, XCB_COPY_FROM_PARENT,
				mask, values
			);
#endif
		}
	} else if (t->state == TST_SELECT && rt == XCB_BUTTON_RELEASE) {
	       xcb_button_release_event_t *br = (void *)e;
	       int ret;

		if (br->detail == XCB_BUTTON_INDEX_1) {
			t->sel_x2 = br->root_x;
			t->sel_y2 = br->root_y;
			xcb_ungrab_pointer(c, XCB_TIME_CURRENT_TIME);
			t->state = TST_IDLE;
			if (t->sel_x2 < t->sel_x1) {
				int tmp = t->sel_x1;
				t->sel_x1 = t->sel_x2;
				t->sel_x2 = tmp;
			}
			if (t->sel_y2 < t->sel_y1) {
				int tmp = t->sel_y1;
				t->sel_y1 = t->sel_y2;
				t->sel_y2 = tmp;
			}
			ret = create_view(t->sel_target, t->sel_wm_target,
				t->sel_name,
				t->sel_x1, t->sel_y1, t->sel_x2, t->sel_y2);
			if (ret != 0)
				fail("Failed to create view\n");
		}
	} else if (t->state == TST_SELECT && rt == XCB_MOTION_NOTIFY) {
	       xcb_motion_notify_event_t *mv = (void *)e;
	       deb("motion notify, root %d event %d root_xy %d,%d\n",
		       mv->root, mv->event, mv->root_x, mv->root_y);
	       /* draw selection rectangle */
	}

	if (t->state == TST_SELECT)
		xcb_allow_events(c, XCB_ALLOW_SYNC_POINTER,
			XCB_TIME_CURRENT_TIME);
}

void
handle_view_event(xcb_generic_event_t *e, void *ctx)
{
	view_ctx_t *v = ctx;
	int rt = e->response_type & ~0x80;

	if (rt == XCB_EXPOSE) {
		xcb_expose_event_t *ev = (void *)e;

		deb("Window 0x%x exposed. Region to be redrawn at "
			"location (%d,%d), with dimension (%d,%d)\n",
		ev->window, ev->x, ev->y, ev->width, ev->height);

		redraw_view(v);
	} else if (rt == XCB_GRAPHICS_EXPOSURE) {
		xcb_graphics_exposure_event_t *ev = (void *)e;

		deb("Window 0x%x exposed. Region to be redrawn at "
			"location (%d,%d), with dimension (%d,%d)\n",
		ev->drawable, ev->x, ev->y, ev->width, ev->height);

		redraw_view(v);
	} else if (rt == XCB_BUTTON_PRESS) {
		xcb_button_press_event_t *bp = (void *)e;
		deb("button press event, detail %d\n", bp->detail);
		if (bp->detail == XCB_BUTTON_INDEX_1) {
			deb("raise window 0x%x\n", v->window);
			/* left button, raise target window */
			uint32_t values[1];
			values[0] = XCB_STACK_MODE_ABOVE;
			xcb_configure_window(c, v->t->wm_target,
				XCB_CONFIG_WINDOW_STACK_MODE, values);
		}
		if (bp->detail == XCB_BUTTON_INDEX_3) {
			v->button3_pressed = 1;
			v->move_offset_x = bp->event_x;
			v->move_offset_y = bp->event_y;
			deb("button 3 pressed\n");
		}
	} else if (rt == XCB_BUTTON_RELEASE) {
		xcb_button_release_event_t *br = (void *)e;
		deb("button release event, detail %d\n", br->detail);
		if ((br->detail == XCB_BUTTON_INDEX_3) &&
		    v->button3_pressed) {
			deb("button 3 released\n");
			v->button3_pressed = 0;
		}

	} else if (rt == XCB_MOTION_NOTIFY) {
		int values[2];
		xcb_motion_notify_event_t *mv = (void *)e;

		deb("motion notify event at root %d,%d event %d,%d state %d\n",
			mv->root_x, mv->root_y, mv->event_x, mv->event_y,
			mv->state);
		values[0] = mv->root_x - v->move_offset_x;
		values[1] = mv->root_y - v->move_offset_y;
		xcb_configure_window (c, v->window,
			XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
			values);
	} else if (rt == XCB_KEY_PRESS) {
		xcb_key_press_event_t *kp = (void *)e;
		deb("key press event, detail %d\n", kp->detail);
		// Escape or backspace or del
		if (kp->detail == 9 || kp->detail == 22 ||
		    kp->detail == 119) {
			deb("Escape pressed, closing view\n");
			destroy_view(v);
		}
	} else {
		deb("view: discarding event type %d\n", rt);
	}
}

void
handle_target_event(xcb_generic_event_t *e, void *ctx)
{
	target_ctx_t *t = ctx;
	view_ctx_t *v;
	int rt = e->response_type & ~0x80;

	if (rt == damage_notify_event) {
		xcb_damage_notify_event_t *dev = (void *)e;

		deb("got damage notify: drawable 0x%x level %d "
			"area x %d y %d width %d height%d\n",
			dev->drawable, dev->level, dev->area.x, dev->area.y,
			dev->area.width, dev->area.height);
		xcb_damage_subtract(c, t->damage, None, None);

		for (v = t->first_view; v != NULL; v = v->next_view) {
			/*
			 * check if damage lies within our capture area
			 */
			if ( (dev->area.x + dev->area.width < v->cap_x) ||
			     (dev->area.x > v->cap_x + v->cap_width) ||
			     (dev->area.y + dev->area.height < v->cap_y) ||
			     (dev->area.y > v->cap_y + v->cap_height) ) {
				deb("damage outside capture area, ignoring\n");
				continue;
			}
			redraw_view(v);
		}
	} else if (rt == XCB_UNMAP_NOTIFY) {
		xcb_unmap_notify_event_t *um = (void *)e;
		if (um->window == t->target) {
			deb("target window unmapped, blanking view\n");
			for (v = t->first_view; v != NULL; v = v->next_view) {
				// fill with grey (gc foreground)
				xcb_rectangle_t r = {
					.x = border_width,
					.y = border_width,
					.width = v->cap_width,
					.height = v->cap_height,
				};
				xcb_poly_fill_rectangle(c, v->window,
					v->gc, 1, &r);
			}
		} else {
			deb("ignoring unmap notify for window 0x%x "
				"event 0x%x, my window is 0x%x\n",
				um->window, um->event, t->target);
		}
	}
}

void
handle_event(xcb_generic_event_t *e)
{
	xcb_window_t win;
	int i;

	int rt = e->response_type & ~0x80;
	/* extract window from event */
	if (rt == XCB_EXPOSE) {
		win = ((xcb_expose_event_t *)e)->window;
	} else if (rt == XCB_GRAPHICS_EXPOSURE) {
		win = ((xcb_graphics_exposure_event_t *)e)->drawable;
	} else if (rt == XCB_BUTTON_PRESS) {
		win = ((xcb_button_press_event_t *)e)->event;
	} else if (rt == XCB_KEY_PRESS) {
		win = ((xcb_key_press_event_t *)e)->event;
	} else if (rt == XCB_BUTTON_RELEASE) {
		win = ((xcb_button_release_event_t *)e)->event;
	} else if (rt == XCB_MOTION_NOTIFY) {
		win = ((xcb_motion_notify_event_t *)e)->event;
	} else if (rt == XCB_UNMAP_NOTIFY) {
		win = ((xcb_unmap_notify_event_t *)e)->event;
		deb("unmap notify for window 0x%x event 0x%x\n", win,
			((xcb_unmap_notify_event_t *)e)->event);
	} else if (rt == damage_notify_event) {
		win = ((xcb_damage_notify_event_t *)e)->drawable;
	} else {
		deb("unhandled event type %d\n", rt);
		return;
	}

	i = find_window(win);
	if (i < 0)
		fail("event for unknown window 0x%x\n", win);

	switch (windows[i].type) {
	case WIN_TYPE_TOP:
		handle_top_event(e, windows[i].ctx);
		break;
	case WIN_TYPE_VIEW:
		handle_view_event(e, windows[i].ctx);
		break;
	case WIN_TYPE_TARGET:
		handle_target_event(e, windows[i].ctx);
		break;
	default:
		fail("internal error, window %d has unknown type %d\n",
			i, windows[i].type);
	}
}

int
main(int argc, char **argv)
{
	xcb_generic_event_t *e;
	int opt;
	while ((opt = getopt(argc, argv, "d")) != -1) {
		switch (opt) {
		case 'd':
			debug = 1;
			break;
		default:
			fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
			exit(EXIT_FAILURE);
		}
	}

	printf("\nTo add snips, click the large \"plus\" in the main window.\n"
	       "Then select a window by clicking on it.\n"
	       "Then drag a rectangle with the left mouse button.\n"
	       "To move a snip, hold down the right mouse button and drag.\n"
	       "To close a snip, focus it and press escape.\n");

	initialize_xcb();
	initialize_xdamage();
	initialize_top_window();

	/* main loop */
	xcb_flush(c);
	while ((e = xcb_wait_for_event(c))) {
		deb("got event, response_type %d\n", e->response_type);
		handle_event(e);
		free(e);
		xcb_flush(c);
	}

	return EXIT_SUCCESS;
}
