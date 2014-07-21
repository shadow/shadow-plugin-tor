/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <sys/time.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include <openssl/crypto.h>

#include <glib.h>
#include <gmodule.h>

#define SHADOWTOR_PREFIX "shadowtorinterpose_"

/* tor functions */

typedef int (*tor_open_socket_fp)(int, int, int);
typedef int (*tor_gettimeofday_fp)(struct timeval *);
typedef int (*spawn_func_fp)();
typedef int (*rep_hist_bandwidth_assess_fp)();
typedef int (*router_get_advertised_bandwidth_capped_fp)(void*);
typedef int (*crypto_global_init_fp)(int, const char*, const char*);
typedef int (*crypto_global_cleanup_fp)(void);
typedef void (*tor_ssl_global_init_fp)(void);
typedef void (*mark_logs_temp_fp)(void);

/* libevent functions */

typedef int (*event_base_loopexit_fp)();

/* openssl functions */

typedef void (*AES_encrypt_fp)(const unsigned char*, unsigned char*, const void*);
typedef void (*AES_decrypt_fp)(const unsigned char*, unsigned char*, const void*);
typedef void (*AES_ctr128_encrypt_fp)(const unsigned char*, unsigned char*, const void*);
typedef void (*AES_ctr128_decrypt_fp)(const unsigned char*, unsigned char*, const void*);
typedef int (*EVP_Cipher_fp)(void*, unsigned char*, const unsigned char*, unsigned int);
typedef void (*RAND_seed_fp)(const void*, int);
typedef void (*RAND_add_fp)(const void*, int, double);
typedef int (*RAND_poll_fp)();
typedef int (*RAND_bytes_fp)(unsigned char*, int);
typedef int (*RAND_pseudo_bytes_fp)(unsigned char*, int);
typedef void (*RAND_cleanup_fp)();
typedef int (*RAND_status_fp)();
typedef const void* (*RAND_get_rand_method_fp)();
typedef const void* (*RAND_SSLeay_fp)();

typedef struct _InterposeFuncs InterposeFuncs;
struct _InterposeFuncs {
	tor_open_socket_fp tor_open_socket;
	tor_gettimeofday_fp tor_gettimeofday;
	spawn_func_fp spawn_func;
	rep_hist_bandwidth_assess_fp rep_hist_bandwidth_assess;
	router_get_advertised_bandwidth_capped_fp router_get_advertised_bandwidth_capped;
	crypto_global_init_fp crypto_global_init;
	crypto_global_cleanup_fp crypto_global_cleanup;
	tor_ssl_global_init_fp tor_ssl_global_init;
	mark_logs_temp_fp mark_logs_temp;

	event_base_loopexit_fp event_base_loopexit;

    AES_encrypt_fp AES_encrypt;
    AES_decrypt_fp AES_decrypt;
    AES_ctr128_encrypt_fp AES_ctr128_encrypt;
    AES_ctr128_decrypt_fp AES_ctr128_decrypt;
    EVP_Cipher_fp EVP_Cipher;

    RAND_seed_fp RAND_seed;
    RAND_add_fp RAND_add;
    RAND_poll_fp RAND_poll;
    RAND_bytes_fp RAND_bytes;
    RAND_pseudo_bytes_fp RAND_pseudo_bytes;
    RAND_cleanup_fp RAND_cleanup;
    RAND_status_fp RAND_status;
    RAND_get_rand_method_fp RAND_get_rand_method;
    RAND_SSLeay_fp RAND_SSLeay;
};

typedef struct _PreloadWorker PreloadWorker;
struct _PreloadWorker {
	GModule* handle;
	InterposeFuncs tor;
	InterposeFuncs shadowtor;
};

/*
 * the key used to store each threads version of their searched function library.
 * the use this key to retrieve this library when intercepting functions from tor.
 * The PrelaodWorker we store here will be freed using g_free automatically when
 * the thread exits.
 */
static GPrivate threadPreloadWorkerKey = G_PRIVATE_INIT(g_free);

