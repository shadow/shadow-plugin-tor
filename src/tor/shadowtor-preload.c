/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <sys/time.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include <event2/dns.h>
#include <openssl/crypto.h>

#include <glib.h>
#include <gmodule.h>

#include "shadowtor.h"

/* tor functions */

typedef int (*tor_open_socket_fp)(int, int, int);
typedef int (*tor_gettimeofday_fp)(struct timeval *);
typedef int (*spawn_func_fp)();
typedef int (*crypto_global_init_fp)(int, const char*, const char*);
typedef int (*crypto_global_cleanup_fp)(void);
typedef int (*crypto_early_init_fp)(void);
typedef int (*crypto_seed_rng_fp)(int);
typedef int (*crypto_init_siphash_key_fp)(void);
typedef void (*tor_ssl_global_init_fp)(void);
typedef void (*mark_logs_temp_fp)(void);
typedef int (*write_str_to_file_fp)(const char *, const char *, int);
typedef void (*cpuworker_main_fp)(void*);
typedef void (*sockmgr_thread_main_fp)(void*);

/* shadowtor funcs */

typedef void (*shadowtor_newSockMgrWorker_fp)();
typedef void (*shadowtor_newCPUWorker_fp)(int);
typedef int (*shadowtor_openSocket_fp)(int, int, int);
typedef void (*shadowtor_setLogging_fp)();
typedef void (*shadowtor_loopexit_fp)();
typedef void (*shadowtor_trackConsensusUpdate_fp)(const char*, const char*);

typedef struct _TorInterposeFuncs TorInterposeFuncs;
struct _TorInterposeFuncs {
	tor_open_socket_fp tor_open_socket;
	tor_gettimeofday_fp tor_gettimeofday;
	spawn_func_fp spawn_func;
	crypto_global_init_fp crypto_global_init;
	crypto_global_cleanup_fp crypto_global_cleanup;
    crypto_early_init_fp crypto_early_init;
    crypto_seed_rng_fp crypto_seed_rng;
    crypto_init_siphash_key_fp crypto_init_siphash_key;

	tor_ssl_global_init_fp tor_ssl_global_init;
	mark_logs_temp_fp mark_logs_temp;
	write_str_to_file_fp write_str_to_file;
	cpuworker_main_fp cpuworker_main;
	sockmgr_thread_main_fp sockmgr_thread_main;
};

typedef struct _ShadowTorInterposeFuncs ShadowTorInterposeFuncs;
struct _ShadowTorInterposeFuncs {
    shadowtor_newSockMgrWorker_fp shadowtor_newSockMgrWorker;
    shadowtor_newCPUWorker_fp shadowtor_newCPUWorker;
    shadowtor_openSocket_fp shadowtor_openSocket;
    shadowtor_setLogging_fp shadowtor_setLogging;
    shadowtor_loopexit_fp shadowtor_loopexit;
    shadowtor_trackConsensusUpdate_fp shadowtor_trackConsensusUpdate;
};

typedef struct _PreloadWorker PreloadWorker;
struct _PreloadWorker {
	GModule* handle;
	TorInterposeFuncs tor;
	ShadowTorInterposeFuncs shadowtor;
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

	/* tor function lookups */
	g_assert(g_module_symbol(handle, "tor_open_socket", (gpointer*)&(worker->tor.tor_open_socket)));
	g_assert(g_module_symbol(handle, "tor_gettimeofday", (gpointer*)&(worker->tor.tor_gettimeofday)));
	g_assert(g_module_symbol(handle, "spawn_func", (gpointer*)&(worker->tor.spawn_func)));
    g_assert(g_module_symbol(handle, "mark_logs_temp", (gpointer*)&(worker->tor.mark_logs_temp)));
    g_assert(g_module_symbol(handle, "crypto_global_init", (gpointer*)&(worker->tor.crypto_global_init)));
    g_assert(g_module_symbol(handle, "crypto_global_cleanup", (gpointer*)&(worker->tor.crypto_global_cleanup)));
    g_assert(g_module_symbol(handle, "tor_ssl_global_init", (gpointer*)&(worker->tor.tor_ssl_global_init)));
    g_assert(g_module_symbol(handle, "cpuworker_main", (gpointer*)&(worker->tor.cpuworker_main)));
    g_assert(g_module_symbol(handle, "write_str_to_file", (gpointer*)&(worker->tor.write_str_to_file)));

    /* these do not exist in all Tors, so don't assert success */
    g_module_symbol(handle, "crypto_early_init", (gpointer*)&(worker->tor.crypto_early_init));
    g_module_symbol(handle, "crypto_seed_rng", (gpointer*)&(worker->tor.crypto_seed_rng));
    g_module_symbol(handle, "crypto_init_siphash_key", (gpointer*)&(worker->tor.crypto_init_siphash_key));
    g_module_symbol(handle, "sockmgr_thread_main", (gpointer*)&(worker->tor.sockmgr_thread_main));

