/*
 * Copyright © 2012 Scott Moreau
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>

#include "compositor.h"
#include "text-cursor-position-server-protocol.h"

struct text_cursor_position {
	struct wl_object base;
	struct weston_compositor *ec;
	struct wl_global *global;
	struct wl_listener destroy_listener;
};

static void
text_cursor_position_notify(struct wl_client *client,
			    struct wl_resource *resource,
			    struct wl_resource *surface_resource,
			    wl_fixed_t x, wl_fixed_t y)
{
	weston_text_cursor_position_notify((struct weston_surface *) surface_resource, x, y);
}

struct text_cursor_position_interface text_cursor_position_implementation = {
	text_cursor_position_notify
};

static void
bind_text_cursor_position(struct wl_client *client,
	     void *data, uint32_t version, uint32_t id)
{
	wl_client_add_object(client, &text_cursor_position_interface,
			     &text_cursor_position_implementation, id, data);
}

static void
text_cursor_position_notifier_destroy(struct wl_listener *listener, void *data)
{
	struct text_cursor_position *text_cursor_position =
		container_of(listener, struct text_cursor_position, destroy_listener);

	wl_display_remove_global(text_cursor_position->ec->wl_display, text_cursor_position->global);
	free(text_cursor_position);
}

void
text_cursor_position_notifier_create(struct weston_compositor *ec)
{
	struct text_cursor_position *text_cursor_position;

	text_cursor_position = malloc(sizeof *text_cursor_position);
	if (text_cursor_position == NULL)
		return;

	text_cursor_position->base.interface = &text_cursor_position_interface;
	text_cursor_position->base.implementation =
		(void(**)(void)) &text_cursor_position_implementation;
	text_cursor_position->ec = ec;

	text_cursor_position->global = wl_display_add_global(ec->wl_display,
						&text_cursor_position_interface,
						text_cursor_position, bind_text_cursor_position);

	text_cursor_position->destroy_listener.notify = text_cursor_position_notifier_destroy;
	wl_signal_add(&ec->destroy_signal, &text_cursor_position->destroy_listener);
}

WL_EXPORT void
weston_text_cursor_position_notify(struct weston_surface *surface,
						wl_fixed_t cur_pos_x,
						wl_fixed_t cur_pos_y)
{
	struct weston_output *output;
	wl_fixed_t global_x, global_y;

	weston_surface_to_global_fixed(surface, cur_pos_x, cur_pos_y,
						&global_x, &global_y);

	wl_list_for_each(output, &surface->compositor->output_list, link)
		if (output->zoom.active &&
		    pixman_region32_contains_point(&output->region,
						wl_fixed_to_int(global_x),
						wl_fixed_to_int(global_y),
						NULL)) {
			output->zoom.text_cursor.x = global_x;
			output->zoom.text_cursor.y = global_y;
			weston_output_update_zoom(output, ZOOM_FOCUS_TEXT);
		}
}

static void
weston_zoom_frame_z(struct weston_animation *animation,
		struct weston_output *output, uint32_t msecs)
{
	if (animation->frame_counter <= 1)
		output->zoom.spring_z.timestamp = msecs;

	weston_spring_update(&output->zoom.spring_z, msecs);

	if (output->zoom.spring_z.current > output->zoom.max_level)
		output->zoom.spring_z.current = output->zoom.max_level;
	else if (output->zoom.spring_z.current < 0.0)
		output->zoom.spring_z.current = 0.0;

	if (weston_spring_done(&output->zoom.spring_z)) {
		if (output->zoom.level <= 0.0) {
			output->zoom.active = 0;
			output->disable_planes--;
		}
		output->zoom.spring_z.current = output->zoom.level;
		wl_list_remove(&animation->link);
		wl_list_init(&animation->link);
	}

	output->dirty = 1;
	weston_output_damage(output);
}

static void
weston_zoom_frame_xy(struct weston_animation *animation,
		struct weston_output *output, uint32_t msecs)
{
	wl_fixed_t x, y;
	if (animation->frame_counter <= 1)
		output->zoom.spring_xy.timestamp = msecs;

	weston_spring_update(&output->zoom.spring_xy, msecs);

	x = output->zoom.from.x - ((output->zoom.from.x - output->zoom.to.x) *
						output->zoom.spring_xy.current);
	y = output->zoom.from.y - ((output->zoom.from.y - output->zoom.to.y) *
						output->zoom.spring_xy.current);

	output->zoom.current.x = x;
	output->zoom.current.y = y;

	if (weston_spring_done(&output->zoom.spring_xy)) {
		output->zoom.spring_xy.current = output->zoom.spring_xy.target;
		output->zoom.current.x =
				(output->zoom.type == ZOOM_FOCUS_POINTER) ?
					output->compositor->seat->pointer.x :
					output->zoom.text_cursor.x;
		output->zoom.current.y =
				(output->zoom.type == ZOOM_FOCUS_POINTER) ?
					output->compositor->seat->pointer.y :
					output->zoom.text_cursor.y;
		wl_list_remove(&animation->link);
		wl_list_init(&animation->link);
	}

	output->dirty = 1;
	weston_output_damage(output);
}

static void
zoom_area_center_from_pointer(struct weston_output *output,
				wl_fixed_t *x, wl_fixed_t *y)
{
	float level = output->zoom.spring_z.current;
	wl_fixed_t offset_x = wl_fixed_from_int(output->x);
	wl_fixed_t offset_y = wl_fixed_from_int(output->y);
	wl_fixed_t w = wl_fixed_from_int(output->current->width);
	wl_fixed_t h = wl_fixed_from_int(output->current->height);

	*x -= ((((*x - offset_x) / (float) w) - 0.5) * (w * (1.0 - level)));
	*y -= ((((*y - offset_y) / (float) h) - 0.5) * (h * (1.0 - level)));
}

static void
weston_output_update_zoom_transform(struct weston_output *output)
{
	uint32_t type = output->zoom.type;
	float global_x, global_y;
	wl_fixed_t x = output->zoom.current.x;
	wl_fixed_t y = output->zoom.current.y;
	float trans_min, trans_max;
	float ratio, level;

	level = output->zoom.spring_z.current;
	ratio = 1 / level;

	if (!output->zoom.active || level > output->zoom.max_level)
		return;

	if (type == ZOOM_FOCUS_POINTER &&
			wl_list_empty(&output->zoom.animation_xy.link))
		zoom_area_center_from_pointer(output, &x, &y);

	global_x = wl_fixed_to_double(x);
	global_y = wl_fixed_to_double(y);

	output->zoom.trans_x =
		((((global_x - output->x) / output->current->width) *
		(level * 2)) - level) * ratio;
	output->zoom.trans_y =
		((((global_y - output->y) / output->current->height) *
		(level * 2)) - level) * ratio;

	trans_max = level * 2 - level;
	trans_min = -trans_max;

	/* Clip zoom area to output */
	if (output->zoom.trans_x > trans_max)
		output->zoom.trans_x = trans_max;
	else if (output->zoom.trans_x < trans_min)
		output->zoom.trans_x = trans_min;
	if (output->zoom.trans_y > trans_max)
		output->zoom.trans_y = trans_max;
	else if (output->zoom.trans_y < trans_min)
		output->zoom.trans_y = trans_min;
}

