/*
 * Copyright © 2012 Philipp Brüschweiler
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
#include <sys/mman.h>
#include <unistd.h>

#include "desktop-shell-client-protocol.h"
#include "text-client-protocol.h"
#include "window.h"

typedef enum {
	STATE_EMPTY = 0,
	STATE_B,
	STATE_BY,
	STATE_C,
	STATE_CH,
	STATE_D,
	STATE_DY,
	STATE_F,
	STATE_FY,
	STATE_G,
	STATE_GY,
	STATE_H,
	STATE_HY,
	STATE_J,
	STATE_JY,
	STATE_K,
	STATE_KY,
	STATE_L,
	STATE_LY,
	STATE_M,
	STATE_MY,
	STATE_N,
	STATE_NY,
	STATE_P,
	STATE_PY,
	STATE_Q,
	STATE_R,
	STATE_RY,
	STATE_S,
	STATE_SH,
	STATE_SY,
	STATE_T,
	STATE_TY,
	STATE_V,
	STATE_VY,
	STATE_W,
	STATE_X,
	STATE_XY,
	STATE_Y,
	STATE_Z,
	STATE_ZY
} hiragana_state;

struct hiragana_ime {
	struct display *display;
	struct input_method *input_method;
	struct wl_keyboard *input_method_keyboard;

	uint32_t modifiers;
	struct {
		struct xkb_context *context;
		struct xkb_keymap *keymap;
		struct xkb_state *state;
		xkb_mod_mask_t control_mask;
		xkb_mod_mask_t alt_mask;
		xkb_mod_mask_t shift_mask;
	} xkb;

	bool on;
	hiragana_state state;
};

static const char *
state_to_preedit(hiragana_state state)
{
	switch (state) {
	case STATE_EMPTY: return "";
	case STATE_B:     return "b";
	case STATE_BY:    return "by";
	case STATE_C:     return "c";
	case STATE_CH:    return "ch";
	case STATE_D:     return "d";
	case STATE_DY:    return "dy";
	case STATE_F:     return "f";
	case STATE_FY:    return "fy";
	case STATE_G:     return "g";
	case STATE_GY:    return "gy";
	case STATE_H:     return "h";
	case STATE_HY:    return "hy";
	case STATE_J:     return "j";
	case STATE_JY:    return "jy";
	case STATE_K:     return "k";
	case STATE_KY:    return "ky";
	case STATE_L:     return "l";
	case STATE_LY:    return "ly";
	case STATE_M:     return "m";
	case STATE_MY:    return "my";
	case STATE_N:     return "n";
	case STATE_NY:    return "ny";
	case STATE_P:     return "p";
	case STATE_PY:    return "py";
	case STATE_Q:     return "q";
	case STATE_R:     return "r";
	case STATE_RY:    return "ry";
	case STATE_S:     return "s";
	case STATE_SH:    return "sh";
	case STATE_SY:    return "sy";
	case STATE_T:     return "t";
	case STATE_TY:    return "ty";
	case STATE_V:     return "v";
	case STATE_VY:    return "vy";
	case STATE_W:     return "w";
	case STATE_X:     return "x";
	case STATE_XY:    return "xy";
	case STATE_Y:     return "y";
	case STATE_Z:     return "z";
	case STATE_ZY:    return "zy";
	}
}

static const char *vowel_table[][5] = {
	{"あ", "い", "う", "え", "お"}, // STATE_EMPTY
	{"ば", "び", "ぶ", "べ", "ぼ"}, // STATE_B
	{"びゃ", "びぃ", "びゅ", "びぇ", "びょ"}, // STATE_BY
	{"か", "し", "く", "せ", "こ"}, // STATE_C
	{"ちゃ", "ち", "ちゅ", "ちぇ", "ちょ"}, // STATE_CH
	{"だ", "ぢ", "づ", "で", "ど"}, // STATE_D
	{"ぢゃ", "ぢぃ", "ぢゅ", "ぢぇ", "ぢょ"}, // STATE_DY
	{"ふぁ", "ふぃ", "ふ", "ふぇ", "ふぉ"}, // STATE_F
	{"ふゃ", "ｆｙい", "ふゅ", "ｆｙえ", "ふょ"}, // STATE_FY
	{"が", "ぎ", "ぐ", "げ", "ご"}, // STATE_G
	{"ぎゃ", "ぎぃ", "ぎゅ", "ぎぇ", "ぎょ"}, // STATE_GY
	{"は", "ひ", "ふ", "へ", "ほ"}, // STATE_H
	{"ひゃ", "ひぃ", "ひゅ", "ひぇ", "ひょ"}, // STATE_HY
	{"じゃ", "じ", "じゅ", "じぇ", "じょ"}, // STATE_J
	{"じゃ", "じぃ", "じゅ", "じぇ", "じょ"}, // STATE_JY
	{"か", "き", "く", "け", "こ"}, // STATE_K
	{"きゃ", "きぃ", "きゅ", "きぇ", "きょ"}, // STATE_KY
	{"ぁ", "ぃ", "ぅ", "ぇ", "ぉ"}, // STATE_L
	{"ゃ", "ぃ", "ゅ", "ぇ", "ょ"}, // STATE_LY
	{"ま", "み", "む", "め", "も"}, // STATE_M
	{"みゃ", "みぃ", "みゅ", "みぇ", "みょ"}, // STATE_MY
	{"な", "に", "ぬ", "ね", "の"}, // STATE_N
	{"にゃ", "にぃ", "にゅ", "にぇ", "にょ"}, // STATE_NY
	{"ぱ", "ぴ", "ぷ", "ぺ", "ぽ"}, // STATE_P
	{"ぴゃ", "ぴぃ", "ぴゅ", "ぴぇ", "ぴょ"}, // STATE_PY
	{"くぁ", "くぃ", "く", "くぇ", "くぉ"}, // STATE_Q
	{"ら", "り", "る", "れ", "ろ"}, // STATE_R
	{"りゃ", "りぃ", "りゅ", "りぇ", "りょ"}, // STATE_RY
	{"さ", "し", "す", "せ", "そ"}, // STATE_S
	{"しゃ", "し", "しゅ", "しぇ", "しょ"}, // STATE_SH
	{"しゃ", "しぃ", "しゅ", "しぇ", "しょ"}, // STATE_SY
	{"た", "ち", "つ", "て", "と"}, // STATE_T
	{"ちゃ", "ちぃ", "ちゅ", "ちぇ", "ちょ"}, // STATE_TY
	{"ヴぁ", "ヴぃ", "ヴ", "ヴぇ", "ヴぉ"}, // STATE_V
	{"ヴゃ", "ヴぃ", "ヴゅ", "ヴぇ", "ヴょ"}, // STATE_VY
	{"わ", "うぃ", "う", "うぇ", "を"}, // STATE_W
	{"ぁ", "ぃ", "ぅ", "ぇ", "ぉ"}, // STATE_X
	{"ゃ", "ぃ", "ゅ", "ぇ", "ょ"}, // STATE_XY
	{"や", "ｙい", "ゆ", "いぇ", "よ"}, // STATE_Y
	{"ざ", "じ", "ず", "ぜ", "ぞ"}, // STATE_Z
	{"じゃ", "じぃ", "じゅ", "じぇ", "じょ"} // STATE_ZY
};

static inline int
vowel_index(uint32_t sym)
{
	switch (sym) {
	case 'a': return 0;
	case 'i': return 1;
	case 'u': return 2;
	case 'e': return 3;
	case 'o': return 4;
	default: return -1;
	}
}

static inline hiragana_state
consonant_to_state(uint32_t sym)
{
	switch (sym) {
	case 'b': return STATE_B;
	case 'c': return STATE_C;
	case 'd': return STATE_D;
	case 'f': return STATE_F;
	case 'g': return STATE_G;
	case 'h': return STATE_H;
	case 'j': return STATE_J;
	case 'k': return STATE_K;
	case 'l': return STATE_L;
	case 'm': return STATE_M;
	case 'n': return STATE_N;
	case 'p': return STATE_P;
	case 'q': return STATE_Q;
	case 'r': return STATE_R;
	case 's': return STATE_S;
	case 't': return STATE_T;
	case 'v': return STATE_V;
	case 'w': return STATE_W;
	case 'x': return STATE_X;
	case 'y': return STATE_Y;
	case 'z': return STATE_Z;
	default: assert(false);
	}
}

static void
update_state(hiragana_state state,
	     uint32_t sym,
	     hiragana_state *new_state,
	     const char **output)
{
	*output = NULL;
	if (sym == 'y') {
		switch (state) {
		case STATE_B: *new_state = STATE_BY; return;
		case STATE_D: *new_state = STATE_DY; return;
		case STATE_F: *new_state = STATE_FY; return;
		case STATE_G: *new_state = STATE_GY; return;
		case STATE_H: *new_state = STATE_HY; return;
		case STATE_J: *new_state = STATE_JY; return;
		case STATE_K: *new_state = STATE_KY; return;
		case STATE_L: *new_state = STATE_LY; return;
		case STATE_M: *new_state = STATE_MY; return;
		case STATE_N: *new_state = STATE_NY; return;
		case STATE_P: *new_state = STATE_PY; return;
		case STATE_R: *new_state = STATE_RY; return;
		case STATE_S: *new_state = STATE_SY; return;
		case STATE_T: *new_state = STATE_TY; return;
		case STATE_V: *new_state = STATE_VY; return;
		case STATE_X: *new_state = STATE_XY; return;
		case STATE_Z: *new_state = STATE_ZY; return;
		default: break; // continue normal handling
		}
	} else if (sym == 'h') {
		switch (state) {
		case STATE_C: *new_state = STATE_CH; return;
		case STATE_S: *new_state = STATE_SH; return;
		default: break; // continue normal handling
		}
	} else if (sym == XKB_KEY_BackSpace) {
		switch (state) {
		case STATE_CH: *new_state = STATE_C; break;
		case STATE_SH: *new_state = STATE_S; break;
		case STATE_BY: *new_state = STATE_B; break;
		case STATE_DY: *new_state = STATE_D; break;
		case STATE_FY: *new_state = STATE_F; break;
		case STATE_GY: *new_state = STATE_G; break;
		case STATE_HY: *new_state = STATE_H; break;
		case STATE_JY: *new_state = STATE_J; break;
		case STATE_KY: *new_state = STATE_K; break;
		case STATE_LY: *new_state = STATE_L; break;
		case STATE_MY: *new_state = STATE_M; break;
		case STATE_NY: *new_state = STATE_N; break;
		case STATE_PY: *new_state = STATE_P; break;
		case STATE_RY: *new_state = STATE_R; break;
		case STATE_SY: *new_state = STATE_S; break;
		case STATE_TY: *new_state = STATE_T; break;
		case STATE_VY: *new_state = STATE_V; break;
		case STATE_XY: *new_state = STATE_X; break;
		case STATE_ZY: *new_state = STATE_Z; break;
		default:       *new_state = STATE_EMPTY; break;
		}
		return;
	}

	int vi = vowel_index(sym);

	// N needs special handling
	if (state == STATE_N) {
		if (vi >= 0) {
			*new_state = STATE_EMPTY;
			*output = vowel_table[STATE_N][vi];
		} else if (sym == 'n') {
			*new_state = STATE_EMPTY;
			*output = "ん";
		} else if (sym == 'y') {
			*new_state = STATE_NY;
		} else {
			*new_state = consonant_to_state(sym);
			*output = "ん";
		}
		return;
	}

	if (vi >= 0) {
		*output = vowel_table[state][vi];
		*new_state = STATE_EMPTY;
	} else {
		*output = state_to_preedit(state);
		*new_state = consonant_to_state(sym);
	}
}

static void
ime_handle_key(struct hiragana_ime *ime, uint32_t key, uint32_t sym,
	       enum wl_keyboard_key_state state)
{
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	if (sym == ' ' && (ime->modifiers & ime->xkb.control_mask)) {
		ime->on = !ime->on;
		fprintf(stderr, "turning ime %s\n", ime->on ? "on" : "off");
		if (!ime->on) {
			input_method_preedit_string(ime->input_method, "", -1);
			ime->state = STATE_EMPTY;
		}
		return;
	}

	if (!ime->on)
		return;

	const char *output = NULL;
	if (sym == XKB_KEY_BackSpace || (sym >= 'a' && sym <= 'z')) {
		update_state(ime->state, sym, &ime->state, &output);
	}

	if (output && *output) {
		fprintf(stderr, "committing '%s'\n", output);
		input_method_commit_string(ime->input_method, output, -1);
	}

	if (ime->state != STATE_EMPTY || !(output && *output)) {
		const char *preedit_string = state_to_preedit(ime->state);
		fprintf(stderr, "preedit '%s'\n", preedit_string);
		input_method_preedit_string(ime->input_method, preedit_string, -1);
	}
}

static void reset_hiragana_ime(struct hiragana_ime *ime)
{
	ime->on = false;
	ime->state = STATE_EMPTY;
}

static void
input_method_reset(void *data,
		   struct input_method *input_method)
{
	struct hiragana_ime *ime = data;
	reset_hiragana_ime(ime);
}

static void
input_method_keymap(void *data,
		    struct wl_keyboard *wl_keyboard,
		    uint32_t format,
		    int32_t fd,
		    uint32_t size)
{
	struct hiragana_ime *ime = data;

	// XXX: this is copy-pasted from window.c
	char *map_str;

	if (!data) {
		close(fd);
		return;
	}

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}

	map_str = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
	if (map_str == MAP_FAILED) {
		close(fd);
		return;
	}

	ime->xkb.keymap = xkb_map_new_from_string(ime->xkb.context,
						  map_str,
						  XKB_KEYMAP_FORMAT_TEXT_V1,
						  0);
	munmap(map_str, size);
	close(fd);

	if (!ime->xkb.keymap) {
		fprintf(stderr, "failed to compile keymap\n");
		return;
	}

	ime->xkb.state = xkb_state_new(ime->xkb.keymap);
	if (!ime->xkb.state) {
		fprintf(stderr, "failed to create XKB state\n");
		xkb_map_unref(ime->xkb.keymap);
		ime->xkb.keymap = NULL;
		return;
	}

	ime->xkb.control_mask =
		1 << xkb_map_mod_get_index(ime->xkb.keymap, "Control");
	ime->xkb.alt_mask =
		1 << xkb_map_mod_get_index(ime->xkb.keymap, "Mod1");
	ime->xkb.shift_mask =
		1 << xkb_map_mod_get_index(ime->xkb.keymap, "Shift");
}

static void
input_method_key(void *data,
		 struct wl_keyboard *wl_keyboard,
		 uint32_t serial,
		 uint32_t time,
		 uint32_t key,
		 uint32_t state_w)
{
	struct hiragana_ime *ime = data;

	// XXX: copied from window.c
	uint32_t code, num_syms;
	enum wl_keyboard_key_state state = state_w;
	const xkb_keysym_t *syms;
	xkb_keysym_t sym;
	xkb_mod_mask_t mask;

	code = key + 8;
	if (!ime->xkb.state)
		return;

	num_syms = xkb_key_get_syms(ime->xkb.state, code, &syms);

	mask = xkb_state_serialize_mods(ime->xkb.state,
					XKB_STATE_DEPRESSED |
					XKB_STATE_LATCHED);
	ime->modifiers = 0;
	if (mask & ime->xkb.control_mask)
		ime->modifiers |= MOD_CONTROL_MASK;
	if (mask & ime->xkb.alt_mask)
		ime->modifiers |= MOD_ALT_MASK;
	if (mask & ime->xkb.shift_mask)
		ime->modifiers |= MOD_SHIFT_MASK;

	sym = XKB_KEY_NoSymbol;
	if (num_syms == 1)
		sym = syms[0];

	ime_handle_key(ime, key, sym, state);
}

static void
input_method_modifiers(void *data,
		       struct wl_keyboard *wl_keyboard,
		       uint32_t serial,
		       uint32_t mods_depressed,
		       uint32_t mods_latched,
		       uint32_t mods_locked,
		       uint32_t group)
{
	struct hiragana_ime *ime = data;

	// XXX: copied from window.c
	xkb_state_update_mask(ime->xkb.state, mods_depressed, mods_latched,
			      mods_locked, 0, 0, group);
}

static const struct wl_keyboard_listener input_method_keyboard_listener = {
	input_method_keymap,
	NULL, /* enter */
	NULL, /* leave */
	input_method_key,
	input_method_modifiers
};

