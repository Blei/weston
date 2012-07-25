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

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input.h>
#include <cairo.h>
#include <pango/pangocairo.h>

#include "window.h"
#include "text-client-protocol.h"

struct buffer {
	char *data;
	size_t length;
	size_t capacity;
};

struct text_entry {
	struct widget *widget;
	struct buffer text;
	struct buffer preedit;
	PangoAttrList *attr_list;
	int active;
	struct rectangle allocation;
	struct text_model *model;
};

struct editor {
	struct text_model_manager *text_model_manager;
	struct display *display;
	struct window *window;
	struct widget *widget;
	struct text_entry *entry;
	struct text_entry *editor;
};

static void
buffer_append(struct buffer *buffer, const char *text)
{
	size_t new_length = strlen(text) + buffer->length;
	if (new_length > buffer->capacity) {
		buffer->data = realloc(buffer->data, new_length);
		buffer->capacity = new_length;
	}
	strcat(buffer->data, text);
	buffer->length = new_length;
}

static void
buffer_set(struct buffer *buffer, const char *text)
{
	size_t length = strlen(text) + 1;
	if (length > buffer->length) {
		buffer->data = realloc(buffer->data, length);
		buffer->capacity = length;
	}
	strcpy(buffer->data, text);
	buffer->length = length;
}

static void
buffer_delete_last_character(struct buffer *buffer)
{
	char *prev_char;

	if (buffer->length == 0)
		return;

	prev_char = g_utf8_find_prev_char(buffer->data,
					  buffer->data + buffer->length - 1);
	if (prev_char != NULL) {
		buffer->length = prev_char - buffer->data + 1;
		*prev_char = 0;
	}
}

static void
text_entry_commit(struct text_entry *entry, const char *text)
{
	buffer_set(&entry->preedit, "");
	buffer_append(&entry->text, text);
	pango_attr_list_unref(entry->attr_list);
	entry->attr_list = NULL;
}

static void
text_entry_preedit(struct text_entry *entry, const char *text)
{
	buffer_set(&entry->preedit, text);
	pango_attr_list_unref(entry->attr_list);
	entry->attr_list = NULL;
}

static void
text_model_commit_string(void *data,
			 struct text_model *text_model,
			 const char *text,
			 uint32_t index)
{
	struct text_entry *entry = data;

	text_entry_commit(entry, text);

	widget_schedule_redraw(entry->widget);
}

static void
text_model_preedit_string(void *data,
			  struct text_model *text_model,
			  const char *text,
			  uint32_t index)
{
	struct text_entry *entry = data;

	text_entry_preedit(entry, text);

	widget_schedule_redraw(entry->widget);
}

static PangoAttribute *
convert_to_pango_attribute(uint32_t type,
			   uint32_t value,
			   uint32_t start,
			   uint32_t end)
{
	PangoAttribute *attr;
	PangoUnderline underline_type;

	guint16 red   = (value & 0xff0000) >> 8;
	guint16 green = value & 0xff00;
	guint16 blue  = (value & 0xff) << 8;

	switch (type) {
	case TEXT_MODEL_PREEDIT_STYLE_TYPE_UNDERLINE:
		switch (value) {
		case TEXT_MODEL_PREEDIT_UNDERLINE_TYPE_NONE:
			underline_type = PANGO_UNDERLINE_NONE;
			break;
		case TEXT_MODEL_PREEDIT_UNDERLINE_TYPE_SINGLE:
			underline_type = PANGO_UNDERLINE_SINGLE;
			break;
		case TEXT_MODEL_PREEDIT_UNDERLINE_TYPE_DOUBLE:
			underline_type = PANGO_UNDERLINE_DOUBLE;
			break;
		case TEXT_MODEL_PREEDIT_UNDERLINE_TYPE_LOW:
			underline_type = PANGO_UNDERLINE_LOW;
			break;
		case TEXT_MODEL_PREEDIT_UNDERLINE_TYPE_ERROR:
			underline_type = PANGO_UNDERLINE_ERROR;
			break;
		default:
			assert(false && "unknown underline type");
		}
		attr = pango_attr_underline_new(underline_type);
		break;
	case TEXT_MODEL_PREEDIT_STYLE_TYPE_FOREGROUND:
		attr = pango_attr_foreground_new(red, green, blue);
		break;
	case TEXT_MODEL_PREEDIT_STYLE_TYPE_BACKGROUND:
		attr = pango_attr_background_new(red, green, blue);
		break;
	default:
		assert(false && "unknown preedit style type");
	}

	attr->start_index = start;
	attr->end_index   = end;
	return attr;
}

