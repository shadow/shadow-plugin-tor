/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <sys/time.h>
#include <stdint.h>
#include <stdarg.h>

#include <glib.h>
#include <gmodule.h>

#include "torlog.h"

#define TOR_LIB_PREFIX "intercept_"

typedef int (*tor_open_socket_fp)(int, int, int);
typedef int (*tor_gettimeofday_fp)(struct timeval *);
typedef int (*spawn_func_fp)();
typedef int (*rep_hist_bandwidth_assess_fp)();
typedef int (*router_get_advertised_bandwidth_capped_fp)(void*);
typedef int (*event_base_loopexit_fp)();
typedef int (*crypto_global_cleanup_fp)(void);
typedef void (*mark_logs_temp_fp)(void);

/* the key used to store each threads version of their searched function library.
 * the use this key to retrieve this library when intercepting functions from tor.
 */
GStaticPrivate threadWorkerKey;

typedef struct _TorPreloadWorker TorPreloadWorker;
/* TODO fix func names */
struct _TorPreloadWorker {
	GModule* handle;
	tor_open_socket_fp a;
	tor_gettimeofday_fp b;
	spawn_func_fp d;
	rep_hist_bandwidth_assess_fp e;
	router_get_advertised_bandwidth_capped_fp f;
	event_base_loopexit_fp g;
	crypto_global_cleanup_fp h;
	mark_logs_temp_fp i;
	mark_logs_temp_fp j;
};

/* scallionpreload_init must be called before this so the worker gets created */
static TorPreloadWorker* _scallionpreload_getWorker() {
	/* get current thread's private worker object */
	TorPreloadWorker* worker = g_static_private_get(&threadWorkerKey);
	g_assert(worker);
	return worker;
}

static TorPreloadWorker* _scallionpreload_newWorker(GModule* handle) {
	TorPreloadWorker* worker = g_new0(TorPreloadWorker, 1);
	worker->handle = handle;
	return worker;
}

/* here we search and save pointers to the functions we need to call when
 * we intercept tor's functions. this is initialized for each thread, and each
 * thread has pointers to their own functions (each has its own version of the
 * plug-in state). We dont register these function locations, because they are
 * not *node* dependent, only *thread* dependent.
 */

void scallionpreload_init(GModule* handle) {
	TorPreloadWorker* worker = _scallionpreload_newWorker(handle);

	/* lookup all our required symbols in this worker's module, asserting success */
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "tor_open_socket", (gpointer*)&(worker->a)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "tor_gettimeofday", (gpointer*)&(worker->b)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "spawn_func", (gpointer*)&(worker->d)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "rep_hist_bandwidth_assess", (gpointer*)&(worker->e)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "router_get_advertised_bandwidth_capped", (gpointer*)&(worker->f)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "event_base_loopexit", (gpointer*)&(worker->g)));
	g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "crypto_global_cleanup", (gpointer*)&(worker->h)));
    g_assert(g_module_symbol(handle, TOR_LIB_PREFIX "mark_logs_temp", (gpointer*)&(worker->i)));
    g_assert(g_module_symbol(handle, "mark_logs_temp", (gpointer*)&(worker->j)));

	g_static_private_set(&threadWorkerKey, worker, g_free);
}

int tor_open_socket(int domain, int type, int protocol) {
	return _scallionpreload_getWorker()->a(domain, type, protocol);
}

void tor_gettimeofday(struct timeval *timeval) {
	_scallionpreload_getWorker()->b(timeval);
}

int spawn_func(void (*func)(void *), void *data) {
	return _scallionpreload_getWorker()->d(func, data);
}

int rep_hist_bandwidth_assess(void) {
	return _scallionpreload_getWorker()->e();
}

uint32_t router_get_advertised_bandwidth_capped(void *router) {
	return _scallionpreload_getWorker()->f(router);
}

/* struct event_base* base */
int event_base_loopexit(gpointer base, const struct timeval * t) {
	return _scallionpreload_getWorker()->g(base, t);
}

int crypto_global_cleanup(void) {
	return _scallionpreload_getWorker()->h();
}

void mark_logs_temp(void) {
    _scallionpreload_getWorker()->j(); // call real tor mark_logs_temp function
    _scallionpreload_getWorker()->i(); // reset our log callback
}
