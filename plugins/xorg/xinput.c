/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtin/log.h>
#include <deai/deai.h>
#include <deai/helper.h>
#include <deai/compiler.h>

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <xcb/xinput.h>

#include "xorg.h"

struct di_xorg_xinput {
	struct di_xorg_ext;

	xcb_input_event_mask_t ec;
	char mask[4];

	// XI_LASTEVENT is not defined in xcb,
	// but should be 26
	unsigned int listener_count[27];
};

struct di_xorg_xinput_device {
	struct di_object;

	int deviceid;
	struct di_xorg_xinput *xi;
};

#define set_mask(a, m) (a)[(m) >> 3] |= (1 << ((m)&7))
#define clear_mask(a, m) (a)[(m) >> 3] &= ~(1 << ((m)&7))
#define get_mask(a, m) (a)[(m) >> 3] & (1 << ((m)&7))

static void di_xorg_xi_start_listen_for_event(struct di_xorg_xinput *xi, int ev) {
	di_getmi(xi->dc->x->di, log);
	if (ev > 26) {
		if (logm)
			di_log_va(logm, DI_LOG_ERROR, "invalid xi event number %d",
			          ev);
		return;
	}

	xi->listener_count[ev]++;
	if (xi->listener_count[ev] > 1)
		return;

	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	set_mask(xi->mask, ev);
	auto cookie =
	    xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
	auto e = xcb_request_check(xi->dc->c, cookie);
	if (e && logm)
		di_log_va(logm, DI_LOG_ERROR, "select events failed\n");
}

static void di_xorg_xi_stop_listen_for_event(struct di_xorg_xinput *xi, int ev) {
	di_getmi(xi->dc->x->di, log);
	if (ev > 26) {
		if (logm)
			di_log_va(logm, DI_LOG_ERROR, "invalid xi event number %d",
			          ev);
		return;
	}

	assert(xi->listener_count[ev] > 0);
	xi->listener_count[ev]--;
	if (xi->listener_count[ev] > 0)
		return;

	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	clear_mask(xi->mask, ev);
	auto cookie =
	    xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);
	auto e = xcb_request_check(xi->dc->c, cookie);
	if (e && logm)
		di_log_va(logm, DI_LOG_ERROR, "select events failed\n");
}

static void enable_hierarchy_event(struct di_xorg_xinput *xi) {
	di_xorg_xi_start_listen_for_event(xi, XCB_INPUT_HIERARCHY);
}

static void UNUSED disable_hierarchy_event(struct di_xorg_xinput *xi) {
	di_xorg_xi_stop_listen_for_event(xi, XCB_INPUT_HIERARCHY);
}

static void di_xorg_free_xinput(struct di_xorg_ext *x) {
	// clear event mask when free
	if (!x->dc)
		return;

	struct di_xorg_xinput *xi = (void *)x;
	memset(xi->mask, 0, xi->ec.mask_len * 4);
	auto scrn = screen_of_display(xi->dc->c, xi->dc->dflt_scrn);
	xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);

	auto cookie =
	    xcb_input_xi_select_events_checked(xi->dc->c, scrn->root, 1, &xi->ec);

	di_getmi(x->dc->x->di, log);
	auto e = xcb_request_check(xi->dc->c, cookie);
	if (e && logm)
		di_log_va(logm, DI_LOG_ERROR, "select events failed\n");
}

define_trivial_cleanup_t(xcb_input_xi_query_device_reply_t);

static xcb_input_xi_device_info_t *
xcb_input_get_device_info(xcb_connection_t *c, xcb_input_device_id_t deviceid,
                          xcb_input_xi_query_device_reply_t **rr) {
	*rr = NULL;
	auto r = xcb_input_xi_query_device_reply(
	    c, xcb_input_xi_query_device(c, deviceid), NULL);
	if (!r)
		return NULL;

	auto ri = xcb_input_xi_query_device_infos_iterator(r);
	xcb_input_xi_device_info_t *ret = NULL;
	for (; ri.rem; xcb_input_xi_device_info_next(&ri)) {
		auto info = ri.data;
		if (info->deviceid == deviceid) {
			ret = info;
			break;
		}
	}

	*rr = r;
	return ret;
}