static void
text_model_preedit_styling(void *data,
			   struct text_model *text_model,
			   uint32_t type,
			   uint32_t value,
			   uint32_t start,
			   uint32_t end)
{
	struct text_entry *entry = data;

	PangoAttribute *attr =
		convert_to_pango_attribute(type, value, start, end);
	if (!entry->attr_list) {
		entry->attr_list = pango_attr_list_new();
	}
	pango_attr_list_insert(entry->attr_list, attr);

	widget_schedule_redraw(entry->widget);
}

static const struct text_model_listener text_model_listener = {
	text_model_commit_string,
	text_model_preedit_string,
	text_model_preedit_styling
};

static struct text_entry*
text_entry_create(struct editor *editor, const char *text)
{
	struct text_entry *entry;
	struct wl_surface *surface;

	entry = malloc(sizeof *entry);

	surface = window_get_wl_surface(editor->window);

	entry->widget = editor->widget;
	entry->text.data = strdup(text);
	entry->text.length = entry->text.capacity = strlen(text) + 1;
	entry->preedit.data = calloc(1, 1);
	entry->preedit.length = entry->preedit.capacity = 1;
	entry->attr_list = NULL;
	entry->active = 0;
	entry->model = text_model_manager_create_text_model(editor->text_model_manager, surface);
	text_model_add_listener(entry->model, &text_model_listener, entry);

	return entry;
}

static void
text_entry_destroy(struct text_entry *entry)
{
	text_model_destroy(entry->model);
	free(entry->text.data);
	free(entry->preedit.data);
	free(entry);
}

static void
text_entry_draw(struct text_entry *entry, cairo_t *cr)
{
	PangoLayout *layout;
	PangoAttrList *attrs;
	PangoAttribute *weight_attr;

	int width, height;
	double text_width, text_height;
	double text_start_height;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_rectangle(cr, entry->allocation.x, entry->allocation.y, entry->allocation.width, entry->allocation.height);
	cairo_clip(cr);

	cairo_translate(cr, entry->allocation.x, entry->allocation.y);
	cairo_rectangle(cr, 0, 0, entry->allocation.width, entry->allocation.height);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.5);
	cairo_fill(cr);
	if (entry->active) {
		cairo_rectangle(cr, 0, 0, entry->allocation.width, entry->allocation.height);
		cairo_set_source_rgba(cr, 0, 0, 1, 0.5);
		cairo_stroke(cr);
	}

	cairo_set_source_rgb(cr, 0, 0, 0);

	layout = pango_cairo_create_layout(cr);
	attrs = pango_attr_list_new();

	weight_attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
	weight_attr->start_index = PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
	weight_attr->end_index   = PANGO_ATTR_INDEX_TO_TEXT_END;
	pango_attr_list_insert(attrs, weight_attr);
	pango_layout_set_attributes(layout, attrs);
	pango_attr_list_unref(attrs);

	pango_layout_set_text(layout, entry->text.data, -1);
	pango_layout_get_size(layout, &width, &height);

	text_width = (double) width / PANGO_SCALE;
	text_height = (double) height / PANGO_SCALE;
	text_start_height = (entry->allocation.height - text_height) / 2;

	cairo_translate(cr, 10, text_start_height);
	pango_cairo_show_layout(cr, layout);

	pango_layout_set_text(layout, entry->preedit.data, -1);
	pango_layout_set_attributes(layout, entry->attr_list);
	cairo_translate(cr, text_width, 0);
	pango_cairo_show_layout(cr, layout);

	g_object_unref(layout);

	cairo_restore(cr);
}

