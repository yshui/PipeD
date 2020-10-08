/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <assert.h>
#include <stdarg.h>

#include <deai/callable.h>
#include <deai/object.h>

#include "di_internal.h"
#include "utils.h"

struct di_typed_method {
	struct di_object_internal;

	/// The `this` pointer, which is passed as the first argument to the function.
	struct di_object *this;
	void (*nonnull fn)(void);

	/// Number of arguments
	int nargs;
	/// Return type
	di_type_t rtype;
	ffi_cif cif;
	/// Expected types of the arguments
	di_type_t atypes[];
};

struct di_closure {
	struct di_object_internal;

	/// Captured values, we don't explicitly save their types.
	/// `cif` contains the information of how to pass them to the function
	const void **cargs;
	void (*nonnull fn)(void);

	/// Number of actual arguments
	int nargs;
	/// Number of captured values
	int nargs0;
	/// Return type
	di_type_t rtype;
	// TODO(yshui) add a weak_ref type.
	/// Whether the captured values are weakly referenced
	bool weak_capture;
	ffi_cif cif;
	/// Expected types of the arguments
	di_type_t atypes[];
};

static inline void *allocate_for_return(di_type_t rtype) {
	auto sz = di_sizeof_type(rtype);
	if (sz < sizeof(ffi_arg)) {
		sz = sizeof(ffi_arg);
	}
	return malloc(sz);
}

static int
_di_typed_trampoline(ffi_cif *cif, void (*fn)(void), void *ret, const di_type_t *fnats,
                     int nargs0, const void *const *args0, struct di_tuple args) {
	assert(args.length == 0 || args.elements != NULL);
	assert(nargs0 == 0 || args0 != NULL);
	assert(args.length >= 0 && nargs0 >= 0);
	assert(args.length + nargs0 <= MAX_NARGS);

	struct di_variant *vars = args.elements;
	void *null_ptr = NULL;
	void **xargs = alloca((nargs0 + args.length) * sizeof(void *));
	memcpy(xargs, args0, sizeof(void *) * nargs0);
	memset(xargs + nargs0, 0, sizeof(void *) * args.length);

	int rc = 0;
	for (int i = nargs0; i < nargs0 + args.length; i++) {
		// Type check and implicit conversion
		// conversion between all types of integers are allowed
		// as long as there's no overflow
		rc = di_type_conversion(vars[i - nargs0].type, vars[i - nargs0].value,
		                        fnats[i - nargs0], xargs + i);
		if (rc != 0) {
			if (vars[i - nargs0].type == DI_TYPE_NIL) {
				struct di_array *arr;
				switch (fnats[i - nargs0]) {
				case DI_TYPE_OBJECT:
					rc = 0;
					xargs[i] = tmalloc(struct di_object *, 1);
					*(struct di_object **)xargs[i] =
					    di_new_object_with_type(struct di_object);
					break;
				case DI_TYPE_STRING:
				case DI_TYPE_POINTER:
					rc = 0;
					xargs[i] = &null_ptr;
					break;
				case DI_TYPE_ARRAY:
					arr = tmalloc(struct di_array, 1);
					*arr = DI_ARRAY_INIT;
					xargs[i] = arr;
					rc = 0;
					break;
				default:
					xargs[i] = NULL;
					goto out;
				}
			} else {
				// Conversion failed
				xargs[i] = NULL;
				goto out;
			}
		}
	}

	ffi_call(cif, fn, ret, (void *)xargs);

out:
	for (int i = nargs0; i < nargs0 + args.length; i++) {
		if (xargs[i] != vars[i - nargs0].value && xargs[i] != &null_ptr) {
			di_free_value(fnats[i - nargs0], xargs[i]);
			free((void *)xargs[i]);
		}
	}

	return rc;
}

static int
method_trampoline(struct di_object *o, di_type_t *rtype, void **ret, struct di_tuple t) {
	if (!di_check_type(o, "deai:method")) {
		return -EINVAL;
	}

	struct di_typed_method *tm = (void *)o;
	if (t.length != tm->nargs) {
		return -EINVAL;
	}

	*rtype = tm->rtype;

	if (di_sizeof_type(tm->rtype) != 0) {
		*ret = allocate_for_return(tm->rtype);
	} else {
		*ret = NULL;
	}

	void *this = tm->this;
	return _di_typed_trampoline(&tm->cif, tm->fn, *ret, tm->atypes + 1, 1,
	                            (const void *[]){&this}, t);
}

static int
closure_trampoline(struct di_object *o, di_type_t *rtype, void **ret, struct di_tuple t) {
	if (!di_check_type(o, "deai:closure")) {
		return -EINVAL;
	}

	struct di_closure *cl = (void *)o;
	if (t.length != cl->nargs) {
		return -EINVAL;
	}

	*rtype = cl->rtype;

	if (di_sizeof_type(cl->rtype) != 0) {
		*ret = allocate_for_return(cl->rtype);
	} else {
		*ret = NULL;
	}

	return _di_typed_trampoline(&cl->cif, cl->fn, *ret, cl->atypes + cl->nargs0,
	                            cl->nargs0, cl->cargs, t);
}

static void free_closure(struct di_object *o) {
	assert(di_check_type(o, "deai:closure"));

	struct di_closure *cl = (void *)o;
	for (int i = 0; i < cl->nargs0; i++) {
		if (!cl->weak_capture) {
			di_free_value(cl->atypes[i], (void *)cl->cargs[i]);
		}
		free((void *)cl->cargs[i]);
	}
	free(cl->cargs);
	free(cl->cif.arg_types);
}

