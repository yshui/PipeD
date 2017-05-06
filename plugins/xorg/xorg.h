/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

#include "list.h"

struct di_xorg {
	struct di_module;
};

struct di_atom_entry;

struct di_xorg_connection {
	struct di_object;
	struct di_xorg *x;
	xcb_connection_t *c;
	int dflt_scrn;
	struct di_object *xcb_fd;
	struct di_listener *xcb_fdlistener;
	struct di_xorg_ext *xi;

	struct di_atom_entry *a_byatom, *a_byname;
};

struct di_xorg_ext {
	struct di_object;
	struct di_xorg_connection *dc;
	struct di_xorg_ext **e;
	const char *id;

	uint8_t opcode;

	void (*free)(struct di_xorg_ext *);
	void (*handle_event)(struct di_xorg_ext *, xcb_ge_generic_event_t *ev);
};

static inline xcb_screen_t *screen_of_display(xcb_connection_t *c, int screen) {
	xcb_screen_iterator_t iter;

	iter = xcb_setup_roots_iterator(xcb_get_setup(c));
	for (; iter.rem; --screen, xcb_screen_next(&iter))
		if (screen == 0)
			return iter.data;

	return NULL;
}

static inline bool xorg_has_extension(xcb_connection_t *c, const char *name) {
	auto cookie = xcb_list_extensions(c);
	auto r = xcb_list_extensions_reply(c, cookie, NULL);
	if (!r)
		return false;

	auto i = xcb_list_extensions_names_iterator(r);
	for (; i.rem; xcb_str_next(&i))
		if (strncmp(xcb_str_name(i.data), name, xcb_str_name_length(i.data)) == 0) {
			free(r);
			return true;
		}
	free(r);
	return false;
}

void di_xorg_free_sub(struct di_xorg_ext *x);
const char *di_xorg_get_atom_name(struct di_xorg_connection *xc, xcb_atom_t atom);
xcb_atom_t di_xorg_intern_atom(struct di_xorg_connection *xc, const char *name, xcb_generic_error_t **e);