static void
redraw_handler(struct widget *widget, void *data)
{
	struct editor *editor = data;
	cairo_surface_t *surface;
	struct rectangle allocation;
	cairo_t *cr;

	surface = window_get_surface(editor->window);
	widget_get_allocation(editor->widget, &allocation);

	cr = cairo_create(surface);
	cairo_rectangle(cr, allocation.x, allocation.y, allocation.width, allocation.height);
	cairo_clip(cr);

	cairo_translate(cr, allocation.x, allocation.y);

	/* Draw background */
	cairo_push_group(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);	
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	/* Entry */
	text_entry_draw(editor->entry, cr);

	/* Editor */
	text_entry_draw(editor->editor, cr);

	cairo_pop_group_to_source(cr);
	cairo_paint(cr);

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

static void
text_entry_allocate(struct text_entry *entry, int32_t x, int32_t y,
		    int32_t width, int32_t height)
{
	entry->allocation.x = x;
	entry->allocation.y = y;
	entry->allocation.width = width;
	entry->allocation.height = height;
}

static void
resize_handler(struct widget *widget,
	       int32_t width, int32_t height, void *data)
{
	struct editor *editor = data;

	text_entry_allocate(editor->entry, 20, 20, width - 40, height / 2 - 40);
	text_entry_allocate(editor->editor, 20, height / 2 + 20, width - 40, height / 2 - 40);
}

static int32_t
rectangle_contains(struct rectangle *rectangle, int32_t x, int32_t y)
{
	if (x < rectangle->x || x > rectangle->x + rectangle->width) {
		return 0;
	}

	if (y < rectangle->y || y > rectangle->y + rectangle->height) {
		return 0;
	}

	return 1;
}

static void
text_entry_activate(struct text_entry *entry)
{
	text_model_activate(entry->model);
}

static void
text_entry_deactivate(struct text_entry *entry)
{
	text_model_deactivate(entry->model);
}

static void
button_handler(struct widget *widget,
	       struct input *input, uint32_t time,
	       uint32_t button,
	       enum wl_pointer_button_state state, void *data)
{
	struct editor *editor = data;
	struct rectangle allocation;
	int32_t x, y;

	if (state != WL_POINTER_BUTTON_STATE_PRESSED || button != BTN_LEFT) {
		return;
	}

	input_get_position(input, &x, &y);

	widget_get_allocation(editor->widget, &allocation);
	x -= allocation.x;
	y -= allocation.y;

	int32_t activate_entry = rectangle_contains(&editor->entry->allocation, x, y);
	int32_t activate_editor = rectangle_contains(&editor->editor->allocation, x, y);
	assert(!(activate_entry && activate_editor));

	if (activate_entry) {
		if (editor->editor->active)
			text_entry_deactivate(editor->editor);
		if (!editor->entry->active)
			text_entry_activate(editor->entry);
	} else if (activate_editor) {
		if (editor->entry->active)
			text_entry_deactivate(editor->entry);
		if (!editor->editor->active)
			text_entry_activate(editor->editor);
	} else {
		if (editor->entry->active)
			text_entry_deactivate(editor->entry);
		if (editor->editor->active)
			text_entry_deactivate(editor->editor);
	}
	editor->entry->active = activate_entry;
	editor->editor->active = activate_editor;
	assert(!(editor->entry->active && editor->editor->active));

	widget_schedule_redraw(widget);
}

static void
key_handler(struct window *window, struct input *input,
	    uint32_t time, uint32_t key, uint32_t unicode,
	    enum wl_keyboard_key_state state, void *data)
{
	struct editor *editor = data;
	struct text_entry *active_entry;
	struct buffer *buffer;
	char *prev_char;

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
		return;

	if (editor->entry->active)
		active_entry = editor->entry;
	else if (editor->editor->active)
		active_entry = editor->editor;
	else
		return;

	/* Only handle backspace for now. This is a demo after all. */
	if (unicode == XKB_KEY_BackSpace) {
		buffer_delete_last_character(&active_entry->text);
		widget_schedule_redraw(active_entry->widget);
	}
}

static void
keyboard_focus_handler(struct window *window,
		       struct input *device,
		       void *data)
{
	struct editor *editor = data;

	if (device == NULL) {
		if (editor->entry->active)
			text_entry_deactivate(editor->entry);
		else if (editor->editor->active)
			text_entry_deactivate(editor->editor);
	} else {
		if (editor->entry->active)
			text_entry_activate(editor->entry);
		else if (editor->editor->active)
			text_entry_activate(editor->editor);
	}

}

static void
global_handler(struct wl_display *display, uint32_t id,
	       const char *interface, uint32_t version, void *data)
{
	struct editor *editor = data;

	if (!strcmp(interface, "text_model_manager")) {
		editor->text_model_manager = wl_display_bind(display, id,
							     &text_model_manager_interface);
	}
}

int
main(int argc, char *argv[])
{
	struct editor editor;

	editor.display = display_create(argc, argv);
	if (editor.display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}
	wl_display_add_global_listener(display_get_display(editor.display),
				       global_handler, &editor);


	editor.window = window_create(editor.display);
	editor.widget = frame_create(editor.window, &editor);

	editor.entry = text_entry_create(&editor, "Entry");
	editor.editor = text_entry_create(&editor, "Editor");

	window_set_title(editor.window, "Text Editor");
	window_set_key_handler(editor.window, key_handler);
	window_set_keyboard_focus_handler(editor.window,
					  keyboard_focus_handler);
	window_set_user_data(editor.window, &editor);

	widget_set_redraw_handler(editor.widget, redraw_handler);
	widget_set_resize_handler(editor.widget, resize_handler);
	widget_set_button_handler(editor.widget, button_handler);

	window_schedule_resize(editor.window, 500, 400);

	display_run(editor.display);

	text_entry_destroy(editor.entry);
	text_entry_destroy(editor.editor);

	return 0;
}