static char *di_xorg_xinput_get_device_name(struct di_xorg_xinput_device *dev) {
	if (!dev->xi->dc)
		return strdup("unknown");

	with_cleanup_t(xcb_input_xi_query_device_reply_t) rr;
	auto info = xcb_input_get_device_info(dev->xi->dc->c, dev->deviceid, &rr);
	if (!info)
		return strdup("unknown");
	return strndup(xcb_input_xi_device_info_name(info),
	               xcb_input_xi_device_info_name_length(info));
}

static char *di_xorg_xinput_get_device_use(struct di_xorg_xinput_device *dev) {
	if (!dev->xi->dc)
		return strdup("unknown");

	with_cleanup_t(xcb_input_xi_query_device_reply_t) rr;
	auto info = xcb_input_get_device_info(dev->xi->dc->c, dev->deviceid, &rr);
	if (!info)
		return strdup("unknown");

	switch (info->type) {
	case XCB_INPUT_DEVICE_TYPE_MASTER_KEYBOARD: return strdup("master keyboard");
	case XCB_INPUT_DEVICE_TYPE_SLAVE_KEYBOARD: return strdup("keyboard");
	case XCB_INPUT_DEVICE_TYPE_MASTER_POINTER: return strdup("master pointer");
	case XCB_INPUT_DEVICE_TYPE_SLAVE_POINTER: return strdup("pointer");
	default: return strdup("unknown");
	}
}

define_trivial_cleanup_t(xcb_input_list_input_devices_reply_t);

#if 0
const char *possible_types[] = {
    "KEYBOARD",   "MOUSE",      "TABLET",    "TOUCHSCREEN", "TOUCHPAD",
    "BARCODE",    "BUTTONBOX",  "KNOB_BOX",  "ONE_KNOB",    "NINE_KNOB",
    "TRACKBALL",  "QUADRATURE", "ID_MODULE", "SPACEBALL",   "DATAGLOVE",
    "EYETRACKER", "CURSORKEYS", "FOOTMOUSE", "JOYSTICK", NULL
};
#endif

static char *di_xorg_xinput_get_device_type(struct di_xorg_xinput_device *dev) {
	if (!dev->xi->dc)
		return strdup("unknown");

	struct di_xorg_connection *dc = dev->xi->dc;

	with_cleanup_t(xcb_input_list_input_devices_reply_t) r =
	    xcb_input_list_input_devices_reply(
	        dc->c, xcb_input_list_input_devices(dc->c), NULL);

	auto di = xcb_input_list_input_devices_devices_iterator(r);
	for (; di.rem; xcb_input_device_info_next(&di))
		if (di.data->device_id == dev->deviceid)
			break;

	const char *dname = di_xorg_get_atom_name(dc, di.data->device_type);
	if (!dname) {
		// fprintf(stderr, "%d\n", di.data->device_type);
		return strdup("unknown");
	}

	char *ret = strdup(dname);

	for (int i = 0; ret[i]; i++)
		ret[i] = tolower(ret[i]);
	return ret;
}

static int di_xorg_xinput_get_device_id(struct di_object *o) {
	struct di_xorg_xinput_device *dev = (void *)o;
	return dev->deviceid;
}

define_trivial_cleanup_t(xcb_input_xi_get_property_reply_t);
define_trivial_cleanup_t(xcb_input_xi_change_property_items_t);
define_trivial_cleanup_t(char);

