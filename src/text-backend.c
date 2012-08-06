/*
 * Copyright © 2012 Openismus GmbH
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
#include "text-server-protocol.h"

struct input_method;

struct text_model {
	struct wl_resource resource;

	struct wl_list link;

	struct input_method *input_method;
	struct wl_keyboard_grab grab;
	struct wl_surface *surface;
};

struct input_method {
	struct wl_resource *input_method_binding;
	struct wl_resource *keyboard_binding;
	struct wl_global *input_method_global;
	struct wl_global *text_model_manager_global;
	struct wl_listener destroy_listener;

	struct weston_compositor *ec;
	struct wl_list models;
	struct text_model *active_model;
};

static void
deactivate_text_model(struct text_model *text_model)
{
	struct weston_compositor *ec = text_model->input_method->ec;
	struct wl_keyboard_grab *grab = &text_model->grab;

	if (grab->keyboard && (grab->keyboard->grab == grab)) {
		wl_keyboard_end_grab(grab->keyboard);
		weston_log("end keyboard grab\n");
	}

	if (text_model->input_method->active_model == text_model) {
		text_model->input_method->active_model = NULL;
		wl_signal_emit(&ec->hide_input_panel_signal, ec);
	}
}

static void
destroy_text_model(struct wl_resource *resource)
{
	struct text_model *text_model =
		container_of(resource, struct text_model, resource);

	deactivate_text_model(text_model);

	wl_list_remove(&text_model->link);
	free(text_model);
}

static void
text_model_key(struct wl_keyboard_grab *grab,
	       uint32_t time, uint32_t key, uint32_t state_w)
{
	struct text_model *text_model = container_of(grab, struct text_model, grab);
	struct input_method *input_method = text_model->input_method;

	uint32_t serial = wl_display_next_serial(input_method->ec->wl_display);
	if (input_method->keyboard_binding) {
		wl_keyboard_send_key(input_method->keyboard_binding,
				     serial, time, key, state_w);
	}
}

static void
text_model_modifier(struct wl_keyboard_grab *grab, uint32_t serial,
		    uint32_t mods_depressed, uint32_t mods_latched,
		    uint32_t mods_locked, uint32_t group)
{
	struct text_model *text_model = container_of(grab, struct text_model, grab);
	struct input_method *input_method = text_model->input_method;

	uint32_t new_serial = wl_display_next_serial(input_method->ec->wl_display);
	if (input_method->keyboard_binding) {
		wl_keyboard_send_modifiers(input_method->keyboard_binding,
					   new_serial, mods_depressed, mods_latched,
					   mods_locked, group);
	}
}

static const struct wl_keyboard_grab_interface text_model_grab = {
	text_model_key,
	text_model_modifier,
};

static void
text_model_set_surrounding_text(struct wl_client *client,
				struct wl_resource *resource,
				const char *text)
{
}

static void
text_model_set_cursor_index(struct wl_client *client,
			    struct wl_resource *resource,
			    uint32_t index)
{
}

static void
text_model_activate(struct wl_client *client,
	            struct wl_resource *resource)
{
	struct text_model *text_model = resource->data;
	struct weston_compositor *ec = text_model->input_method->ec;

	text_model->input_method->active_model = text_model;

	wl_signal_emit(&ec->show_input_panel_signal, ec);

	if (text_model->input_method->input_method_binding &&
	    text_model->input_method->keyboard_binding) {
		struct wl_seat *seat = &text_model->input_method->ec->seat->seat;

		if (seat->keyboard->grab != &seat->keyboard->default_grab) {
			wl_keyboard_end_grab(seat->keyboard);
		}
		wl_keyboard_start_grab(seat->keyboard, &text_model->grab);
		weston_log("start keyboard grab\n");
	}
}

static void
text_model_deactivate(struct wl_client *client,
		      struct wl_resource *resource)
{
	struct text_model *text_model = resource->data;

	deactivate_text_model(text_model);
}

static void
text_model_set_selected_text(struct wl_client *client,
			     struct wl_resource *resource,
			     const char *text,
			     int32_t index)
{
}

static void
text_model_set_micro_focus(struct wl_client *client,
			   struct wl_resource *resource,
			   int32_t x,
			   int32_t y,
			   int32_t width,
			   int32_t height)
{
}

static void
text_model_set_preedit(struct wl_client *client,
		       struct wl_resource *resource)
{
}

static void
text_model_set_content_type(struct wl_client *client,
			    struct wl_resource *resource)
{
}

struct text_model_interface text_model_implementation = {
	text_model_set_surrounding_text,
	text_model_set_cursor_index,
	text_model_activate,
	text_model_deactivate,
	text_model_set_selected_text,
	text_model_set_micro_focus,
	text_model_set_preedit,
	text_model_set_content_type
};

static void text_model_manager_create_text_model(struct wl_client *client,
						 struct wl_resource *resource,
						 uint32_t id,
						 struct wl_resource *surface)
{
	struct input_method *input_method = resource->data;
	struct text_model *text_model;

	text_model = calloc(1, sizeof *text_model);

	text_model->resource.destroy = destroy_text_model;

	text_model->resource.object.id = id;
	text_model->resource.object.interface = &text_model_interface;
	text_model->resource.object.implementation =
		(void (**)(void)) &text_model_implementation;
	text_model->resource.data = text_model;

	text_model->input_method = input_method;
	text_model->surface = container_of(surface, struct wl_surface, resource);

	text_model->grab.interface = &text_model_grab;

	wl_client_add_resource(client, &text_model->resource);

	wl_list_insert(&input_method->models, &text_model->link);
};

static const struct text_model_manager_interface text_model_manager_implementation = {
	text_model_manager_create_text_model
};

static void
bind_text_model_manager(struct wl_client *client,
			void *data,
			uint32_t version,
			uint32_t id)
{
	struct input_method *input_method = data;

	/* No checking for duplicate binding necessary.
	 * No events have to be sent, so we don't need the return value. */
	wl_client_add_object(client, &text_model_manager_interface,
			     &text_model_manager_implementation,
			     id, input_method);
}

