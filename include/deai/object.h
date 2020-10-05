/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#pragma once

// XXX merge into deai.h

#include <deai/compiler.h>

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/// deai type ids. Use negative numbers for invalid types.
///
/// Notes:
///
/// * Arrays are passed by value, which contains a pointer to the array storage. It has the
///   same effect as C++ vectors. You can modify the elements of the array, but if you
///   change the storage pointer, it won't be reflected in the actual array. The same
///   applies to tuples as well.
typedef enum di_type {
	DI_TYPE_UNIT = 0,
	DI_TYPE_ANY,                   // any, only used as element type for empty arrays.
	DI_TYPE_BOOL,                  // boolean, no implicit conversion to number types
	DI_TYPE_NINT,                  // native int
	DI_TYPE_NUINT,                 // native unsigned int
	DI_TYPE_UINT,                  // uint64_t
	DI_TYPE_INT,                   // int64_t
	DI_TYPE_FLOAT,                 // platform dependent, double
	DI_TYPE_POINTER,               // Generic pointer, void *
	DI_TYPE_OBJECT,                // pointer to di_object
	DI_TYPE_STRING,                // utf-8 string, char *
	DI_TYPE_STRING_LITERAL,        // utf-8 string literal, const char *
	DI_TYPE_ARRAY,                 // struct di_array
	DI_TYPE_TUPLE,                 // array with variable element type
	DI_LAST_TYPE,                  // bottom, the empty type
} di_type_t;

struct di_tuple;
struct di_object;
typedef int (*di_call_fn_t)(struct di_object *nonnull, di_type_t *nonnull rt,
                            void *nullable *nonnull ret, struct di_tuple);
struct di_signal;
struct di_listener;
struct di_callable;
struct di_member;
struct di_object {
	struct di_member *nullable members;
	struct di_signal *nullable signals;

	void (*nullable dtor)(struct di_object *nonnull);
	di_call_fn_t nullable call;

	uint64_t ref_count;
	uint8_t destroyed;
};

struct di_array {
	uint64_t length;
	void *nullable arr;
	di_type_t elem_type;
};

struct di_tuple {
	uint64_t length;
	void *nonnull *nullable tuple;
	di_type_t *nullable elem_type;
};

struct di_module {
	struct di_object;
	struct deai *nonnull di;
	char padding[56];
};

/// Fetch member object `name` from object `o`, then call the member object with `args`.
///
/// # Errors
///
/// * EINVAL: if the member object is not callable.
/// * ENOENT: if the member object doesn't exist.
///
/// @param[out] rt The return type of the function
/// @param[out] ret The return value
int di_rawcallxn(struct di_object *nonnull o, const char *nonnull name,
                 di_type_t *nonnull rt, void *nullable *nonnull ret, struct di_tuple args);

/// Like `di_rawcallxn`, but also calls getter functions to fetch the member object. And
/// the arguments are pass as variadic arguments. Arguments are passed as pairs of type
/// ids and values, end with DI_LAST_TYPE.
///
/// You shouldn't use this function directly, use the `di_call` macro if you are using C.
int di_callx(struct di_object *nonnull o, const char *nonnull name, di_type_t *nonnull rt,
             void *nullable *nonnull ret, ...);

/// Change the value of member `prop` of object `o`. It will call the setter if one exists.
///
/// NOTE: currently di_setx cannot change the type of `prop`, it will convert `val` to the
/// type of `prop`. This WILL change!
///
/// @param[in] type The type of the value
/// @param[in] val The value
int di_setx(struct di_object *nonnull o, const char *nonnull prop, di_type_t type,
            void *nullable val);

/// Fetch a member with name `prop` from an object `o`, without calling the getter
/// functions. The value is cloned, then returned.
///
/// # Errors
///
/// * ENOENT: member `prop` not found.
///
/// @param[out] type Type of the value
/// @param[out] ret The value
/// @return 0 for success, or an error code.
int di_rawgetx(struct di_object *nonnull o, const char *nonnull prop,
               di_type_t *nonnull type, void *nullable *nonnull ret);

/// Like `di_rawgetx`, but tries to do automatic type conversion to the desired type `type`.
///
/// # Errors
///
/// Same as `di_rawgetx`, plus:
///
/// * EINVAL: if type conversion failes.
///
/// @param[out] ret The value
/// @return 0 for success, or an error code.
int di_rawgetxt(struct di_object *nonnull o, const char *nonnull prop, di_type_t type,
                void *nullable *nonnull ret);

/// Like `di_rawgetx`, but also calls getter functions if `prop` is not found.
/// The getter functions are the generic getter "__get", or the specialized getter
/// "__get_<prop>"
int di_getx(struct di_object *nonnull no, const char *nonnull prop,
            di_type_t *nonnull type, void *nullable *nonnull ret);

