/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai.h>
#include <ev.h>
#include <helper.h>
#include <builtin/event.h>

#include "di_internal.h"
#include "event.h"
#include "utils.h"

struct di_ioev {
	struct di_object;
	ev_io evh;
	struct ev_loop *loop;
};

struct di_timer {
	struct di_object;
	ev_timer evt;
	struct ev_loop *loop;
};

struct di_periodic {
	struct di_object;
	ev_periodic pt;
	struct ev_loop *loop;
};

struct di_evmodule {
	struct di_module;
	struct ev_loop *loop;
};

static void di_ioev_callback(EV_P_ ev_io *w, int revents) {
	auto ev = container_of(w, struct di_ioev, evh);
	if (revents & EV_READ)
		di_emit_signal((void *)ev, "read", NULL);
	if (revents & EV_WRITE)
		di_emit_signal((void *)ev, "write", NULL);
}

static void di_timer_callback(EV_P_ ev_timer *t, int revents) {
	auto d = container_of(t, struct di_timer, evt);
	ev_timer_stop(d->loop, t);
	di_emit_signal_v((void *)d, "elapsed", ev_now(EV_A));
}

static void
di_periodic_callback(EV_P_ ev_periodic *w, int revents) {
	auto p = container_of(w, struct di_periodic, pt);
	di_emit_signal_v((void *)p, "triggered", ev_now(EV_A));
}

static void di_start_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	ev_io_start(ev->loop, &ev->evh);
}

static void di_ioev_dtor(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	ev_io_stop(ev->loop, &ev->evh);
}

static void di_timer_dtor(struct di_object *obj) {
	struct di_timer *ev = (void *)obj;
	ev_timer_stop(ev->loop, &ev->evt);
}
static void di_timer_again(struct di_timer *obj) {
	ev_timer_again(obj->loop, &obj->evt);
}
static void di_timer_set(struct di_timer *obj, uint64_t t) {
	obj->evt.repeat = t;
	ev_timer_again(obj->loop, &obj->evt);
}
static struct di_object *di_create_ioev(struct di_object *obj, int fd, int t) {
	struct di_evmodule *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_ioev);

	unsigned int flags = 0;
	if (t & IOEV_READ)
		flags |= EV_READ;
	if (t & IOEV_WRITE)
		flags |= EV_WRITE;

	ev_io_init(&ret->evh, di_ioev_callback, fd, flags);
	ret->loop = em->loop;

	auto startfn =
	    di_create_typed_method((di_fn_t)di_start_ioev, "start", DI_TYPE_VOID, 0);
	di_register_typed_method((void *)ret, (void *)startfn);

	di_dtor(ret, di_ioev_dtor);

	if (t & IOEV_READ)
		di_register_signal((void *)ret, "read", 0);
	if (t & IOEV_WRITE)
		di_register_signal((void *)ret, "write", 0);
	return (void *)ret;
}
static struct di_object *di_create_timer(struct di_object *obj, uint64_t timeout) {
	struct di_evmodule *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	ret->loop = em->loop;

	di_dtor(ret, di_timer_dtor);
	di_register_typed_method((void *)ret,
	                         di_create_typed_method((di_fn_t)di_timer_again,
	                                                "start", DI_TYPE_VOID, 0));
	di_register_typed_method((void *)ret,
	                         di_create_typed_method((di_fn_t)di_timer_again,
	                                                "again", DI_TYPE_VOID, 0));

	// Set the timeout and restart the timer
	di_register_typed_method(
	    (void *)ret, di_create_typed_method((di_fn_t)di_timer_set, "__set_timeout",
	                                        DI_TYPE_VOID, 1, DI_TYPE_UINT));

	ev_init(&ret->evt, di_timer_callback);
	ret->evt.repeat = timeout;
	di_register_signal((void *)ret, "elapsed", 1, DI_TYPE_FLOAT);
	return (void *)ret;
}
static void
di_periodic_dtor(struct di_periodic *p) {
	ev_periodic_stop(p->loop, &p->pt);
}
static void di_periodic_set(struct di_periodic *p, double interval, double offset) {
	ev_periodic_set(&p->pt, offset, interval, NULL);
	ev_periodic_again(p->loop, &p->pt);
}
static struct di_object *
di_create_periodic(struct di_evmodule *evm, double interval, double offset) {
	auto ret = di_new_object_with_type(struct di_periodic);
	ret->loop = evm->loop;

	di_dtor(ret, di_periodic_dtor);
	di_register_typed_method(
	    (void *)ret,
	    di_create_typed_method((di_fn_t)di_periodic_set, "set", DI_TYPE_VOID,
	                           2, DI_TYPE_FLOAT, DI_TYPE_FLOAT));

	ev_periodic_init(&ret->pt, di_periodic_callback, offset, interval, NULL);
	ev_periodic_start(evm->loop, &ret->pt);
	di_register_signal((void *)ret, "triggered", 1, DI_TYPE_FLOAT);
	return (void *)ret;
}
void di_init_event_module(struct deai *di) {
	auto em = di_new_module_with_type("event", struct di_evmodule);

	di_register_typed_method(
	    (void *)em,
	    di_create_typed_method((di_fn_t)di_create_ioev, "fdevent",
	                           DI_TYPE_OBJECT, 2, DI_TYPE_NINT, DI_TYPE_NINT));

	di_register_typed_method(
	    (void *)em, di_create_typed_method((di_fn_t)di_create_timer, "timer",
	                                       DI_TYPE_OBJECT, 1, DI_TYPE_UINT));

	di_register_typed_method(
	    (void *)em,
	    di_create_typed_method((di_fn_t)di_create_periodic, "periodic",
	                           DI_TYPE_OBJECT, 2, DI_TYPE_FLOAT, DI_TYPE_FLOAT));
	em->loop = di->loop;
	di_register_module(di, (void *)em);
}