static void
input_method_commit_string(struct wl_client *client,
			   struct wl_resource *resource,
			   const char *text,
			   uint32_t index)
{
	struct input_method *input_method = resource->data;

	if (input_method->active_model) {
		text_model_send_commit_string(&input_method->active_model->resource, text, index);
	}
}

static void
unbind_keyboard_binding(struct wl_resource *resource)
{
	struct input_method *input_method = resource->data;

	input_method->keyboard_binding = NULL;
	free(resource);
}

static void
input_method_request_keyboard(struct wl_client *client,
			      struct wl_resource *resource,
			      uint32_t keyboard_id)
{
	struct input_method *input_method = resource->data;

	if (input_method->keyboard_binding) {
		wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
				       "keyboard already bound");
		return;
	}

	input_method->keyboard_binding =
		wl_client_add_object(client, &wl_keyboard_interface,
				     NULL, keyboard_id, input_method);

	if (input_method->keyboard_binding == NULL)
		return;

	input_method->keyboard_binding->destroy = unbind_keyboard_binding;

	struct weston_seat *seat = input_method->ec->seat;
	wl_keyboard_send_keymap(input_method->keyboard_binding,
				WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
				seat->xkb_info.keymap_fd,
				seat->xkb_info.keymap_size);
}

static const struct input_method_interface input_method_implementation = {
	input_method_commit_string,
	input_method_request_keyboard
};

static void
unbind_input_method(struct wl_resource *resource)
{
	struct input_method *input_method = resource->data;

	input_method->input_method_binding = NULL;
	free(resource);

	if (input_method->keyboard_binding) {
		wl_resource_destroy(input_method->keyboard_binding);
	}
}

static void
bind_input_method(struct wl_client *client,
		  void *data,
		  uint32_t version,
		  uint32_t id)
{
	struct input_method *input_method = data;
	struct wl_resource *resource;

	resource = wl_client_add_object(client, &input_method_interface,
					&input_method_implementation,
					id, input_method);

	if (input_method->input_method_binding == NULL) {
		resource->destroy = unbind_input_method;
		input_method->input_method_binding = resource;
		return;
	}

	wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
			       "interface object already bound");
	wl_resource_destroy(resource);
}

static void
input_method_notifier_destroy(struct wl_listener *listener, void *data)
{
	struct input_method *input_method =
		container_of(listener, struct input_method, destroy_listener);

	wl_display_remove_global(input_method->ec->wl_display,
				 input_method->input_method_global);
	wl_display_remove_global(input_method->ec->wl_display,
				 input_method->text_model_manager_global);
	free(input_method);
}

static void
handle_keyboard_focus(struct wl_listener *listener, void *data)
{
	struct wl_keyboard *keyboard = data;
	struct weston_surface *surface =
		(struct weston_surface *) keyboard->focus;

	struct weston_compositor *compositor =
		container_of(listener, struct weston_compositor,
			     input_method_keyboard_focus_listener);

	struct text_model *active_model = compositor->input_method->active_model;

	if (active_model &&
	    (!surface || active_model->surface != &surface->surface)) {
		deactivate_text_model(active_model);
	}
}

WL_EXPORT struct input_method *
input_method_create(struct weston_compositor *ec)
{
	struct input_method *input_method;

	input_method = calloc(1, sizeof *input_method);

	input_method->ec = ec;

	wl_list_init(&input_method->models);

	input_method->input_method_global =
		wl_display_add_global(ec->wl_display,
				      &input_method_interface,
				      input_method, bind_input_method);

	input_method->text_model_manager_global =
		wl_display_add_global(ec->wl_display,
				      &text_model_manager_interface,
				      input_method, bind_text_model_manager);

	input_method->destroy_listener.notify = input_method_notifier_destroy;
	wl_signal_add(&ec->destroy_signal, &input_method->destroy_listener);

	/* Initialize the listener. It will be added by the shell when the
	 * seat is initialized.
	 */
	ec->input_method_keyboard_focus_listener.notify = handle_keyboard_focus;

	return input_method;
}
