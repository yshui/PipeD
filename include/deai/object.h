/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

// XXX merge into deai.h

#include <deai/compiler.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum di_type {
	DI_TYPE_VOID = 0,
	DI_TYPE_BOOL,           // boolean, no implicit conversion to number types
	DI_TYPE_NINT,           // native int
	DI_TYPE_NUINT,          // native unsigned int
	DI_TYPE_UINT,           // uint64_t
	DI_TYPE_INT,            // int64_t
	DI_TYPE_FLOAT,          // platform dependent, double
	DI_TYPE_POINTER,        // Generic pointer, void *
	DI_TYPE_OBJECT,         // pointer to di_object
	DI_TYPE_STRING,         // utf-8 string, char *
	DI_TYPE_STRING_LITERAL,        // utf-8 string literal, const char *
	DI_TYPE_ARRAY,                 // struct di_array
	DI_TYPE_TUPLE,                 // array with variable element type
	DI_TYPE_NIL,
	DI_LAST_TYPE
} di_type_t;

enum di_object_state {
	DI_OBJECT_STATE_HEALTHY,
	DI_OBJECT_STATE_APOPTOSIS,
	DI_OBJECT_STATE_ORPHANED,
	DI_OBJECT_STATE_DEAD,
};

struct di_tuple;
struct di_object;
typedef void (*di_fn_t)(void);
typedef int (*di_call_fn_t)(struct di_object *_Nonnull, di_type_t *_Nonnull rt,
                            void *_Nullable *_Nonnull ret, struct di_tuple);
struct di_signal;
struct di_listener;
struct di_callable;
struct di_object {
	struct di_member *_Nullable members;
	struct di_signal *_Nullable signals;

	void (*_Nullable dtor)(struct di_object *_Nonnull);
	di_call_fn_t _Nullable call;

	uint64_t ref_count;
	uint16_t state;
};

struct di_array {
	uint64_t length;
	void *_Nullable arr;
	di_type_t elem_type;
};

struct di_tuple {
	uint64_t length;
	void *_Nonnull *_Nullable tuple;
	di_type_t *_Nullable elem_type;
};

struct di_module {
	struct di_object;
	struct deai *_Nonnull di;
	char padding[56];
};

struct di_member {
	char *_Nonnull name;
	void *_Nonnull data;
	di_type_t type;
	bool writable;
	bool own;
};

int di_callx(struct di_object *_Nonnull o, const char *_Nonnull name,
             di_type_t *_Nonnull rt, void *_Nullable *_Nonnull ret, ...);
int di_rawcallx(struct di_object *_Nonnull o, const char *_Nonnull name,
                di_type_t *_Nonnull rt, void *_Nullable *_Nonnull ret, ...);
int di_rawcallxn(struct di_object *_Nonnull o, const char *_Nonnull name,
                 di_type_t *_Nonnull rt, void *_Nullable *_Nonnull ret,
                 struct di_tuple);

int di_setx(struct di_object *_Nonnull o, const char *_Nonnull prop, di_type_t type,
            void *_Nullable val);
int di_rawgetx(struct di_object *_Nonnull o, const char *_Nonnull prop,
               di_type_t *_Nonnull type, const void *_Nullable *_Nonnull ret);
int di_rawgetxt(struct di_object *_Nonnull o, const char *_Nonnull prop,
                di_type_t type, const void *_Nullable *_Nonnull ret);
int di_getx(struct di_object *_Nonnull no, const char *_Nonnull prop,
            di_type_t *_Nonnull type, const void *_Nullable *_Nonnull ret);
int di_getxt(struct di_object *_Nonnull o, const char *_Nonnull prop, di_type_t type,
             const void *_Nullable *_Nonnull ret);

int di_set_type(struct di_object *_Nonnull o, const char *_Nonnull type);
const char *_Nonnull di_get_type(struct di_object *_Nonnull o);
bool di_check_type(struct di_object *_Nonnull o, const char *_Nonnull type);

int di_add_ref_member(struct di_object *_Nonnull o, const char *_Nonnull name,
                      bool writable, di_type_t t, void *_Nonnull address);