PUBLIC struct di_closure *
di_create_closure(void (*fn)(void), di_type_t rtype, int nargs0, const di_type_t *cats,
                  const void *const *cargs, int nargs, const di_type_t *ats, bool weak_capture) {
	if (nargs0 < 0 || nargs < 0 || nargs0 + nargs > MAX_NARGS) {
		return ERR_PTR(-E2BIG);
	}

	for (int i = 0; i < nargs0; i++) {
		if (di_sizeof_type(cats[i]) == 0) {
			return ERR_PTR(-EINVAL);
		}
	}

	for (int i = 0; i < nargs; i++) {
		if (di_sizeof_type(ats[i]) == 0) {
			return ERR_PTR(-EINVAL);
		}
	}

	struct di_closure *cl = (void *)di_new_object(
	    sizeof(struct di_closure) + sizeof(di_type_t) * (nargs0 + nargs),
	    alignof(struct di_closure));

	cl->rtype = rtype;
	cl->call = closure_trampoline;
	cl->fn = fn;
	cl->dtor = free_closure;
	cl->nargs = nargs;
	cl->nargs0 = nargs0;
	cl->weak_capture = weak_capture;

	if (nargs0) {
		memcpy(cl->atypes, cats, sizeof(di_type_t) * nargs0);
	}
	if (nargs) {
		memcpy(cl->atypes + nargs0, ats, sizeof(di_type_t) * nargs);
	}

	auto ffiret = di_ffi_prep_cif(&cl->cif, nargs0 + nargs, cl->rtype, cl->atypes);
	if (ffiret != FFI_OK) {
		free(cl);
		return ERR_PTR(-EINVAL);
	}

	if (nargs0) {
		cl->cargs = malloc(sizeof(void *) * nargs0);
	}

	for (int i = 0; i < nargs0; i++) {
		void *dst = malloc(di_sizeof_type(cats[i]));
		if (!weak_capture) {
			di_copy_value(cats[i], dst, cargs[i]);
		} else {
			memcpy(dst, cargs[i], di_sizeof_type(cats[i]));
		}
		cl->cargs[i] = dst;
	}

	DI_OK_OR_RET_PTR(di_set_type((void *)cl, "deai:closure"));

	return cl;
}

static void free_method(struct di_typed_method *tm) {
	free(tm->cif.arg_types);
}

PUBLIC int di_add_method(struct di_object *o, const char *name, void (*fn)(void),
                         di_type_t rtype, int nargs, ...) {
	// TODO(yshui): convert to use closure
	if (nargs < 0 || nargs + 1 > MAX_NARGS) {
		return -EINVAL;
	}

	struct di_typed_method *f = (void *)di_new_object(
	    sizeof(struct di_typed_method) + sizeof(di_type_t) * (1 + nargs),
	    alignof(struct di_typed_method));

	f->rtype = rtype;
	f->call = method_trampoline;
	f->fn = fn;
	f->dtor = (void *)free_method;

	va_list ap;
	va_start(ap, nargs);
	for (unsigned int i = 0; i < nargs; i++) {
		f->atypes[i + 1] = va_arg(ap, di_type_t);
		if (f->atypes[i + 1] == DI_TYPE_NIL) {
			free(f);
			return -EINVAL;
		}
	}
	va_end(ap);

	f->atypes[0] = DI_TYPE_OBJECT;
	f->nargs = nargs;

	ffi_status ret = di_ffi_prep_cif(&f->cif, nargs + 1, f->rtype, f->atypes);

	if (ret != FFI_OK) {
		free(f);
		return -EINVAL;
	}

	DI_OK_OR_RET(di_set_type((void *)f, "deai:method"));

	f->this = o;
	return di_add_member_move(o, name, (di_type_t[]){DI_TYPE_OBJECT}, (void **)&f);
}

// va_args version of di_call_callable
PUBLIC int di_call_objectv(struct di_object *_obj, di_type_t *rtype, void **ret, va_list ap) {
	auto obj = (struct di_object_internal *)_obj;
	if (!obj->call) {
		return -EINVAL;
	}

	va_list ap2;
	struct di_tuple tu = DI_TUPLE_INIT;

	va_copy(ap2, ap);
	di_type_t t = va_arg(ap, di_type_t);
	while (t < DI_LAST_TYPE) {
		if (di_sizeof_type(t) == 0) {
			va_end(ap2);
			return -EINVAL;
		}
		tu.length++;
		va_arg_with_di_type(ap, t, NULL);
		t = va_arg(ap, di_type_t);
	}

	if (tu.length > 0) {
		tu.elements = alloca(sizeof(struct di_variant) * tu.length);
		for (unsigned int i = 0; i < tu.length; i++) {
			tu.elements[i].type = va_arg(ap2, di_type_t);
			assert(di_sizeof_type(tu.elements[i].type) != 0);
			tu.elements[i].value = alloca(di_sizeof_type(tu.elements[i].type));
			va_arg_with_di_type(ap2, tu.elements[i].type, tu.elements[i].value);
		}
		va_end(ap2);
	}

	return obj->call(_obj, rtype, ret, tu);
}

PUBLIC int di_call_object(struct di_object *o, di_type_t *rtype, void **ret, ...) {
	va_list ap;

	va_start(ap, ret);
	return di_call_objectv(o, rtype, ret, ap);
}

PUBLIC int di_call_objectt(struct di_object *nonnull obj, di_type_t *nonnull rt,
                           void *nullable *nonnull ret, struct di_tuple args) {
	auto internal = (struct di_object_internal *)obj;
	return internal->call(obj, rt, ret, args);
}
