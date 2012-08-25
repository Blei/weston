/*
 * Copyright © 2012 Philipp Brüschweiler
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <string.h>

#include "window.h"

struct relative_grab_demo {
	struct display *d;
	struct window *window;
	struct widget *widget;
	struct wl_relative_grab *relative_grab;
};

static void
relative_grab_motion(void *data,
		     struct wl_relative_grab *wl_relative_grab,
		     uint32_t time,
		     wl_fixed_t dx, wl_fixed_t dy)
{
	fprintf(stderr, "motion, dx: %.f, dy: %.f\n",
		wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}

static void
relative_grab_destroy_me(void *data,
			 struct wl_relative_grab *wl_relative_grab)
{
	struct relative_grab_demo *demo = data;

	fprintf(stderr, "destroy me\n");

	demo->relative_grab = NULL;

	wl_relative_grab_destroy(wl_relative_grab);

	window_schedule_redraw(demo->window);
}

static const struct wl_relative_grab_listener relative_grab_listener = {
	relative_grab_motion,
	relative_grab_destroy_me,
};

static void
redraw_handler(struct widget *widget, void *data)
{
	struct relative_grab_demo *demo = data;
	cairo_surface_t *surface;
	cairo_t *cr;
	struct rectangle rect;

	widget_get_allocation(demo->widget, &rect);
	surface = window_get_surface(demo->window);

	cr = cairo_create(surface);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

	cairo_rectangle(cr, rect.x, rect.y, rect.width, rect.height);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_fill(cr);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

static void
button_handler(struct widget *widget, struct input *input, uint32_t time,
	       uint32_t button, enum wl_pointer_button_state state, void *data)
{
	struct relative_grab_demo *demo = data;

	if (demo->relative_grab)
		return;

	fprintf(stderr, "starting relative grab\n");

	struct wl_shell_surface *surface =
		window_get_wl_shell_surface(demo->window);
	struct wl_seat *seat = input_get_seat(input);
	uint32_t serial = display_get_serial(demo->d);

	demo->relative_grab =
		wl_shell_surface_relative_grab(surface, seat, serial);
	wl_relative_grab_add_listener(demo->relative_grab,
				      &relative_grab_listener,
				      demo);

	input_ungrab(input);
}

static void
key_handler(struct window *window, struct input *input,
	    uint32_t time, uint32_t key, uint32_t unicode,
	    enum wl_keyboard_key_state state, void *data)
{
	struct relative_grab_demo *demo = data;

	if (demo->relative_grab && (unicode == XKB_KEY_Escape)) {
		fprintf(stderr, "escape was hit, stopping grab\n");
		wl_relative_grab_destroy(demo->relative_grab);
		demo->relative_grab = NULL;
	}
}

static void
keyboard_focus_handler(struct window *window,
		       struct input *device, void *data)
{
	struct relative_grab_demo *demo = data;

	window_schedule_redraw(demo->window);
}

static void
relative_grab_demo_init(struct relative_grab_demo *demo) {
	demo->window = window_create(demo->d);
	window_set_user_data(demo->window, demo);
	window_set_title(demo->window, "Relative Grab Demo");

	demo->widget = frame_create(demo->window, demo);

	widget_set_redraw_handler(demo->widget, redraw_handler);
	widget_set_button_handler(demo->widget, button_handler);

	window_set_key_handler(demo->window, key_handler);
	window_set_keyboard_focus_handler(demo->window,
					  keyboard_focus_handler);

	window_schedule_resize(demo->window, 300, 300);
}

int
main(int argc, char *argv[])
{
	struct relative_grab_demo demo;
	memset(&demo, 0, sizeof demo);

	demo.d = display_create(argc, argv);
	if (demo.d == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	relative_grab_demo_init(&demo);

	display_run(demo.d);

	display_destroy(demo.d);

	return 0;
}