int di_add_value_member(struct di_object *_Nonnull o, const char *_Nonnull name,
                        bool writable, di_type_t t, ...);
int di_remove_member(struct di_object *_Nonnull o, const char *_Nonnull name);
struct di_member *_Nullable di_lookup(struct di_object *_Nonnull o,
                                      const char *_Nonnull name);
struct di_object *_Nullable di_new_object(size_t sz);

struct di_listener *_Nullable di_listen_to(struct di_object *_Nonnull o,
                                           const char *_Nonnull name,
                                           struct di_object *_Nonnull h);
struct di_listener *_Nullable di_listen_to_once(struct di_object *_Nonnull o,
                                                const char *_Nonnull name,
                                                struct di_object *_Nonnull h,
                                                bool once);
int di_stop_listener(struct di_listener *_Nullable);
int di_emitn(struct di_object *_Nonnull, const char *_Nonnull name, struct di_tuple);
void di_apoptosis(struct di_object *_Nonnull);

void di_clear_listeners(struct di_object *_Nonnull);

void di_ref_object(struct di_object *_Nonnull);
void di_unref_object(struct di_object *_Nonnull);

void di_free_tuple(struct di_tuple);
void di_free_array(struct di_array);
void di_free_value(di_type_t, void *_Nonnull);
void di_copy_value(di_type_t t, void *_Nullable dest, const void *_Nullable src);

static inline size_t di_sizeof_type(di_type_t t) {
	switch (t) {
	case DI_TYPE_VOID:
	case DI_LAST_TYPE:
	case DI_TYPE_NIL:
	default: return 0;
	case DI_TYPE_FLOAT: return sizeof(double);
	case DI_TYPE_ARRAY: return sizeof(struct di_array);
	case DI_TYPE_TUPLE: return sizeof(struct di_tuple);
	case DI_TYPE_UINT:
	case DI_TYPE_INT: return 8;
	case DI_TYPE_NUINT: return sizeof(unsigned int);
	case DI_TYPE_NINT: return sizeof(int);
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_OBJECT:
	case DI_TYPE_POINTER: return sizeof(void *);
	case DI_TYPE_BOOL: return sizeof(bool);
	}
}

// Workaround for _Generic limitations, see:
// http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1930.htm
#define di_typeid(x)                                                                \
	_Generic((x*)0, \
	struct di_array *: DI_TYPE_ARRAY, \
	struct di_tuple *: DI_TYPE_TUPLE, \
	int *: DI_TYPE_NINT, \
	unsigned int *: DI_TYPE_NUINT, \
	int64_t *: DI_TYPE_INT, \
	uint64_t *: DI_TYPE_UINT, \
	char **: DI_TYPE_STRING, \
	const char **: DI_TYPE_STRING, \
	struct di_object **: DI_TYPE_OBJECT, \
	void **: DI_TYPE_POINTER, \
	double *: DI_TYPE_FLOAT, \
	void *: DI_TYPE_VOID, \
	bool *: DI_TYPE_BOOL \
)

#define di_typeof(expr) di_typeid(typeof(expr))

#define di_set_return(v)                                                            \
	do {                                                                        \
		*rtype = di_typeof(v);                                              \
		typeof(v) *retv;                                                    \
		if (!*ret)                                                          \
			*ret = calloc(1, di_min_return_size(sizeof(v)));            \
		retv = *(typeof(v) **)ret;                                          \
		*retv = v;                                                          \
	} while (0);

#define DI_ARRAY_NIL ((struct di_array){0, NULL, DI_TYPE_NIL})
#define DI_TUPLE_NIL ((struct di_tuple){0, NULL, NULL})

#define define_object_cleanup(t)                                                    \
	static inline void free_##t(struct t **ptr) {                               \
		if (*ptr)                                                           \
			di_unref_object((struct di_object *)*ptr);                  \
		*ptr = NULL;                                                        \
	}
#define with_object_cleanup(t) with_cleanup(free_##t) struct t *