/* preload_init must be called before this so the worker gets created */
static PreloadWorker* _shadowtorpreload_getWorker() {
	/* get current thread's private worker object */
	PreloadWorker* worker = g_private_get(&threadPreloadWorkerKey);
	if(!worker) {
	    worker = g_new0(PreloadWorker, 1);
        g_private_set(&threadPreloadWorkerKey, worker);
	}
	g_assert(worker);
	return worker;
}

/* forward declarations */
static void _shadowtorpreload_cryptoSetup(int);
static void _shadowtorpreload_cryptoTeardown();

/*
 * here we search and save pointers to the functions we need to call when
 * we intercept tor's functions. this is initialized for each thread, and each
 * thread has pointers to their own functions (each has its own version of the
 * plug-in state). We dont register these function locations, because they are
 * not *node* dependent, only *thread* dependent.
 */

void shadowtorpreload_init(GModule* handle, gint nLocks) {
	/* lookup all our required symbols in this worker's module, asserting success */
	PreloadWorker* worker = _shadowtorpreload_getWorker();
	worker->handle = handle;

	/* tor */

	g_assert(g_module_symbol(handle, "tor_open_socket", (gpointer*)&(worker->tor.tor_open_socket)));
	g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "tor_open_socket", (gpointer*)&(worker->shadowtor.tor_open_socket)));

	g_assert(g_module_symbol(handle, "tor_gettimeofday", (gpointer*)&(worker->tor.tor_gettimeofday)));
	g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "tor_gettimeofday", (gpointer*)&(worker->shadowtor.tor_gettimeofday)));

	g_assert(g_module_symbol(handle, "spawn_func", (gpointer*)&(worker->tor.spawn_func)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "spawn_func", (gpointer*)&(worker->shadowtor.spawn_func)));

	g_assert(g_module_symbol(handle, "rep_hist_bandwidth_assess", (gpointer*)&(worker->tor.rep_hist_bandwidth_assess)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "rep_hist_bandwidth_assess", (gpointer*)&(worker->shadowtor.rep_hist_bandwidth_assess)));

	g_assert(g_module_symbol(handle, "router_get_advertised_bandwidth_capped", (gpointer*)&(worker->tor.router_get_advertised_bandwidth_capped)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "router_get_advertised_bandwidth_capped", (gpointer*)&(worker->shadowtor.router_get_advertised_bandwidth_capped)));

    g_assert(g_module_symbol(handle, "mark_logs_temp", (gpointer*)&(worker->tor.mark_logs_temp)));
	g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "mark_logs_temp", (gpointer*)&(worker->shadowtor.mark_logs_temp)));

    g_assert(g_module_symbol(handle, "crypto_global_init", (gpointer*)&(worker->tor.crypto_global_init)));
	g_assert(g_module_symbol(handle, "crypto_global_cleanup", (gpointer*)&(worker->tor.crypto_global_cleanup)));
    g_assert(g_module_symbol(handle, "tor_ssl_global_init", (gpointer*)&(worker->tor.tor_ssl_global_init)));
//    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "crypto_global_init", (gpointer*)&(worker->shadowtor.crypto_global_init)));
//    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "crypto_global_cleanup", (gpointer*)&(worker->shadowtor.crypto_global_cleanup)));


	/* libevent */

//	g_assert(g_module_symbol(handle, "event_base_loopexit", (gpointer*)&(worker->tor.event_base_loopexit)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "event_base_loopexit", (gpointer*)&(worker->shadowtor.event_base_loopexit)));


    /* openssl */

//    g_assert(g_module_symbol(handle, "AES_encrypt", (gpointer*)&(worker->tor.AES_encrypt)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "AES_encrypt", (gpointer*)&(worker->shadowtor.AES_encrypt)));

//    g_assert(g_module_symbol(handle, "AES_decrypt", (gpointer*)&(worker->tor.AES_decrypt)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "AES_decrypt", (gpointer*)&(worker->shadowtor.AES_decrypt)));

//    g_assert(g_module_symbol(handle, "AES_ctr128_encrypt", (gpointer*)&(worker->tor.AES_ctr128_encrypt)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "AES_ctr128_encrypt", (gpointer*)&(worker->shadowtor.AES_ctr128_encrypt)));