static void di_xorg_xinput_set_prop(struct di_xorg_xinput_device *dev,
                                    const char *key, struct di_array arr) {
	if (!dev->xi->dc)
		return;

	struct di_xorg_connection *dc = dev->xi->dc;

	di_getmi(dc->x->di, log);
	xcb_generic_error_t *e;
	auto prop_atom = di_xorg_intern_atom(dc, key, &e);
	if (e)
		return;

	auto float_atom = di_xorg_intern_atom(dc, "FLOAT", &e);

	with_cleanup_t(xcb_input_xi_get_property_reply_t) prop =
	    xcb_input_xi_get_property_reply(
	        dc->c, xcb_input_xi_get_property(dc->c, dev->deviceid, 0,
	                                              prop_atom, XCB_ATOM_ANY, 0, 0),
	        NULL);

	if (prop->type == XCB_ATOM_NONE) {
		// non-existent property should be silently ignored
		if (logm)
			di_log_va(logm, DI_LOG_DEBUG,
			          "setting non-existent property: %s\n", key);
		return;
	}

	if ((prop->type == float_atom || prop->type == XCB_ATOM_ATOM) &&
	    prop->format != 32) {
		di_log_va(logm, DI_LOG_ERROR,
		          "Xorg return invalid format for float/atom type: %d\n",
		          prop->format);
		return;
	}

	with_cleanup_t(xcb_input_xi_change_property_items_t) item =
	    tmalloc(xcb_input_xi_change_property_items_t, arr.length);
	int step = prop->format / 8;
	with_cleanup_t(char) data = malloc(step * (arr.length));
	for (int i = 0; i < arr.length; i++) {
		int64_t i64;
		float f;
		const char *str;
		void *dst = data + step * i;
		void *src = arr.arr + di_sizeof_type(arr.elem_type) * i;

		switch (arr.elem_type) {
		case DI_TYPE_INT:
		case DI_TYPE_UINT:
			i64 = *(int64_t *)src;
			f = i64;
			break;
		case DI_TYPE_NINT:
		case DI_TYPE_NUINT:
			i64 = *(int *)src;
			f = i64;
			break;
		case DI_TYPE_FLOAT:
			if (prop->type != float_atom)
				goto err;
			f = *(double *)src;
			break;
		case DI_TYPE_STRING:
			if (prop->type != XCB_ATOM_ATOM)
				goto err;
			str = *(const char **)src;
			i64 = di_xorg_intern_atom(dc, str, &e);
			if (e)
				return;
			break;
		default: goto err;
		}

		if (prop->type == XCB_ATOM_INTEGER ||
		    prop->type == XCB_ATOM_CARDINAL) {
			if (arr.elem_type == DI_TYPE_FLOAT ||
			    arr.elem_type == DI_TYPE_STRING)
				goto err;
			switch (prop->format) {
			case 8:
				*(int8_t *)dst = i64;
				item[i].data8 = dst;
				break;
			case 16:
				*(int16_t *)dst = i64;
				item[i].data16 = dst;
				break;
			case 32:
				*(int32_t *)dst = i64;
				item[i].data32 = dst;
				break;
			}
		} else if (prop->type == float_atom) {
			if (arr.elem_type == DI_TYPE_STRING)
				goto err;
			*(float *)dst = f;
			item[i].data32 = dst;
		} else if (prop->type == XCB_ATOM_ATOM) {
			if (arr.elem_type != DI_TYPE_STRING)
				goto err;
			*(int32_t *)dst = i64;
			item[i].data32 = dst;
		}
	}

	auto err = xcb_request_check(
	    dc->c, xcb_input_xi_change_property_aux_checked(
	                    dc->c, dev->deviceid, XCB_PROP_MODE_REPLACE,
	                    prop->format, prop_atom, prop->type, arr.length, item));

	if (err)
		di_log_va(logm, DI_LOG_ERROR, "Failed to set property '%s'\n", key);
	(void)err;
	return;
err:
	di_log_va(logm, DI_LOG_ERROR, "Try to set xinput property '%s' with wrong "
	                              "type of data %d\n",
	          key, arr.elem_type);
}