static void
weston_zoom_transition(struct weston_output *output, uint32_t type,
						wl_fixed_t x, wl_fixed_t y)
{
	if (output->zoom.type != type) {
		/* Set from/to points and start animation */
		output->zoom.spring_xy.current = 0.0;
		output->zoom.spring_xy.previous = 0.0;
		output->zoom.spring_xy.target = 1.0;

		if (wl_list_empty(&output->zoom.animation_xy.link)) {
			output->zoom.animation_xy.frame_counter = 0;
			wl_list_insert(output->animation_list.prev,
				&output->zoom.animation_xy.link);

			output->zoom.from.x = (type == ZOOM_FOCUS_TEXT) ?
						x : output->zoom.text_cursor.x;
			output->zoom.from.y = (type == ZOOM_FOCUS_TEXT) ?
						y : output->zoom.text_cursor.y;
		} else {
			output->zoom.from.x = output->zoom.current.x;
			output->zoom.from.y = output->zoom.current.y;
		}

		output->zoom.to.x = (type == ZOOM_FOCUS_POINTER) ?
						x : output->zoom.text_cursor.x;
		output->zoom.to.y = (type == ZOOM_FOCUS_POINTER) ?
						y : output->zoom.text_cursor.y;
		output->zoom.current.x = output->zoom.from.x;
		output->zoom.current.y = output->zoom.from.y;

		output->zoom.type = type;
	}

	if (output->zoom.level != output->zoom.spring_z.current) {
		output->zoom.spring_z.target = output->zoom.level;
		if (wl_list_empty(&output->zoom.animation_z.link)) {
			output->zoom.animation_z.frame_counter = 0;
			wl_list_insert(output->animation_list.prev,
				&output->zoom.animation_z.link);
		}
	}

	output->dirty = 1;
	weston_output_damage(output);
}

WL_EXPORT void
weston_output_update_zoom(struct weston_output *output, uint32_t type)
{
	wl_fixed_t x = output->compositor->seat->pointer.x;
	wl_fixed_t y = output->compositor->seat->pointer.y;

	zoom_area_center_from_pointer(output, &x, &y);

	if (type == ZOOM_FOCUS_POINTER) {
		if (wl_list_empty(&output->zoom.animation_xy.link)) {
			output->zoom.current.x =
					output->compositor->seat->pointer.x;
			output->zoom.current.y =
					output->compositor->seat->pointer.y;
		} else {
			output->zoom.to.x = x;
			output->zoom.to.y = y;
		}
	}

	if (type == ZOOM_FOCUS_TEXT) {
		if (wl_list_empty(&output->zoom.animation_xy.link)) {
			output->zoom.current.x = output->zoom.text_cursor.x;
			output->zoom.current.y = output->zoom.text_cursor.y;
		} else {
			output->zoom.to.x = output->zoom.text_cursor.x;
			output->zoom.to.y = output->zoom.text_cursor.y;
		}
	}

	weston_zoom_transition(output, type, x, y);
	weston_output_update_zoom_transform(output);
}

WL_EXPORT void
weston_output_init_zoom(struct weston_output *output)
{
	output->zoom.active = 0;
	output->zoom.increment = 0.07;
	output->zoom.max_level = 0.95;
	output->zoom.level = 0.0;
	output->zoom.trans_x = 0.0;
	output->zoom.trans_y = 0.0;
	output->zoom.type = ZOOM_FOCUS_POINTER;
	weston_spring_init(&output->zoom.spring_z, 250.0, 0.0, 0.0);
	output->zoom.spring_z.friction = 1000;
	output->zoom.animation_z.frame = weston_zoom_frame_z;
	wl_list_init(&output->zoom.animation_z.link);
	weston_spring_init(&output->zoom.spring_xy, 250.0, 0.0, 0.0);
	output->zoom.spring_xy.friction = 1000;
	output->zoom.animation_xy.frame = weston_zoom_frame_xy;
	wl_list_init(&output->zoom.animation_xy.link);
}