//    g_assert(g_module_symbol(handle, "AES_ctr128_decrypt", (gpointer*)&(worker->tor.AES_ctr128_decrypt)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "AES_ctr128_decrypt", (gpointer*)&(worker->shadowtor.AES_ctr128_decrypt)));

//    g_assert(g_module_symbol(handle, "EVP_Cipher", (gpointer*)&(worker->tor.EVP_Cipher)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "EVP_Cipher", (gpointer*)&(worker->shadowtor.EVP_Cipher)));

//    g_assert(g_module_symbol(handle, "RAND_seed", (gpointer*)&(worker->tor.RAND_seed)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_seed", (gpointer*)&(worker->shadowtor.RAND_seed)));

//    g_assert(g_module_symbol(handle, "RAND_add", (gpointer*)&(worker->tor.RAND_add)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_add", (gpointer*)&(worker->shadowtor.RAND_add)));

//    g_assert(g_module_symbol(handle, "RAND_poll", (gpointer*)&(worker->tor.RAND_poll)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_poll", (gpointer*)&(worker->shadowtor.RAND_poll)));

//    g_assert(g_module_symbol(handle, "RAND_bytes", (gpointer*)&(worker->tor.RAND_bytes)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_bytes", (gpointer*)&(worker->shadowtor.RAND_bytes)));

//    g_assert(g_module_symbol(handle, "RAND_pseudo_bytes", (gpointer*)&(worker->tor.RAND_pseudo_bytes)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_pseudo_bytes", (gpointer*)&(worker->shadowtor.RAND_pseudo_bytes)));

//    g_assert(g_module_symbol(handle, "RAND_cleanup", (gpointer*)&(worker->tor.RAND_cleanup)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_cleanup", (gpointer*)&(worker->shadowtor.RAND_cleanup)));

//    g_assert(g_module_symbol(handle, "RAND_status", (gpointer*)&(worker->tor.RAND_status)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_status", (gpointer*)&(worker->shadowtor.RAND_status)));

//    g_assert(g_module_symbol(handle, "RAND_get_rand_method", (gpointer*)&(worker->tor.RAND_get_rand_method)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_get_rand_method", (gpointer*)&(worker->shadowtor.RAND_get_rand_method)));

//    g_assert(g_module_symbol(handle, "RAND_SSLeay", (gpointer*)&(worker->tor.RAND_SSLeay)));
    g_assert(g_module_symbol(handle, SHADOWTOR_PREFIX "RAND_SSLeay", (gpointer*)&(worker->shadowtor.RAND_SSLeay)));

    /* now initialize our locking facilities, ensuring that this is only done once */
    _shadowtorpreload_cryptoSetup(nLocks);
}

void shadowtorpreload_clear() {
    /* the glib thread private worker is freed automatically */
    _shadowtorpreload_cryptoTeardown();
}

/* interposition happens below */


/* tor family */


int tor_open_socket(int domain, int type, int protocol) {
	return _shadowtorpreload_getWorker()->shadowtor.tor_open_socket(domain, type, protocol);
}

void tor_gettimeofday(struct timeval *timeval) {
	_shadowtorpreload_getWorker()->shadowtor.tor_gettimeofday(timeval);
}

int spawn_func(void (*func)(void *), void *data) {
	return _shadowtorpreload_getWorker()->shadowtor.spawn_func(func, data);
}

int rep_hist_bandwidth_assess(void) {
	return _shadowtorpreload_getWorker()->shadowtor.rep_hist_bandwidth_assess();
}

uint32_t router_get_advertised_bandwidth_capped(void *router) {
	return _shadowtorpreload_getWorker()->shadowtor.router_get_advertised_bandwidth_capped(router);
}

void mark_logs_temp(void) {
    _shadowtorpreload_getWorker()->tor.mark_logs_temp(); // call real tor mark_logs_temp function
    _shadowtorpreload_getWorker()->shadowtor.mark_logs_temp(); // reset our log callback
}


/* libevent family */