/// Like `di_rawgetxt`, but also calls getter functions if `prop` is not found.
int di_getxt(struct di_object *nonnull o, const char *nonnull prop, di_type_t type,
             void *nullable *nonnull ret);

/// Set the "__type" member of the object `o`. By convention, "__type" names the type of
/// the object. Type names should be formated as "<namespace>:<type>". The "deai"
/// namespace is used by deai.
///
/// @param[in] type The type name, must be a string literal
int di_set_type(struct di_object *nonnull o, const char *nonnull type);

/// Get the type name of the object
///
/// @return A const string, the type name. It shouldn't be freed.
const char *nonnull di_get_type(struct di_object *nonnull o);

/// Check if the type of the object is `type`
bool di_check_type(struct di_object *nonnull o, const char *nonnull type);

int nonnull_all di_add_member_move(struct di_object *nonnull o, const char *nonnull name,
                                   di_type_t *nonnull, void *nonnull address);
int nonnull_all di_add_member_ref(struct di_object *nonnull o, const char *nonnull name,
                                  di_type_t, void *nonnull address);
int nonnull_all di_add_member_clone(struct di_object *nonnull o, const char *nonnull name,
                                    di_type_t, ...);
int di_remove_member(struct di_object *nonnull o, const char *nonnull name);
struct di_member *nullable di_lookup(struct di_object *nonnull o, const char *nonnull name);
struct di_object *nullable di_new_object(size_t sz);

struct di_listener *nullable di_listen_to(struct di_object *nonnull o, const char *nonnull name,
                                          struct di_object *nullable h);
struct di_listener *nullable di_listen_to_once(struct di_object *nonnull o,
                                               const char *nonnull name,
                                               struct di_object *nullable h, bool once);

// Unscribe from a signal from the listener side. __detach is not called in this case
// It's guaranteed after calling this, the handler and __detach method will never be
// called
int di_stop_listener(struct di_listener *nullable);
int di_emitn(struct di_object *nonnull, const char *nonnull name, struct di_tuple);
// Call object dtor, remove all listeners and members from the object. And free the
// memory
// if the ref count drop to 0 after this process
void di_destroy_object(struct di_object *nonnull);

// Detach all listeners attached to object, __detach of listeners will be called
void di_clear_listeners(struct di_object *nonnull);

struct di_object *nonnull di_ref_object(struct di_object *nonnull);
void di_unref_object(struct di_object *nonnull);

void di_free_tuple(struct di_tuple);
void di_free_array(struct di_array);
void di_free_value(di_type_t, void *nonnull);
void di_copy_value(di_type_t t, void *nullable dest, const void *nullable src);

static inline unused size_t di_sizeof_type(di_type_t t) {
	switch (t) {
	case DI_TYPE_UNIT:
	case DI_TYPE_ANY:
	case DI_LAST_TYPE:
		return 0;
	case DI_TYPE_FLOAT:
		return sizeof(double);
	case DI_TYPE_ARRAY:
		return sizeof(struct di_array);
	case DI_TYPE_TUPLE:
		return sizeof(struct di_tuple);
	case DI_TYPE_UINT:
	case DI_TYPE_INT:
		return 8;
	case DI_TYPE_NUINT:
		return sizeof(unsigned int);
	case DI_TYPE_NINT:
		return sizeof(int);
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL:
	case DI_TYPE_OBJECT:
	case DI_TYPE_POINTER:
		return sizeof(void *);
	case DI_TYPE_BOOL:
		return sizeof(bool);
	default:
		assert(false);
		unreachable();
	}
}

// Workaround for _Generic limitations, see:
// http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1930.htm
#define di_typeid(x)                                                                     \
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
	void *: DI_TYPE_UNIT, \
	bool *: DI_TYPE_BOOL \
)

#define di_typeof(expr) di_typeid(typeof(expr))

#define di_set_return(v)                                                                 \
	do {                                                                             \
		*rtype = di_typeof(v);                                                   \
		typeof(v) *retv;                                                         \
		if (!*ret)                                                               \
			*ret = calloc(1, di_min_return_size(sizeof(v)));                 \
		retv = *(typeof(v) **)ret;                                               \
		*retv = v;                                                               \
	} while (0);

/// The unit value in array form, []
static const struct di_array unused DI_ARRAY_UNIT = {0, NULL, DI_TYPE_ANY};
/// The unit value in tuple form, ()
static const struct di_tuple unused DI_TUPLE_UNIT = {0, NULL, NULL};

#define define_object_cleanup(t)                                                         \
	static inline void free_##t(struct t *nullable *nonnull ptr) {                   \
		if (*ptr)                                                                \
			di_unref_object((struct di_object *)*ptr);                       \
		*ptr = NULL;                                                             \
	}
#define with_object_cleanup(t) with_cleanup(free_##t) struct t *

unused define_object_cleanup(di_object);
