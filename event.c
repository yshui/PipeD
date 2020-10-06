/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <deai/builtin/event.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include <ev.h>

#include "di_internal.h"
#include "event.h"
#include "utils.h"

struct di_ioev {
	struct di_object_internal;
	ev_io evh;
	struct deai *di;
	struct di_listener *d;
	bool running;
};

struct di_timer {
	struct di_object_internal;
	ev_timer evt;
	struct deai *di;
	struct di_listener *d;
};

struct di_periodic {
	struct di_object_internal;
	ev_periodic pt;
	struct deai *di;
	struct di_listener *d;
};

static void di_ioev_callback(EV_P_ ev_io *w, int revents) {
	auto ev = container_of(w, struct di_ioev, evh);
	int dt = 0;
	if (revents & EV_READ) {
		di_emit(ev, "read");
		dt |= IOEV_READ;
	}
	if (revents & EV_WRITE) {
		di_emit(ev, "write");
		dt |= IOEV_WRITE;
	}
	di_emit(ev, "io", dt);
}

static void di_timer_callback(EV_P_ ev_timer *t, int revents) {
	auto d = container_of(t, struct di_timer, evt);
	double now = ev_now(EV_A);
	ev_timer_stop(d->di->loop, t);
	di_emit(d, "elapsed", now);
}

static void di_periodic_callback(EV_P_ ev_periodic *w, int revents) {
	auto p = container_of(w, struct di_periodic, pt);
	double now = ev_now(EV_A);
	di_emit(p, "triggered", now);
}

static void di_start_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	if (!ev->di)
		return;
	if (ev->running)
		return;
	ev_io_start(ev->di->loop, &ev->evh);
	ev->running = true;
}

static void di_stop_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	if (!ev->di)
		return;
	if (!ev->running)
		return;
	ev_io_stop(ev->di->loop, &ev->evh);
	ev->running = false;
}

static void di_toggle_ioev(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	if (!ev->di)
		return;
	if (ev->running)
		ev_io_stop(ev->di->loop, &ev->evh);
	else
		ev_io_start(ev->di->loop, &ev->evh);
	ev->running = !ev->running;
}

static void di_ioev_dtor(struct di_object *obj) {
	struct di_ioev *ev = (void *)obj;
	di_stop_listener(ev->d);
	ev_io_stop(ev->di->loop, &ev->evh);
	di_unref_object((void *)ev->di);
	ev->di = NULL;
}

static struct di_object *di_create_ioev(struct di_object *obj, int fd, int t) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_ioev);
	di_set_type((void *)ret, "deai.builtin.event:ioev");

	unsigned int flags = 0;
	if (t & IOEV_READ)
		flags |= EV_READ;
	if (t & IOEV_WRITE)
		flags |= EV_WRITE;

	ev_io_init(&ret->evh, di_ioev_callback, fd, flags);
	ev_io_start(em->di->loop, &ret->evh);
	ret->di = em->di;
	di_ref_object((void *)ret->di);

	ret->d = di_listen_to_destroyed((void *)em->di, trivial_destroyed_handler,
	                                (void *)ret);

	di_method(ret, "start", di_start_ioev);
	di_method(ret, "stop", di_stop_ioev);
	di_method(ret, "toggle", di_toggle_ioev);
	di_method(ret, "close", di_destroy_object);

	ret->dtor = di_ioev_dtor;
	ret->running = true;
	return (void *)ret;
}

static void di_timer_dtor(struct di_object *obj) {
	struct di_timer *ev = (void *)obj;
	di_stop_listener(ev->d);
	ev_timer_stop(ev->di->loop, &ev->evt);
	di_unref_object((void *)ev->di);
	ev->di = NULL;
}

static void di_timer_again(struct di_timer *obj) {
	if (!obj->di)
		return;

	ev_timer_again(obj->di->loop, &obj->evt);
}

static void di_timer_set(struct di_timer *obj, double t) {
	if (!obj->di)
		return;

	obj->evt.repeat = t;
	ev_timer_again(obj->di->loop, &obj->evt);
}

static struct di_object *di_create_timer(struct di_object *obj, double timeout) {
	struct di_module *em = (void *)obj;
	auto ret = di_new_object_with_type(struct di_timer);
	di_set_type((void *)ret, "deai.builtin.event:timer");
	ret->di = em->di;
	di_ref_object((void *)ret->di);

	ret->dtor = di_timer_dtor;
	di_method(ret, "again", di_timer_again);

	// Set the timeout and restart the timer
	di_method(ret, "__set_timeout", di_timer_set, double);

	ret->d = di_listen_to_destroyed((void *)em->di, trivial_destroyed_handler,
	                                (void *)ret);

	ev_init(&ret->evt, di_timer_callback);
	ret->evt.repeat = timeout;
	ev_timer_again(em->di->loop, &ret->evt);
	return (void *)ret;
}

static void periodic_dtor(struct di_periodic *p) {
	di_stop_listener(p->d);
	ev_periodic_stop(p->di->loop, &p->pt);
	di_unref_object((void *)p->di);
	p->di = NULL;
}

static void periodic_set(struct di_periodic *p, double interval, double offset) {
	if (!p->di)
		return;
	ev_periodic_set(&p->pt, offset, interval, NULL);
	ev_periodic_again(p->di->loop, &p->pt);
}

static struct di_object *
di_create_periodic(struct di_module *evm, double interval, double offset) {
	auto ret = di_new_object_with_type(struct di_periodic);
	di_set_type((void *)ret, "deai.builtin.event:periodic");
	ret->di = evm->di;
	di_ref_object((void *)ret->di);

	ret->dtor = (void *)periodic_dtor;
	di_method(ret, "set", periodic_set, double, double);
	ev_periodic_init(&ret->pt, di_periodic_callback, offset, interval, NULL);
	ev_periodic_start(ret->di->loop, &ret->pt);

	ret->d = di_listen_to_destroyed((void *)evm->di, trivial_destroyed_handler,
	                                (void *)ret);

	return (void *)ret;
}

struct di_prepare {
	ev_prepare;
	struct di_module *evm;
};

static void di_prepare(EV_P_ ev_prepare *w, int revents) {
	struct di_prepare *dep = (void *)w;
	di_emit(dep->evm, "prepare");
}

void di_init_event(struct deai *di) {
	auto em = di_new_module(di);

	di_method(em, "fdevent", di_create_ioev, int, int);
	di_method(em, "timer", di_create_timer, double);
	di_method(em, "periodic", di_create_periodic, double, double);

	auto dep = tmalloc(struct di_prepare, 1);
	dep->evm = em;
	ev_prepare_init(dep, di_prepare);
	ev_prepare_start(di->loop, (ev_prepare *)dep);

	di_register_module(di, "event", &em);
}