/* struct event_base* base */
int event_base_loopexit(gpointer base, const struct timeval * t) {
	return _shadowtorpreload_getWorker()->shadowtor.event_base_loopexit(base, t);
}


/* openssl family */


/* const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers */
void AES_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
    _shadowtorpreload_getWorker()->shadowtor.AES_encrypt(in, out, key);
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
    _shadowtorpreload_getWorker()->shadowtor.AES_decrypt(in, out, key);
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
    _shadowtorpreload_getWorker()->shadowtor.AES_ctr128_encrypt(in, out, key);
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
    _shadowtorpreload_getWorker()->shadowtor.AES_ctr128_decrypt(in, out, key);
}

/*
 * There is a corner case on certain machines that causes padding-related errors
 * when the EVP_Cipher is set to use aesni_cbc_hmac_sha1_cipher. Our memmove
 * implementation does not handle padding.
 *
 * We attempt to disable the use of aesni_cbc_hmac_sha1_cipher with the environment
 * variable OPENSSL_ia32cap=~0x200000200000000, and by default intercept EVP_Cipher
 * in order to skip the encryption.
 *
 * If that doesn't work, the user can request that we let the application perform
 * the encryption by defining SHADOW_ENABLE_EVPCIPHER, which means we will not
 * intercept EVP_Cipher and instead let OpenSSL do its thing.
 */
#ifndef SHADOW_ENABLE_EVPCIPHER
/*
 * EVP_CIPHER_CTX *ctx
 * The ctx parameter has been voided to avoid requiring Openssl headers
 */
int EVP_Cipher(void *ctx, unsigned char *out, const unsigned char *in, unsigned int inl){
    return _shadowtorpreload_getWorker()->shadowtor.EVP_Cipher(ctx, out, in, inl);
}
#endif

void RAND_seed(const void *buf, int num) {
    _shadowtorpreload_getWorker()->shadowtor.RAND_seed(buf, num);
}

void RAND_add(const void *buf, int num, double entropy) {
    _shadowtorpreload_getWorker()->shadowtor.RAND_add(buf, num, entropy);
}

int RAND_poll() {
    return _shadowtorpreload_getWorker()->shadowtor.RAND_poll();
}

int RAND_bytes(unsigned char *buf, int num) {
    return _shadowtorpreload_getWorker()->shadowtor.RAND_bytes(buf, num);
}

int RAND_pseudo_bytes(unsigned char *buf, int num) {
    return _shadowtorpreload_getWorker()->shadowtor.RAND_pseudo_bytes(buf, num);
}

void RAND_cleanup() {
    _shadowtorpreload_getWorker()->shadowtor.RAND_cleanup();
}

int RAND_status() {
    return _shadowtorpreload_getWorker()->shadowtor.RAND_status();
}

const void* RAND_get_rand_method() {
    return _shadowtorpreload_getWorker()->shadowtor.RAND_get_rand_method();
}

const void* RAND_SSLeay() {
    return _shadowtorpreload_getWorker()->shadowtor.RAND_SSLeay();
}


/********************************************************************************
 * the code below provides support for multi-threaded openssl.
 * we need global state here to manage locking for all threads, so we use the
 * preload library because the symbols here do not get hoisted and copied
 * for each instance of the plug-in.
 *
 * @see '$man CRYPTO_lock'
 ********************************************************************************/

/*
 * Holds global state for the Tor preload library. This state will not be
 * copied for every instance of Tor, or for every instance of the plugin.
 * Therefore, we must ensure that modifications to it are properly locked
 * for thread safety, it is not initialized multiple times, etc.
 */
typedef struct _PreloadGlobal PreloadGlobal;
struct _PreloadGlobal {
    gboolean initialized;
    gint nTorCryptoNodes;
    gint nThreads;
    gint numCryptoThreadLocks;
    GRWLock* cryptoThreadLocks;
};

G_LOCK_DEFINE_STATIC(shadowtorpreloadGlobalLock);
PreloadGlobal shadowtorpreloadGlobalState = {FALSE, 0, 0, 0, NULL};

/**
 * these init and cleanup Tor functions are called to handle openssl.
 * they must be globally locked and only called once globally to avoid openssl errors.
 */