static struct di_array
di_xorg_xinput_get_prop(struct di_xorg_xinput_device *dev, const char *name) {
	if (!dev->xi->dc)
		return DI_ARRAY_NIL;

	struct di_xorg_connection *dc = dev->xi->dc;

	di_getmi(dc->x->di, log);
	xcb_generic_error_t *e;
	struct di_array ret = {0, NULL, DI_TYPE_NIL};
	auto prop_atom = di_xorg_intern_atom(dc, name, &e);

	if (e)
		return ret;

	auto float_atom = di_xorg_intern_atom(dc, "FLOAT", &e);

	with_cleanup_t(xcb_input_xi_get_property_reply_t) prop =
	    xcb_input_xi_get_property_reply(
	        dc->c, xcb_input_xi_get_property(dc->c, dev->deviceid, 0,
	                                              prop_atom, XCB_ATOM_ANY, 0, 0),
	        NULL);

	if (prop->type == XCB_ATOM_NONE)
		return ret;

	size_t plen = prop->bytes_after;
	free(prop);

	prop = xcb_input_xi_get_property_reply(
	    dc->c, xcb_input_xi_get_property(dc->c, dev->deviceid, 0,
	                                          prop_atom, XCB_ATOM_ANY, 0, plen),
	    NULL);

	if (prop->type == XCB_ATOM_NONE)
		return ret;

	if (prop->type == XCB_ATOM_INTEGER || prop->type == XCB_ATOM_CARDINAL)
		ret.elem_type = DI_TYPE_INT;
	else if (prop->type == XCB_ATOM_ATOM)
		ret.elem_type = DI_TYPE_STRING;
	else if (prop->type == float_atom)
		ret.elem_type = DI_TYPE_FLOAT;
	else {
		if (logm)
			di_log_va(logm, DI_LOG_WARN, "Unknown property type %d\n",
			          prop->type);
		return ret;
	}

	if (prop->format != 8 && prop->format != 16 && prop->format != 32) {
		if (logm)
			di_log_va(logm, DI_LOG_WARN,
			          "Xorg returns invalid format %d\n", prop->format);
		return ret;
	}
	if ((prop->type == float_atom || prop->type == XCB_ATOM_ATOM) &&
	    prop->format != 32) {
		di_log_va(logm, DI_LOG_WARN,
		          "Xorg return invalid format for float/atom %d\n",
		          prop->format);
		return ret;
	}

	void *buf = xcb_input_xi_get_property_items(prop);
	ret.length = prop->num_items;
	ret.arr = calloc(ret.length, di_sizeof_type(ret.elem_type));
	void *curr = ret.arr;
#define read(n) ((uint##n##_t *)buf)[i]
	for (int i = 0; i < prop->num_items; i++) {
		if (ret.elem_type == DI_TYPE_INT) {
			int64_t *tmp = curr;
			switch (prop->format) {
			case 8: *tmp = read(8); break;
			case 16: *tmp = read(16); break;
			case 32: *tmp = read(32); break;
			default: __builtin_unreachable();
			}
		} else if (ret.elem_type == DI_TYPE_STRING) {
			const char **tmp = curr;
			*tmp = strdup(di_xorg_get_atom_name(dc, read(32)));
		} else {
			// float
			double *tmp = curr;
			*tmp = ((float *)buf)[i];
		}
		curr += di_sizeof_type(ret.elem_type);
	}
#undef read
	return ret;
}

static struct di_object *di_xorg_xinput_props(struct di_xorg_xinput_device *dev) {
	auto obj = di_new_object_with_type(struct di_xorg_xinput_device);
	obj->deviceid = dev->deviceid;
	obj->xi = dev->xi;

	di_method(obj, "__get", di_xorg_xinput_get_prop, char *);
	di_method(obj, "__set", di_xorg_xinput_set_prop, char *, struct di_array);
	return (void *)obj;
}

static void free_xi_device_object(struct di_xorg_xinput_device *dev) {
	di_unref_object((void *)dev->xi);
}

