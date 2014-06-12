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
Scallion scallion;
/* needed because we dont link tor_main.c */
const char tor_git_revision[] = "";

static in_addr_t _scallion_HostnameCallback(const gchar* hostname) {
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
			scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, "unable to create client: error in getaddrinfo");
		}
		freeaddrinfo(info);
	}

	return addr;
}

static void _scallion_new(gint argc, gchar* argv[]) {
	scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_new called");

	gchar* usage = "Scallion USAGE: (\"dirauth\"|\"bridgeauth\"|\"relay\"|\"exitrelay\"|\"bridge\"|\"client\"|\"bridgeclient\") consensusWeightKiB ...\n";

	if(argc < 3) {
		scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, usage);
		return;
	}
	
	/* parse our arguments, program name is argv[0] */
	gchar* type = argv[1];
	gint weight = atoi(argv[2]);

	enum vtor_nodetype ntype;

	if(g_ascii_strncasecmp(type, "dirauth", strlen("dirauth")) == 0) {
		ntype = VTOR_DIRAUTH;
	} else if(g_ascii_strncasecmp(type, "hsauth", strlen("hsauth")) == 0) {
		ntype = VTOR_HSAUTH;
	} else if(g_ascii_strncasecmp(type, "bridgeauth", strlen("bridgeauth")) == 0) {
		ntype = VTOR_BRIDGEAUTH;
	} else if(g_ascii_strncasecmp(type, "relay", strlen("relay")) == 0) {
		ntype = VTOR_RELAY;
	} else if(g_ascii_strncasecmp(type, "exitrelay", strlen("exitrelay")) == 0) {
		ntype = VTOR_EXITRELAY;
	} else if(g_ascii_strncasecmp(type, "bridge", strlen("bridge")) == 0) {
		ntype = VTOR_BRIDGE;
	} else if(g_ascii_strncasecmp(type, "client", strlen("client")) == 0) {
		ntype = VTOR_CLIENT;
	} else if(g_ascii_strncasecmp(type, "bridgeclient", strlen("bridgeclient")) == 0) {
		ntype = VTOR_BRIDGECLIENT;
	} else {
		scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "Unrecognized relay type: %s", usage);
		return;
	}

	/* get the hostname, IP, and IP string */
	if(gethostname(scallion.hostname, 128) < 0) {
		scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "error getting hostname");
		return;
	}
	scallion.ip = _scallion_HostnameCallback(scallion.hostname);
	inet_ntop(AF_INET, &scallion.ip, scallion.ipstring, sizeof(scallion.ipstring));

	scallion.stor = scalliontor_new(scallion.shadowlibFuncs, scallion.hostname, ntype, weight, argc-3, &argv[3]);
}

static void _scallion_free() {
	scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "scallion_free called");
	scalliontor_free(scallion.stor);
}

static void _scallion_notify() {
	scallion.shadowlibFuncs->log(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "_scallion_notify called");
	scalliontor_notify(scallion.stor);
}

typedef void (*CRYPTO_lock_func)(int, int, const char*, int);
typedef unsigned long (*CRYPTO_id_func)(void);

/* called immediately after the plugin is loaded. shadow loads plugins once for
 * each worker thread. the GModule* is needed as a handle for g_module_symbol()
 * symbol lookups.
 * return NULL for success, or a string describing the error */
const gchar* g_module_check_init(GModule *module) {
	/* clear our memory before initializing */
	memset(&scallion, 0, sizeof(Scallion));

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

    scallion.opensslThreadSupport = 1;
#else
    /* no thread support */
    scallion.opensslThreadSupport = 0;
#endif

    /* setup libevent locks */
#ifdef EVTHREAD_USE_PTHREADS_IMPLEMENTED
    scallion.libeventThreadSupport = 1;
    if(evthread_use_pthreads()) {
        scallion.libeventHasError = 1;
    }
#else
    scallion.libeventThreadSupport = 0;
#endif

	return NULL;
}

/* called after g_module_check_init(), after shadow searches for __shadow_plugin_init__ */
void __shadow_plugin_init__(ShadowFunctionTable* shadowlibFuncs) {
	/* save the shadow functions we will use */
	scallion.shadowlibFuncs = shadowlibFuncs;

	/* tell shadow which functions it should call to manage nodes */
	shadowlibFuncs->registerPlugin(&_scallion_new, &_scallion_free, &_scallion_notify);

	/* log a message through Shadow's logging system */
	shadowlibFuncs->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "finished registering scallion plug-in state");

	/* print results of library initialization */
	if(scallion.opensslThreadSupport) {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_INFO, __FUNCTION__, "initialized openssl with thread support");
	} else {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "please rebuild openssl with threading support. expect segfaults.");
	}

	if(scallion.libeventThreadSupport) {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "initialized libevent with thread support using evthread_use_pthreads()");
	} else {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "please rebuild libevent with threading support, or link with event_pthread. expect segfaults.");
	}
	if(scallion.libeventHasError) {
	    shadowlibFuncs->log(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__, "there was an error in evthread_use_pthreads()");
	}
}

static void _scallion_cleanupOpenSSL() {
	EVP_cleanup();
	ERR_remove_state(0);
	ERR_free_strings();

#ifndef DISABLE_ENGINES
	ENGINE_cleanup();
#endif

	CONF_modules_unload(1);
	CRYPTO_cleanup_all_ex_data();
}

/* called immediately after the plugin is unloaded. shadow unloads plugins
 * once for each worker thread.
 */
void g_module_unload(GModule *module) {
	/* _scallion_cleanupOpenSSL should only be called once globally */
    shadowtorpreload_clear(&_scallion_cleanupOpenSSL);
	memset(&scallion, 0, sizeof(Scallion));
}