    /* shadowtor function lookups */
    g_assert(g_module_symbol(handle, "shadowtor_newSockMgrWorker", (gpointer*)&(worker->shadowtor.shadowtor_newSockMgrWorker)));
    g_assert(g_module_symbol(handle, "shadowtor_newCPUWorker", (gpointer*)&(worker->shadowtor.shadowtor_newCPUWorker)));
    g_assert(g_module_symbol(handle, "shadowtor_openSocket", (gpointer*)&(worker->shadowtor.shadowtor_openSocket)));
    g_assert(g_module_symbol(handle, "shadowtor_setLogging", (gpointer*)&(worker->shadowtor.shadowtor_setLogging)));
    g_assert(g_module_symbol(handle, "shadowtor_loopexit", (gpointer*)&(worker->shadowtor.shadowtor_loopexit)));
    g_assert(g_module_symbol(handle, "shadowtor_trackConsensusUpdate", (gpointer*)&(worker->shadowtor.shadowtor_trackConsensusUpdate)));

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
    return _shadowtorpreload_getWorker()->shadowtor.shadowtor_openSocket(domain, type, protocol);
}

void tor_gettimeofday(struct timeval *timeval) {
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    timeval->tv_sec = tp.tv_sec;
    timeval->tv_usec = tp.tv_nsec/1000;
}

int spawn_func(void (*func)(void *), void *data) {
    if(func == _shadowtorpreload_getWorker()->tor.cpuworker_main) {
        /* this takes the place of forking a cpuworker and running cpuworker_main.
         * func points to cpuworker_main, but we'll implement a version that
         * works in shadow */
        int *fdarray = data;
        int fd = fdarray[1]; /* this side is ours */

        _shadowtorpreload_getWorker()->shadowtor.shadowtor_newCPUWorker(fd);

        /* now we should be ready to receive events in vtor_cpuworker_readable */
        return 0;
    } else if(_shadowtorpreload_getWorker()->tor.sockmgr_thread_main != NULL &&
            func == _shadowtorpreload_getWorker()->tor.sockmgr_thread_main) {
        _shadowtorpreload_getWorker()->shadowtor.shadowtor_newSockMgrWorker();
        return 0;
    }
    return -1;
}

void mark_logs_temp(void) {
    _shadowtorpreload_getWorker()->tor.mark_logs_temp(); // call real tor mark_logs_temp function
    _shadowtorpreload_getWorker()->shadowtor.shadowtor_setLogging();
}

int write_str_to_file(const char *fname, const char *str, int bin) {
    _shadowtorpreload_getWorker()->shadowtor.shadowtor_trackConsensusUpdate(fname, str);
    return _shadowtorpreload_getWorker()->tor.write_str_to_file(fname, str, bin);
}

/* libevent family */


/* struct event_base* base */
int event_base_loopexit(struct event_base * base, const struct timeval * t) {
    _shadowtorpreload_getWorker()->shadowtor.shadowtor_loopexit();
    return 0;
}

struct evdns_request* evdns_base_resolve_ipv4(struct evdns_base *base, const char *name, int flags,
    evdns_callback_type callback, void *ptr) {

    in_addr_t ip;
    int success = 0;
    if(callback) {
        struct addrinfo* info = NULL;

        int success = (0 == getaddrinfo(name, NULL, NULL, &info));
        if (success) {
            ip = ((struct sockaddr_in*) (info->ai_addr))->sin_addr.s_addr;
        }
        freeaddrinfo(info);
    }

    if(success) {
        callback(DNS_ERR_NONE, DNS_IPv4_A, 1, 86400, &ip, ptr);
        return (struct evdns_request *)1;
    } else {
        return NULL;
    }
}


/* openssl family */


/* const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers */
void AES_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void AES_ctr128_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
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
int EVP_Cipher(struct evp_cipher_ctx_st* ctx, unsigned char *out, const unsigned char *in, unsigned int inl){
    memmove(out, in, (size_t)inl);
    return 1;
}
#endif

void RAND_seed(const void *buf, int num) {
    return;
}

void RAND_add(const void *buf, int num, double entropy) {
    return;
}

int RAND_poll() {
    return 1;
}

static gint _shadowtorpreload_getRandomBytes(guchar* buf, gint numBytes) {
    gint bytesWritten = 0;

    while(numBytes > bytesWritten) {
        gint r = rand();
        gint copyLength = MIN(numBytes-bytesWritten, sizeof(gint));
        g_memmove(buf+bytesWritten, &r, copyLength);
        bytesWritten += copyLength;
    }

    return 1;
}