static const struct input_method_listener input_method_listener = {
	input_method_reset
};

static void
global_handler(struct wl_display *display, uint32_t id,
	       const char *interface, uint32_t version, void *data)
{
	struct hiragana_ime *ime = data;

	if (!strcmp(interface, "input_method")) {
		ime->input_method = wl_display_bind(display, id, &input_method_interface);
		input_method_add_listener(ime->input_method,
					  &input_method_listener,
					  ime);
		ime->input_method_keyboard =
			input_method_request_keyboard(ime->input_method);
		wl_keyboard_add_listener(ime->input_method_keyboard,
					 &input_method_keyboard_listener,
					 ime);
	}
}

int
main(int argc, char *argv[])
{
	struct hiragana_ime ime;
	memset(&ime, 0, sizeof ime);

	reset_hiragana_ime(&ime);

	ime.xkb.context = xkb_context_new(0);
	if (ime.xkb.context == NULL) {
		fprintf(stderr, "Failed to create XKB context\n");
		return 1;
	}

	ime.display = display_create(argc, argv);
	if (ime.display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return 1;
	}

	wl_display_add_global_listener(display_get_display(ime.display),
				       global_handler, &ime);

	display_set_user_data(ime.display, &ime);

	display_run(ime.display);

	xkb_state_unref(ime.xkb.state);
	xkb_map_unref(ime.xkb.keymap);

	return 0;
}
