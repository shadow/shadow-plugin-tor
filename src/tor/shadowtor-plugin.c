/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <openssl/rand.h>
#include <event2/thread.h>
#include <netinet/in.h>

#include "shadowtor.h"

/* my global structure to hold all variable, node-specific application state.
 * the name must not collide with other loaded modules globals. */
ShadowTor shadowtor;
/* needed because we dont link tor_main.c */
const char tor_git_revision[] = "";

static in_addr_t _shadowtorplugin_HostnameCallback(const gchar* hostname) {
	in_addr_t addr = 0;

	/* get the address in network order */
	if(g_ascii_strncasecmp(hostname, "none", 4) == 0) {
		addr = htonl(INADDR_NONE);
	} else if(g_ascii_strncasecmp(hostname, "localhost", 9) == 0) {
		addr = htonl(INADDR_LOOPBACK);
	} else {
		struct addrinfo* info;
		int result = getaddrinfo((gchar*) hostname, NULL, NULL, &info);
		if(result != -1 && info != NULL) {
			addr = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
		} else {
			shadowtor.shadowlibFuncs->log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "unable to create client: error in getaddrinfo");
		}
		freeaddrinfo(info);
	}

	return addr;
}

static void _shadowtorplugin_new(gint argc, gchar* argv[]) {
	shadowtor.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "shadowtorplugin_new called");

	gchar* usage = "USAGE: (\"please provide a torrc file or configs in the same format as tor command line options\n";
	if(argc == 1) {
		shadowtor.shadowlibFuncs->log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, usage);
		return;
	}
	
	/* get the hostname, IP, and IP string */
	if(gethostname(shadowtor.hostname, 128) < 0) {
		shadowtor.shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "error getting hostname");
		return;
	}
	shadowtor.ip = _shadowtorplugin_HostnameCallback(shadowtor.hostname);
	inet_ntop(AF_INET, &shadowtor.ip, shadowtor.ipstring, sizeof(shadowtor.ipstring));

	/* pass arguments to tor, program name is argv[0] */
	shadowtor.stor = shadowtor_new(shadowtor.shadowlibFuncs, shadowtor.hostname, argc-1, &argv[1]);
}

static void _shadowtorplugin_free() {
	shadowtor.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "shadowtorplugin_free called");
	shadowtor_free(shadowtor.stor);
}

static void _shadowtorplugin_notify() {
	shadowtor.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "_shadowtorplugin_notify called");
	shadowtor_notify(shadowtor.stor);
}

typedef void (*CRYPTO_lock_func)(int, int, const char*, int);
typedef unsigned long (*CRYPTO_id_func)(void);

/* called immediately after the plugin is loaded. shadow loads plugins once for
 * each worker thread. the GModule* is needed as a handle for g_module_symbol()
 * symbol lookups.
 * return NULL for success, or a string describing the error */
const gchar* g_module_check_init(GModule *module) {
	/* clear our memory before initializing */
	memset(&shadowtor, 0, sizeof(ShadowTor));

    /* handle multi-threading support*/

#define OPENSSL_THREAD_DEFINES
#include <openssl/opensslconf.h>
#if defined(OPENSSL_THREADS)
    /* thread support enabled, how many locks does openssl want */
    int nLocks = CRYPTO_num_locks();

    /* do all the symbol lookups we will need now, and init thread-specific
     * library of intercepted functions, init our global openssl locks. */
    shadowtorpreload_init(module, nLocks);

    /* make sure openssl uses Shadow's random sources and make crypto thread-safe
     * get function pointers through LD_PRELOAD */
    const RAND_METHOD* shadowtor_randomMethod = RAND_get_rand_method();
    CRYPTO_lock_func shadowtor_lockFunc = CRYPTO_get_locking_callback();
    CRYPTO_id_func shadowtor_idFunc = CRYPTO_get_id_callback();

    CRYPTO_set_locking_callback(shadowtor_lockFunc);
    CRYPTO_set_id_callback(shadowtor_idFunc);
    RAND_set_rand_method(shadowtor_randomMethod);

    shadowtor.opensslThreadSupport = 1;
#else
    /* no thread support */
    shadowtor.opensslThreadSupport = 0;
#endif

    /* setup libevent locks */
#ifdef EVTHREAD_USE_PTHREADS_IMPLEMENTED
    shadowtor.libeventThreadSupport = 1;
    if(evthread_use_pthreads()) {
        shadowtor.libeventHasError = 1;
    }
#else
    shadowtor.libeventThreadSupport = 0;
#endif

	return NULL;
}

/* called after g_module_check_init(), after shadow searches for __shadow_plugin_init__ */
void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	/* save the shadow functions we will use */
	shadowtor.shadowlibFuncs = shadowlibFuncs;

	/* tell shadow which functions it should call to manage nodes */
	shadowlibFuncs->registerPlugin(&_shadowtorplugin_new, &_shadowtorplugin_free, &_shadowtorplugin_notify);

	/* log a message through Shadow's logging system */
	shadowlibFuncs->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "finished registering shadowtor plug-in state");

	/* print results of library initialization */
	if(shadowtor.opensslThreadSupport) {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "initialized openssl with thread support");
	} else {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "please rebuild openssl with threading support. expect segfaults.");
	}

	if(shadowtor.libeventThreadSupport) {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "initialized libevent with thread support using evthread_use_pthreads()");
	} else {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "please rebuild libevent with threading support, or link with event_pthread. expect segfaults.");
	}
	if(shadowtor.libeventHasError) {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "there was an error in evthread_use_pthreads()");
	}
}

/* called immediately after the plugin is unloaded. shadow unloads plugins
 * once for each worker thread.
 */
void g_module_unload(GModule *module) {
	/* _shadowtorplugin_cleanupOpenSSL should only be called once globally */
    shadowtorpreload_clear();
	memset(&shadowtor, 0, sizeof(ShadowTor));
}