int RAND_bytes(unsigned char *buf, int num) {
    return _shadowtorpreload_getRandomBytes(buf, num);
}

int RAND_pseudo_bytes(unsigned char *buf, int num) {
    return _shadowtorpreload_getRandomBytes(buf, num);
}

void RAND_cleanup() {
    return;
}

int RAND_status() {
    return 1;
}

static const struct {
    void* seed;
    void* bytes;
    void* cleanup;
    void* add;
    void* pseudorand;
    void* status;
} shadowtorpreload_customRandMethod = {
    RAND_seed,
    RAND_bytes,
    RAND_cleanup,
    RAND_add,
    RAND_pseudo_bytes,
    RAND_status
};

const void* RAND_get_rand_method() {
    return (const void *)(&shadowtorpreload_customRandMethod);
}

const void* RAND_SSLeay() {
    return (const void *)(&shadowtorpreload_customRandMethod);
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
    gboolean sslInitializedEarly;
    gboolean sslInitializedGlobal;
    gint nTorCryptoNodes;
    gint nThreads;
    gint numCryptoThreadLocks;
    GRWLock* cryptoThreadLocks;
};

G_LOCK_DEFINE_STATIC(shadowtorpreloadPrimaryLock);
G_LOCK_DEFINE_STATIC(shadowtorpreloadSecondaryLock);
PreloadGlobal shadowtorpreloadGlobalState = {FALSE, 0, 0, 0, NULL};

/**
 * these init and cleanup Tor functions are called to handle openssl.
 * they must be globally locked and only called once globally to avoid openssl errors.
 */

int crypto_early_init() {
    G_LOCK(shadowtorpreloadSecondaryLock);
    gint result = 0;

    if(!shadowtorpreloadGlobalState.sslInitializedEarly) {
        shadowtorpreloadGlobalState.sslInitializedEarly = TRUE;
        if(_shadowtorpreload_getWorker()->tor.crypto_early_init != NULL) {
            result = _shadowtorpreload_getWorker()->tor.crypto_early_init();
        }
    } else {
        if(_shadowtorpreload_getWorker()->tor.crypto_early_init != NULL) {
            if (_shadowtorpreload_getWorker()->tor.crypto_seed_rng(1) < 0)
              result = -1;
            if (_shadowtorpreload_getWorker()->tor.crypto_init_siphash_key() < 0)
              result = -1;
        }
    }

	G_UNLOCK(shadowtorpreloadSecondaryLock);
    return result;
}

int crypto_global_init(int useAccel, const char *accelName, const char *accelDir) {
    G_LOCK(shadowtorpreloadPrimaryLock);

    shadowtorpreloadGlobalState.nTorCryptoNodes++;

    gint result = 0;
    if(!shadowtorpreloadGlobalState.sslInitializedGlobal) {
        shadowtorpreloadGlobalState.sslInitializedGlobal = TRUE;
        if(_shadowtorpreload_getWorker()->tor.tor_ssl_global_init) {
            _shadowtorpreload_getWorker()->tor.tor_ssl_global_init();
        }
        if(_shadowtorpreload_getWorker()->tor.crypto_global_init) {
            result = _shadowtorpreload_getWorker()->tor.crypto_global_init(useAccel, accelName, accelDir);
        }
    }

    G_UNLOCK(shadowtorpreloadPrimaryLock);
    return result;
}

int crypto_global_cleanup(void) {
    G_LOCK(shadowtorpreloadPrimaryLock);

    gint result = 0;
    if(--shadowtorpreloadGlobalState.nTorCryptoNodes == 0) {
        if(_shadowtorpreload_getWorker()->tor.crypto_global_cleanup) {
            result = _shadowtorpreload_getWorker()->tor.crypto_global_cleanup();
        }
    }

    G_UNLOCK(shadowtorpreloadPrimaryLock);
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
    G_LOCK(shadowtorpreloadPrimaryLock);

    if(!shadowtorpreloadGlobalState.initialized) {
        shadowtorpreloadGlobalState.numCryptoThreadLocks = numLocks;

        shadowtorpreloadGlobalState.cryptoThreadLocks = g_new0(GRWLock, numLocks);
        for(gint i = 0; i < numLocks; i++) {
            g_rw_lock_init(&(shadowtorpreloadGlobalState.cryptoThreadLocks[i]));
        }

        shadowtorpreloadGlobalState.initialized = TRUE;
    }

    shadowtorpreloadGlobalState.nThreads++;

    G_UNLOCK(shadowtorpreloadPrimaryLock);
}

static void _shadowtorpreload_cryptoTeardown() {
    G_LOCK(shadowtorpreloadPrimaryLock);

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

    G_UNLOCK(shadowtorpreloadPrimaryLock);
}

/********************************************************************************
 * end code that supports multi-threaded openssl.
 ********************************************************************************/