int crypto_global_init(int useAccel, const char *accelName, const char *accelDir) {
    G_LOCK(shadowtorpreloadGlobalLock);

    gint result = 0;
    if(++shadowtorpreloadGlobalState.nTorCryptoNodes == 1) {
        _shadowtorpreload_getWorker()->tor.tor_ssl_global_init();
        result = _shadowtorpreload_getWorker()->tor.crypto_global_init(useAccel, accelName, accelDir);
    }

    G_UNLOCK(shadowtorpreloadGlobalLock);
    return result;
}
int crypto_global_cleanup(void) {
    G_LOCK(shadowtorpreloadGlobalLock);

    gint result = 0;
    if(--shadowtorpreloadGlobalState.nTorCryptoNodes == 0) {
        result = _shadowtorpreload_getWorker()->tor.crypto_global_cleanup();
    }

    G_UNLOCK(shadowtorpreloadGlobalLock);
    return result;
}

void tor_ssl_global_init() {
    // do nothing, we initialized openssl above in crypto_global_init
}

static unsigned long _shadowtorpreload_getIDFunc() {
    /* return an ID that is unique for each thread */
    return (unsigned long)(_shadowtorpreload_getWorker());
}

unsigned long (*CRYPTO_get_id_callback(void))(void) {
    return (unsigned long)_shadowtorpreload_getIDFunc;
}

static void _shadowtorpreload_cryptoLockingFunc(int mode, int n, const char *file, int line) {
    assert(shadowtorpreloadGlobalState.initialized);

    GRWLock* lock = &(shadowtorpreloadGlobalState.cryptoThreadLocks[n]);
    assert(lock);

    if(mode & CRYPTO_LOCK) {
        if(mode & CRYPTO_READ) {
            g_rw_lock_reader_lock(lock);
        } else if(mode & CRYPTO_WRITE) {
            g_rw_lock_writer_lock(lock);
        }
    } else if(mode & CRYPTO_UNLOCK) {
        if(mode & CRYPTO_READ) {
            g_rw_lock_reader_unlock(lock);
        } else if(mode & CRYPTO_WRITE) {
            g_rw_lock_writer_unlock(lock);
        }
    }
}

void (*CRYPTO_get_locking_callback(void))(int mode,int type,const char *file,
        int line) {
    return _shadowtorpreload_cryptoLockingFunc;
}

static void _shadowtorpreload_cryptoSetup(int numLocks) {
    G_LOCK(shadowtorpreloadGlobalLock);

    if(!shadowtorpreloadGlobalState.initialized) {
        shadowtorpreloadGlobalState.numCryptoThreadLocks = numLocks;

        shadowtorpreloadGlobalState.cryptoThreadLocks = g_new0(GRWLock, numLocks);
        for(gint i = 0; i < numLocks; i++) {
            g_rw_lock_init(&(shadowtorpreloadGlobalState.cryptoThreadLocks[i]));
        }

        shadowtorpreloadGlobalState.initialized = TRUE;
    }

    shadowtorpreloadGlobalState.nThreads++;

    G_UNLOCK(shadowtorpreloadGlobalLock);
}

static void _shadowtorpreload_cryptoTeardown() {
    G_LOCK(shadowtorpreloadGlobalLock);

    if(shadowtorpreloadGlobalState.initialized &&
            shadowtorpreloadGlobalState.cryptoThreadLocks &&
            --shadowtorpreloadGlobalState.nThreads == 0) {

        for(int i = 0; i < shadowtorpreloadGlobalState.numCryptoThreadLocks; i++) {
            g_rw_lock_clear(&(shadowtorpreloadGlobalState.cryptoThreadLocks[i]));
        }

        g_free(shadowtorpreloadGlobalState.cryptoThreadLocks);
        shadowtorpreloadGlobalState.cryptoThreadLocks = NULL;
        shadowtorpreloadGlobalState.initialized = 0;
    }

    G_UNLOCK(shadowtorpreloadGlobalLock);
}

/********************************************************************************
 * end code that supports multi-threaded openssl.
 ********************************************************************************/
