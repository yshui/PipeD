/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <limits.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <deai/builtin/event.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include "uthash.h"
#include "utils.h"

struct di_file {
	struct di_module;
};

struct di_file_watch_entry {
	char *fname;
	int wd;
	UT_hash_handle hh, hh2;
};

struct di_file_watch {
	struct di_object;
	int fd;

	struct di_object *fdev;
	struct di_listener *fdev_listener;

	struct di_file_watch_entry *byname, *bywd;
};

static int di_file_ioev(struct di_file_watch *o, struct di_object *fd) {
	char evbuf[sizeof(struct inotify_event) + NAME_MAX + 1];
	struct inotify_event *ev = (void *)evbuf;
	int ret = read(o->fd, evbuf, sizeof(evbuf));
	int off = 0;
	while (off < ret) {
		char *path = "";
		if (ev->len > 0)
			path = ev->name;

		struct di_file_watch_entry *we = NULL;
		HASH_FIND_INT(o->bywd, &ev->wd, we);
		if (!we)
			// ???
			continue;
#define emit(m, name)                                                               \
	if (ev->mask & m)                                                           \
	di_emit_from_object((void *)o, name, we->fname, path)
		emit(IN_CREATE, "create");
		emit(IN_ACCESS, "access");
		emit(IN_ATTRIB, "attrib");
		emit(IN_CLOSE_WRITE, "close-write");
		emit(IN_CLOSE_NOWRITE, "close-nowrite");
		emit(IN_DELETE, "delete");
		emit(IN_DELETE_SELF, "delete-self");
		emit(IN_MODIFY, "modify");
		emit(IN_MOVE_SELF, "move-self");
		emit(IN_OPEN, "open");
		if (ev->mask & IN_MOVED_FROM)
			di_emit_from_object((void *)o, "moved-from", we->fname, path,
			                    (uint64_t)ev->cookie);
		if (ev->mask & IN_MOVED_TO)
			di_emit_from_object((void *)o, "moved-to", we->fname, path,
			                    (uint64_t)ev->cookie);
#undef emit
		off += sizeof(struct inotify_event) + ev->len;
		ev = (void *)(evbuf + off);
	}
	return 0;
}

static int di_file_add_watch(struct di_file_watch *fw, const char *path) {
	if (!path)
		return -EINVAL;

	int ret = inotify_add_watch(fw->fd, path, IN_ALL_EVENTS);
	if (ret >= 0) {
		auto we = tmalloc(struct di_file_watch_entry, 1);
		we->wd = ret;
		we->fname = strdup(path);

		HASH_ADD_INT(fw->bywd, wd, we);
		HASH_ADD_KEYPTR(hh2, fw->byname, we->fname, strlen(we->fname), we);
		ret = 0;
	}
	return ret;
}

static void di_file_add_many_watch(struct di_file_watch *fw, struct di_array paths) {
	if (paths.length > 0 && paths.elem_type != DI_TYPE_STRING)
		return;
	const char **arr = paths.arr;
	for (int i = 0; i < paths.length; i++)
		di_file_add_watch(fw, arr[i]);
}

static int di_file_rm_watch(struct di_file_watch *fw, const char *path) {
	if (!path)
		return -EINVAL;

	struct di_file_watch_entry *we = NULL;
	HASH_FIND(hh2, fw->byname, path, strlen(path), we);
	if (!we)
		return -ENOENT;

	inotify_rm_watch(fw->fd, we->wd);
	HASH_DEL(fw->bywd, we);
	HASH_DELETE(hh2, fw->byname, we);
	free(we->fname);
	free(we);
	return 0;
}

static void di_file_watch_dtor(struct di_file_watch *fw) {
	close(fw->fd);

	struct di_file_watch_entry *we, *twe;
	HASH_ITER(hh, fw->bywd, we, twe) {
		HASH_DEL(fw->bywd, we);
		HASH_DELETE(hh2, fw->byname, we);
		free(we->fname);
		free(we);
	}

	di_unref_object((void *)fw->fdev_listener);
	di_unref_object(fw->fdev);
}

static struct di_object *di_file_new_watch(struct di_file *f, struct di_array paths) {
	if (paths.length > 0 && paths.elem_type != DI_TYPE_STRING)
		return di_new_error("Argument needs to be an array of strings");

	auto ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0)
		return di_new_error("Failed to create new inotify file descriptor");

	auto fw = di_new_object_with_type(struct di_file_watch);
	fw->fd = ifd;

	di_method(fw, "add", di_file_add_many_watch, struct di_array);
	di_method(fw, "add_one", di_file_add_watch, char *);
	di_method(fw, "remove", di_file_rm_watch, char *);

	fw->dtor = (void *)di_file_watch_dtor;

	di_register_signal(
	    (void *)fw, "moved-from", 3,
	    (di_type_t[]){DI_TYPE_STRING, DI_TYPE_STRING, DI_TYPE_UINT});
	di_register_signal(
	    (void *)fw, "moved-to", 3,
	    (di_type_t[]){DI_TYPE_STRING, DI_TYPE_STRING, DI_TYPE_UINT});
#define add_signal(name)                                                            \
	di_register_signal((void *)fw, name, 2,                                     \
	                   (di_type_t[]){DI_TYPE_STRING, DI_TYPE_STRING})
#define add_signals(...) LIST_APPLY(add_signal, SEP_COLON, __VA_ARGS__)
	add_signals("move-self", "access", "attrib", "close-write", "close-nowrite",
	            "create", "delete", "delete-self", "modify", "open");
#undef add_signal
#undef add_signals

	di_getm(f->di, event, di_new_error("Can't find event module"));
	di_callr(eventm, "fdevent", fw->fdev, fw->fd, IOEV_READ);

	auto cl = di_create_closure(
	    (void *)di_file_ioev, DI_TYPE_VOID, 1, (di_type_t[]){DI_TYPE_OBJECT},
	    (const void *[]){&fw}, 1, (di_type_t[]){DI_TYPE_OBJECT});
	fw->fdev_listener = di_add_listener(fw->fdev, "read", (void *)cl);
	di_unref_object((void *)cl);

	const char **arr = paths.arr;
	for (int i = 0; i < paths.length; i++)
		di_file_add_watch(fw, arr[i]);

	di_call(fw->fdev, "start");
	return (void *)fw;
}
PUBLIC int di_plugin_init(struct deai *di) {
	auto fm = di_new_module_with_type(struct di_file);
	fm->di = di;
	di_method(fm, "watch", di_file_new_watch, struct di_array);
	di_register_module(di, "file", (void *)fm);
	di_unref_object((void *)fm);
	return 0;
}
