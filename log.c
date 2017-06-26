/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <builtin/log.h>
#include <deai.h>
#include <helper.h>
#include <stdarg.h>
#include <stdio.h>

#include "di_internal.h"
#include "log.h"
#include "utils.h"

struct di_log {
	struct di_module;
	int log_level;
};

// Function exposed via di_object to be used by any plugins
static int di_log(struct di_object *o, int log_level, const char *str) {
	struct di_log *l = (void *)o;
	if (log_level > l->log_level)
		return 0;
	if (!str)
		str = "(nil)";
	return fputs(str, stderr);
}

// Public API to be used by C plugins
__attribute__((format(printf, 3, 4))) PUBLIC int
di_log_va(struct di_object *o, int log_level, const char *fmt, ...) {
	struct di_log *l = (void *)o;
	if (log_level > l->log_level)
		return 0;
	va_list ap;
	va_start(ap, fmt);
	int ret = vfprintf(stderr, fmt, ap);
	va_end(ap);

	return ret;
}

static void di_log_set_loglevel(struct di_log *l, int log_level) {
	l->log_level = log_level;
}

static int di_log_get(struct di_log *l, const char *prop) {
#define ret_ll(name)                                                                \
	if (strcmp(prop, #name) == 0)                                               \
		return DI_LOG_##name;

	LIST_APPLY(ret_ll, ERROR, WARN, INFO, DEBUG);
	if (strcmp(prop, "log_level") == 0)
		return l->log_level;
	return 0;
}

void di_init_log(struct deai *di) {
	auto l = di_new_module_with_type("log", struct di_log);
	if (!l)
		return;
	l->log_level = DI_LOG_ERROR;

	di_register_typed_method((void *)l, (di_fn_t)di_log, "log", DI_TYPE_NINT,
	                         2, DI_TYPE_NINT, DI_TYPE_STRING);

	di_register_typed_method((void *)l, (di_fn_t)di_log_set_loglevel,
	                         "__set_log_level", DI_TYPE_VOID, 1, DI_TYPE_NINT);

	di_register_typed_method((void *)l, (di_fn_t)di_log_get, "__get",
	                         DI_TYPE_NINT, 1, DI_TYPE_STRING);
	di_register_module(di, (void *)l);
}