static struct di_object *
di_xorg_make_object_for_devid(struct di_xorg_xinput *xi, int deviceid) {
	auto obj = di_new_object_with_type(struct di_xorg_xinput_device);

	obj->deviceid = deviceid;
	obj->xi = xi;

	di_ref_object((void *)xi);

	obj->dtor = (void *)free_xi_device_object;

	di_method(obj, "__get_name", di_xorg_xinput_get_device_name);
	di_method(obj, "__get_use", di_xorg_xinput_get_device_use);
	di_method(obj, "__get_id", di_xorg_xinput_get_device_id);
	di_method(obj, "__get_type", di_xorg_xinput_get_device_type);
	di_method(obj, "__get_props", di_xorg_xinput_props);

	// const char *ty;
	// DI_GET((void *)obj, "name", ty);
	// fprintf(stderr, "NAME: %s", ty);
	return (void *)obj;
}

static struct di_array di_xorg_get_all_devices(struct di_xorg_xinput *xi) {
	if (!xi->dc)
		return DI_ARRAY_NIL;

	with_cleanup_t(xcb_input_xi_query_device_reply_t) r =
	    xcb_input_xi_query_device_reply(
	        xi->dc->c, xcb_input_xi_query_device(xi->dc->c, 0), NULL);
	auto ri = xcb_input_xi_query_device_infos_iterator(r);

	int ndev = 0;
	for (; ri.rem; xcb_input_xi_device_info_next(&ri))
		ndev++;

	struct di_array ret;
	ret.length = ndev;
	ret.elem_type = DI_TYPE_OBJECT;
	if (ndev)
		ret.arr = tmalloc(void *, ndev);
	else
		ret.arr = NULL;

	struct di_object **arr = ret.arr;
	ri = xcb_input_xi_query_device_infos_iterator(r);
	for (int i = 0; i < ndev; i++) {
		arr[i] = di_xorg_make_object_for_devid(xi, ri.data->deviceid);
		xcb_input_xi_device_info_next(&ri);
	}

	return ret;
}

static int handle_xinput_event(struct di_xorg_xinput *xi, xcb_generic_event_t *ev) {
	if (ev->response_type != XCB_GE_GENERIC)
		return 1;

	xcb_ge_generic_event_t *gev = (void *)ev;
	if (gev->extension != xi->opcode)
		return 1;

	// di_getm(xi->dc->x->di, log);
	if (gev->event_type == XCB_INPUT_HIERARCHY) {
		xcb_input_hierarchy_event_t *hev = (void *)ev;
		auto hevi = xcb_input_hierarchy_infos_iterator(hev);
		for (; hevi.rem; xcb_input_hierarchy_info_next(&hevi)) {
			auto info = hevi.data;
			auto obj =
			    di_xorg_make_object_for_devid(xi, info->deviceid);
			// di_log_va(log, DI_LOG_DEBUG, "hierarchy change %u %u\n",
			// info->deviceid, info->flags);
			if (info->flags & XCB_INPUT_HIERARCHY_MASK_SLAVE_ADDED)
				di_emit(xi, "new-device", obj);
			if (info->flags & XCB_INPUT_HIERARCHY_MASK_DEVICE_ENABLED)
				di_emit(xi, "device-enabled", obj);
			if (info->flags & XCB_INPUT_HIERARCHY_MASK_DEVICE_DISABLED)
				di_emit(xi, "device-disabled", obj);
			di_unref_object(obj);
		}
	}
	return 0;
}

struct di_xorg_ext *new_xinput(struct di_xorg_connection *dc) {
	char *extname = "XInputExtension";
	if (!xorg_has_extension(dc->c, extname))
		return NULL;

	auto cookie = xcb_query_extension(dc->c, strlen(extname), extname);
	auto r = xcb_query_extension_reply(dc->c, cookie, NULL);
	if (!r)
		return NULL;

	auto xi = di_new_object_with_type(struct di_xorg_xinput);
	xi->ec.mask_len = 1;        // 4 bytes unit
	xi->ec.deviceid = 0;        // alldevice
	xi->opcode = r->major_opcode;
	xi->handle_event = (void *)handle_xinput_event;
	xi->dc = dc;
	xi->extname = "xinput";
	xi->free = di_xorg_free_xinput;

	free(r);

	// TODO: only enable if there are listeners?
	enable_hierarchy_event(xi);

	di_method(xi, "__get_devices", di_xorg_get_all_devices);
	return (void *)xi;
}
