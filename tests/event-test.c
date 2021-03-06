/*
 * Copyright © 2012 Intel Corporation
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
#include <stdio.h>
#include <sys/socket.h>
#include <assert.h>
#include <unistd.h>

#include <string.h>

#include "test-runner.h"

static void
handle_surface(struct test_client *client)
{
	uint32_t id;
	struct wl_resource *resource;
	struct weston_surface *surface;
	struct weston_layer *layer = client->data;
	struct wl_seat *seat;

	assert(sscanf(client->buf, "surface %u", &id) == 1);
	fprintf(stderr, "got surface id %u\n", id);
	resource = wl_client_get_object(client->client, id);
	assert(resource);
	assert(strcmp(resource->object.interface->name, "wl_surface") == 0);

	surface = (struct weston_surface *) resource;

	weston_surface_configure(surface, 100, 100, 200, 200);
	weston_surface_assign_output(surface);
	weston_surface_set_color(surface, 0.0, 0.0, 0.0, 1.0);
	wl_list_insert(&layer->surface_list, &surface->layer_link);
	weston_surface_damage(surface);

	seat = &client->compositor->seat->seat;
	client->compositor->focus = 1; /* Make it work even if pointer is
					* outside X window. */
	notify_motion(seat, 100,
		      wl_fixed_from_int(150), wl_fixed_from_int(150));

	test_client_send(client, "bye\n");
}

TEST(event_test)
{
	struct test_client *client;
	struct weston_layer *layer;

	client = test_client_launch(compositor);
	client->terminate = 1;

	test_client_send(client, "create-surface\n");
	client->handle = handle_surface;

	layer = malloc(sizeof *layer);
	assert(layer);
	weston_layer_init(layer, &compositor->cursor_layer.link);
	client->data = layer;
}
