/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

#include <sys/time.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include <event2/dns.h>
#include <event2/thread.h>
#include <openssl/crypto.h>

#include <glib.h>
#include <gmodule.h>

#include "torlog.h"

/* tor functions */
typedef int (*tor_init_fp)(int argc, char *argv[]);
typedef int (*write_str_to_file_fp)(const char *, const char *, int);
typedef int (*crypto_global_init_fp)(int, const char*, const char*);
typedef int (*crypto_global_cleanup_fp)(void);
typedef int (*crypto_early_init_fp)(void);
typedef int (*crypto_seed_rng_fp)(int);
typedef int (*crypto_init_siphash_key_fp)(void);
typedef void (*tor_ssl_global_init_fp)(void);

/* libevent threading */
typedef void (*evthread_set_id_callback_fp)(unsigned long (*id_fn)(void));
typedef int (*evthread_set_lock_callbacks_fp)(const struct evthread_lock_callbacks *cbs);
typedef int (*evthread_set_condition_callbacks_fp)(const struct evthread_condition_callbacks *cbs);

/* openssl threading */
typedef void (*CRYPTO_set_id_callback_fp)(unsigned long (*func)(void));
typedef void (*CRYPTO_set_locking_callback_fp)(void (*func)(int mode,int type, const char *file,int line));
typedef void (*CRYPTO_set_dynlock_create_callback_fp)(struct CRYPTO_dynlock_value *(*dyn_create_function)(const char *file, int line));
typedef void (*CRYPTO_set_dynlock_lock_callback_fp)(void (*dyn_lock_function)(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line));
typedef void (*CRYPTO_set_dynlock_destroy_callback_fp)(void (*dyn_destroy_function)(struct CRYPTO_dynlock_value *l, const char *file, int line));

typedef struct _InterposeFuncs InterposeFuncs;
struct _InterposeFuncs {
    tor_init_fp tor_init;
    write_str_to_file_fp write_str_to_file;
    crypto_global_init_fp crypto_global_init;
    crypto_global_cleanup_fp crypto_global_cleanup;
    crypto_early_init_fp crypto_early_init;
    crypto_seed_rng_fp crypto_seed_rng;
    crypto_init_siphash_key_fp crypto_init_siphash_key;
    tor_ssl_global_init_fp tor_ssl_global_init;

    evthread_set_id_callback_fp evthread_set_id_callback;
    evthread_set_lock_callbacks_fp evthread_set_lock_callbacks;
    evthread_set_condition_callbacks_fp evthread_set_condition_callbacks;

    CRYPTO_set_id_callback_fp CRYPTO_set_id_callback;
    CRYPTO_set_locking_callback_fp CRYPTO_set_locking_callback;
    CRYPTO_set_dynlock_create_callback_fp CRYPTO_set_dynlock_create_callback;
    CRYPTO_set_dynlock_lock_callback_fp CRYPTO_set_dynlock_lock_callback;
    CRYPTO_set_dynlock_destroy_callback_fp CRYPTO_set_dynlock_destroy_callback;
};

typedef struct _PluginData PluginData;
struct _PluginData {
    GModule* handle;
    InterposeFuncs vtable;
};

typedef struct _PreloadWorker PreloadWorker;
struct _PreloadWorker {
    GHashTable* plugins;
    PluginData* active;
    int consensusCounter;
};

/*
 * the key used to store each threads version of their searched function library.
 * the use this key to retrieve this library when intercepting functions from tor.
 * The PrelaodWorker we store here will be freed using g_free automatically when
 * the thread exits.
 */
static GPrivate threadPreloadWorkerKey = G_PRIVATE_INIT(g_free);

/* preload_init must be called before this so the worker gets created */
static PreloadWorker* _shadowtorpreload_getWorker(void) {
    /* get current thread's private worker object */
    PreloadWorker* worker = g_private_get(&threadPreloadWorkerKey);
    if (!worker) {
        worker = g_new0(PreloadWorker, 1);
        worker->plugins = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
        g_private_set(&threadPreloadWorkerKey, worker);
    }
    g_assert(worker);
    return worker;
}

/* used to stop shadow from intercepting our thread locking mechanisms */
extern void interposer_enable();
extern void interposer_disable();

/* forward declarations */
static void _shadowtorpreload_cryptoSetup(int);
static void _shadowtorpreload_cryptoTeardown(void);
/*
 * here we search and save pointers to the functions we need to call when
 * we intercept tor's functions. this is initialized for each thread, and each
 * thread has pointers to their own functions (each has its own version of the
 * plug-in state). We dont register these function locations, because they are
 * not *node* dependent, only *thread* dependent.
 */

static PluginData* shadowtorpreload_newPlugin(GModule* handle) {
    PluginData* pdata = g_new0(PluginData, 1);

    pdata->handle = handle;

    /* tor function lookups */
    g_assert(g_module_symbol(handle, "tor_init", (gpointer*)&(pdata->vtable.tor_init)));
    g_assert(g_module_symbol(handle, "write_str_to_file", (gpointer*)&(pdata->vtable.write_str_to_file)));
    g_assert(g_module_symbol(handle, "crypto_global_init", (gpointer*)&(pdata->vtable.crypto_global_init)));
    g_assert(g_module_symbol(handle, "crypto_global_cleanup", (gpointer*)&(pdata->vtable.crypto_global_cleanup)));
    g_assert(g_module_symbol(handle, "tor_ssl_global_init", (gpointer*)&(pdata->vtable.tor_ssl_global_init)));

    /* libevent thread setup */
    g_assert((pdata->vtable.evthread_set_id_callback = dlsym(RTLD_NEXT, "evthread_set_id_callback")) != NULL);
    g_assert((pdata->vtable.evthread_set_lock_callbacks = dlsym(RTLD_NEXT, "evthread_set_lock_callbacks")) != NULL);
    g_assert((pdata->vtable.evthread_set_condition_callbacks = dlsym(RTLD_NEXT, "evthread_set_condition_callbacks")) != NULL);

    /* openssl thread setup */
    g_assert((pdata->vtable.CRYPTO_set_id_callback = dlsym(RTLD_NEXT, "CRYPTO_set_id_callback")) != NULL);
    g_assert((pdata->vtable.CRYPTO_set_locking_callback = dlsym(RTLD_NEXT, "CRYPTO_set_locking_callback")) != NULL);
    g_assert((pdata->vtable.CRYPTO_set_dynlock_create_callback = dlsym(RTLD_NEXT, "CRYPTO_set_dynlock_create_callback")) != NULL);
    g_assert((pdata->vtable.CRYPTO_set_dynlock_lock_callback = dlsym(RTLD_NEXT, "CRYPTO_set_dynlock_lock_callback")) != NULL);
    g_assert((pdata->vtable.CRYPTO_set_dynlock_destroy_callback = dlsym(RTLD_NEXT, "CRYPTO_set_dynlock_destroy_callback")) != NULL);

    /* these do not exist in all Tors, so don't assert success */
    g_module_symbol(handle, "crypto_early_init", (gpointer*)&(pdata->vtable.crypto_early_init));
    g_module_symbol(handle, "crypto_seed_rng", (gpointer*)&(pdata->vtable.crypto_seed_rng));
    g_module_symbol(handle, "crypto_init_siphash_key", (gpointer*)&(pdata->vtable.crypto_init_siphash_key));

    return pdata;
}

void shadowtorpreload_init(GModule* handle, int nLocks) {
    /* lookup all our required symbols in this worker's module, asserting success */
    PreloadWorker* worker = _shadowtorpreload_getWorker();

    PluginData* pdata = shadowtorpreload_newPlugin(handle);
    g_hash_table_insert(worker->plugins, handle, pdata);

    /* handle multi-threading support
     * initialize our locking facilities, ensuring that this is only done once */
    _shadowtorpreload_cryptoSetup(nLocks);
}

void shadowtorpreload_setActive(GModule* handle) {
    PreloadWorker* worker = _shadowtorpreload_getWorker();
    if(handle) {
        worker->active = g_hash_table_lookup(worker->plugins, handle);
    } else {
        worker->active = NULL;
    }
}

void shadowtorpreload_clear(GModule* handle) {
    /* the glib thread private worker is freed automatically */
    PreloadWorker* worker = _shadowtorpreload_getWorker();
    _shadowtorpreload_cryptoTeardown();
    g_hash_table_remove(worker->plugins, handle);
}

/********************************************************************************
 * start interposition functions
 ********************************************************************************/

/* tor family */

/* this is a hack to ensure only one tor node is initializing at a time,
 * until we can find the race condition that occurs when running shadow
 * with multiple worker threads. */
G_LOCK_DEFINE_STATIC(shadowtorpreloadTorInitLock);
int tor_init(int argc, char *argv[]) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTorInitLock);
    interposer_enable();
    int ret = _shadowtorpreload_getWorker()->active->vtable.tor_init(argc, argv);
    interposer_disable();
    G_UNLOCK(shadowtorpreloadTorInitLock);
    interposer_enable();
    return ret;
}

int write_str_to_file(const char *fname, const char *str, int bin) {
    /* check if filepath is a consenus file. store it in separate files
     * so we don't lose old consenus info on overwrites. */
    if (g_str_has_suffix(fname, "cached-consensus")) {
        //if(g_strrstr(filepath, "cached-consensus") != NULL) {
        GString* newPath = g_string_new(fname);
        GError* error = NULL;
        g_string_append_printf(newPath, ".%03i", _shadowtorpreload_getWorker()->consensusCounter++);
        if (!g_file_set_contents(newPath->str, str, -1, &error)) {
            log_warn(LD_GENERAL, "Error writing file '%s' to track consensus update: error %i: %s",
                    newPath->str, error->code, error->message);
        }
        g_string_free(newPath, TRUE);
    }

    return _shadowtorpreload_getWorker()->active->vtable.write_str_to_file(fname, str, bin);
}

/* libevent family */

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

void RAND_cleanup(void) {
    return;
}

int RAND_status(void) {
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

const RAND_METHOD* RAND_get_rand_method() {
    return (const RAND_METHOD*)(&shadowtorpreload_customRandMethod);
}

RAND_METHOD* RAND_SSLeay() {
    return (RAND_METHOD*)(&shadowtorpreload_customRandMethod);
}


/********************************************************************************
 * the code below provides support for multi-threaded openssl and libevent.
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

/* !!WARNING - do not change this struct without updating the initialization
 * of shadowtorpreloadGlobalState that is done right below it!! */
struct _PreloadGlobal {
    gboolean initialized;
    gboolean sslInitializedEarly;
    gboolean sslInitializedGlobal;
    gint nTorCryptoNodes;
    gint nThreads;
    gint numCryptoThreadLocks;
    GRWLock* cryptoThreadLocks;

    unsigned long (*libevent_id_fn)(void);
    struct evthread_lock_callbacks libevent_lock_fns;
    struct evthread_condition_callbacks libevent_cond_fns;

    unsigned long (*crypto_id_fn)(void);
    void (*crypto_lock_fn)(int mode,int type, const char *file,int line);
    struct CRYPTO_dynlock_value *(*crypto_dyn_create_function)(const char *file, int line);
    void (*crypto_dyn_lock_function)(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line);
    void (*crypto_dyn_destroy_function)(struct CRYPTO_dynlock_value *l, const char *file, int line);
};

PreloadGlobal shadowtorpreloadGlobalState = {FALSE, FALSE, FALSE, 0, 0, 0, NULL,
        NULL, 0, 0, NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL,
        NULL, NULL, NULL, NULL, NULL};
G_LOCK_DEFINE_STATIC(shadowtorpreloadPrimaryLock);
G_LOCK_DEFINE_STATIC(shadowtorpreloadSecondaryLock);
G_LOCK_DEFINE_STATIC(shadowtorpreloadTrenaryLock);

/*
 * here we wrap the libevent thread funcs to make sure that the libevent
 * global library state is locked with the real pthread and shadow doesnt interfere
 */

static unsigned long _shadowtorpreload_libevent_id() {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_id_fn);
    unsigned long ret = shadowtorpreloadGlobalState.libevent_id_fn();
    interposer_enable();
    return ret;
}

static void* _shadowtorpreload_libevent_lock_alloc(unsigned locktype) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_lock_fns.alloc);
    void* ret = shadowtorpreloadGlobalState.libevent_lock_fns.alloc(locktype);
    interposer_enable();
    return ret;
}

static void _shadowtorpreload_libevent_lock_free(void *lock, unsigned locktype) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_lock_fns.free);
    shadowtorpreloadGlobalState.libevent_lock_fns.free(lock, locktype);
    interposer_enable();
}

static int _shadowtorpreload_libevent_lock_lock(unsigned mode, void *lock) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_lock_fns.lock);
    int ret = shadowtorpreloadGlobalState.libevent_lock_fns.lock(mode, lock);
    interposer_enable();
    return ret;
}

static int _shadowtorpreload_libevent_lock_unlock(unsigned mode, void *lock) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_lock_fns.unlock);
    int ret = shadowtorpreloadGlobalState.libevent_lock_fns.unlock(mode, lock);
    interposer_enable();
    return ret;
}

static void* _shadowtorpreload_libevent_cond_alloc(unsigned condtype) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_cond_fns.alloc_condition);
    void* ret = shadowtorpreloadGlobalState.libevent_cond_fns.alloc_condition(condtype);
    interposer_enable();
    return ret;
}

static void _shadowtorpreload_libevent_cond_free(void *cond) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_cond_fns.free_condition);
    shadowtorpreloadGlobalState.libevent_cond_fns.free_condition(cond);
    interposer_enable();
}

static int _shadowtorpreload_libevent_cond_signal(void *cond, int broadcast) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_cond_fns.signal_condition);
    int ret = shadowtorpreloadGlobalState.libevent_cond_fns.signal_condition(cond, broadcast);
    interposer_enable();
    return ret;
}

static int _shadowtorpreload_libevent_cond_wait(void *cond, void *lock, const struct timeval *timeout) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.libevent_cond_fns.wait_condition);
    int ret = shadowtorpreloadGlobalState.libevent_cond_fns.wait_condition(cond, lock, timeout);
    interposer_enable();
    return ret;
}

/*
 * the next libevent functions store the necessary state and set up the wrappers.
 * the libevent funcs are looked up late here using dlsym, because we dont have
 * the handle that we had for the tor module lookups, and we are assured that
 * by the time these funcs are called by libevent, libevent.so has been loaded and
 * therefore we are guaranteed to find the evthread symbols.
 */

void evthread_set_id_callback(unsigned long (*id_fn)(void)) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);

    if(!shadowtorpreloadGlobalState.libevent_id_fn && id_fn) {
        shadowtorpreloadGlobalState.libevent_id_fn = id_fn;

        g_assert(_shadowtorpreload_getWorker()->active->vtable.evthread_set_id_callback != NULL);
        _shadowtorpreload_getWorker()->active->vtable.evthread_set_id_callback(_shadowtorpreload_libevent_id);
    }

    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
}

int evthread_set_lock_callbacks(const struct evthread_lock_callbacks *cbs) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);

    int ret = 0;
    if(!shadowtorpreloadGlobalState.libevent_lock_fns.alloc && cbs) {
        shadowtorpreloadGlobalState.libevent_lock_fns.alloc = cbs->alloc;
        shadowtorpreloadGlobalState.libevent_lock_fns.free = cbs->free;
        shadowtorpreloadGlobalState.libevent_lock_fns.lock = cbs->lock;
        shadowtorpreloadGlobalState.libevent_lock_fns.unlock = cbs->unlock;

        struct evthread_lock_callbacks libevent_lock_wrappers;
        memset(&libevent_lock_wrappers, 0, sizeof(struct evthread_lock_callbacks));
        libevent_lock_wrappers.lock_api_version = cbs->lock_api_version;
        libevent_lock_wrappers.supported_locktypes = cbs->supported_locktypes;
        libevent_lock_wrappers.alloc = _shadowtorpreload_libevent_lock_alloc;
        libevent_lock_wrappers.free = _shadowtorpreload_libevent_lock_free;
        libevent_lock_wrappers.lock = _shadowtorpreload_libevent_lock_lock;
        libevent_lock_wrappers.unlock = _shadowtorpreload_libevent_lock_unlock;

        g_assert(_shadowtorpreload_getWorker()->active->vtable.evthread_set_lock_callbacks != NULL);
        ret = _shadowtorpreload_getWorker()->active->vtable.evthread_set_lock_callbacks(&libevent_lock_wrappers);
    }

    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
    return ret;
}

int evthread_set_condition_callbacks(const struct evthread_condition_callbacks *cbs) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);

    int ret = 0;
    if(!shadowtorpreloadGlobalState.libevent_cond_fns.alloc_condition && cbs) {
        shadowtorpreloadGlobalState.libevent_cond_fns.alloc_condition = cbs->alloc_condition;
        shadowtorpreloadGlobalState.libevent_cond_fns.free_condition = cbs->free_condition;
        shadowtorpreloadGlobalState.libevent_cond_fns.signal_condition = cbs->signal_condition;
        shadowtorpreloadGlobalState.libevent_cond_fns.wait_condition = cbs->wait_condition;

        struct evthread_condition_callbacks libevent_cond_wrappers;
        memset(&libevent_cond_wrappers, 0, sizeof(struct evthread_condition_callbacks));
        libevent_cond_wrappers.condition_api_version = cbs->condition_api_version;
        libevent_cond_wrappers.alloc_condition = _shadowtorpreload_libevent_cond_alloc;
        libevent_cond_wrappers.free_condition = _shadowtorpreload_libevent_cond_free;
        libevent_cond_wrappers.signal_condition = _shadowtorpreload_libevent_cond_signal;
        libevent_cond_wrappers.wait_condition = _shadowtorpreload_libevent_cond_wait;

        g_assert(_shadowtorpreload_getWorker()->active->vtable.evthread_set_condition_callbacks != NULL);
        ret = _shadowtorpreload_getWorker()->active->vtable.evthread_set_condition_callbacks(&libevent_cond_wrappers);
    }

    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
    return ret;
}

/**
 * these init and cleanup Tor functions are called to handle openssl.
 * they must be globally locked and only called once globally to avoid openssl errors.
 */

int crypto_early_init(void) {
    interposer_disable();
    G_LOCK(shadowtorpreloadSecondaryLock);
    interposer_enable();

    gint result = 0;

    if(!shadowtorpreloadGlobalState.sslInitializedEarly) {
        shadowtorpreloadGlobalState.sslInitializedEarly = TRUE;
        if(_shadowtorpreload_getWorker()->active->vtable.crypto_early_init != NULL) {
            result = _shadowtorpreload_getWorker()->active->vtable.crypto_early_init();
        }
    } else {
        if(_shadowtorpreload_getWorker()->active->vtable.crypto_early_init != NULL) {
            if (_shadowtorpreload_getWorker()->active->vtable.crypto_seed_rng(1) < 0)
              result = -1;
            if (_shadowtorpreload_getWorker()->active->vtable.crypto_init_siphash_key() < 0)
              result = -1;
        }
    }

    interposer_disable();
    G_UNLOCK(shadowtorpreloadSecondaryLock);
    interposer_enable();
    return result;
}

int crypto_global_init(int useAccel, const char *accelName, const char *accelDir) {
    interposer_disable();
    G_LOCK(shadowtorpreloadPrimaryLock);
    interposer_enable();

    shadowtorpreloadGlobalState.nTorCryptoNodes++;

    gint result = 0;
    if(!shadowtorpreloadGlobalState.sslInitializedGlobal) {
        shadowtorpreloadGlobalState.sslInitializedGlobal = TRUE;
        if(_shadowtorpreload_getWorker()->active->vtable.tor_ssl_global_init) {
            _shadowtorpreload_getWorker()->active->vtable.tor_ssl_global_init();
        }
        if(_shadowtorpreload_getWorker()->active->vtable.crypto_global_init) {
            result = _shadowtorpreload_getWorker()->active->vtable.crypto_global_init(useAccel, accelName, accelDir);
        }
    }

    interposer_disable();
    G_UNLOCK(shadowtorpreloadPrimaryLock);
    interposer_enable();
    return result;
}

int crypto_global_cleanup(void) {
    interposer_disable();
    G_LOCK(shadowtorpreloadPrimaryLock);
    interposer_enable();

    gint result = 0;
    if(--shadowtorpreloadGlobalState.nTorCryptoNodes == 0) {
        if(_shadowtorpreload_getWorker()->active->vtable.crypto_global_cleanup) {
            result = _shadowtorpreload_getWorker()->active->vtable.crypto_global_cleanup();
        }
    }

    interposer_disable();
    G_UNLOCK(shadowtorpreloadPrimaryLock);
    interposer_enable();
    return result;
}

void tor_ssl_global_init() {
    // do nothing, we initialized openssl above in crypto_global_init
}

static void _shadowtorpreload_cryptoSetup(int numLocks) {
    interposer_disable();
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
    interposer_enable();
}

static void _shadowtorpreload_cryptoTeardown(void) {
    interposer_disable();
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
    interposer_enable();
}

static unsigned long _shadowtorpreload_getIDFunc(void) {
    /* return an ID that is unique for each thread */
    return (unsigned long)(_shadowtorpreload_getWorker());
}

static void _shadowtorpreload_cryptoLockingFunc(int mode, int n, const char *file, int line) {
    assert(shadowtorpreloadGlobalState.initialized);

    GRWLock* lock = &(shadowtorpreloadGlobalState.cryptoThreadLocks[n]);
    assert(lock);

    interposer_disable();
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
    interposer_enable();
}

static struct CRYPTO_dynlock_value* _shadowtorpreload_dynlock_create(const char *file, int line) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.crypto_dyn_create_function != NULL);
    struct CRYPTO_dynlock_value* ret = shadowtorpreloadGlobalState.crypto_dyn_create_function(file, line);
    interposer_enable();
    return ret;
}

static void _shadowtorpreload_dynlock_lock(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.crypto_dyn_lock_function != NULL);
    shadowtorpreloadGlobalState.crypto_dyn_lock_function(mode, l, file, line);
    interposer_enable();
}

static void _shadowtorpreload_dynlock_destroy(struct CRYPTO_dynlock_value *l, const char *file, int line) {
    interposer_disable();
    g_assert(shadowtorpreloadGlobalState.crypto_dyn_destroy_function != NULL);
    shadowtorpreloadGlobalState.crypto_dyn_destroy_function(l, file, line);
    interposer_enable();
}

//unsigned long (*CRYPTO_get_id_callback(void))(void) {
//    return (unsigned long)_shadowtorpreload_getIDFunc;
//}
//
//void (*CRYPTO_get_locking_callback(void))(int mode,int type,const char *file,
//        int line) {
//    return _shadowtorpreload_cryptoLockingFunc;
//}

void CRYPTO_set_id_callback(unsigned long (*func)(void)) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);
    if(!shadowtorpreloadGlobalState.crypto_id_fn && func) {
        shadowtorpreloadGlobalState.crypto_id_fn = func;
        g_assert(_shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_id_callback != NULL);
        _shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_id_callback(_shadowtorpreload_getIDFunc);
    }
    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
}

void CRYPTO_set_locking_callback(void (*func)(int mode,int type, const char *file,int line)) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);
    if(!shadowtorpreloadGlobalState.crypto_lock_fn && func) {
        shadowtorpreloadGlobalState.crypto_lock_fn = func;
        g_assert(_shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_locking_callback != NULL);
        _shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_locking_callback(_shadowtorpreload_cryptoLockingFunc);
    }
    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
}

void CRYPTO_set_dynlock_create_callback(struct CRYPTO_dynlock_value *(*dyn_create_function)(const char *file, int line)) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);
    if(!shadowtorpreloadGlobalState.crypto_dyn_create_function && dyn_create_function) {
        shadowtorpreloadGlobalState.crypto_dyn_create_function = dyn_create_function;
        g_assert(_shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_dynlock_create_callback != NULL);
        _shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_dynlock_create_callback(_shadowtorpreload_dynlock_create);
    }
    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
}

void CRYPTO_set_dynlock_lock_callback(void (*dyn_lock_function)(int mode, struct CRYPTO_dynlock_value *l, const char *file, int line)) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);
    if(!shadowtorpreloadGlobalState.crypto_dyn_lock_function && dyn_lock_function) {
        shadowtorpreloadGlobalState.crypto_dyn_lock_function = dyn_lock_function;
        g_assert(_shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_dynlock_lock_callback != NULL);
        _shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_dynlock_lock_callback(_shadowtorpreload_dynlock_lock);
    }
    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
}

void CRYPTO_set_dynlock_destroy_callback(void (*dyn_destroy_function)(struct CRYPTO_dynlock_value *l, const char *file, int line)) {
    interposer_disable();
    G_LOCK(shadowtorpreloadTrenaryLock);
    if(!shadowtorpreloadGlobalState.crypto_dyn_destroy_function && dyn_destroy_function) {
        shadowtorpreloadGlobalState.crypto_dyn_destroy_function = dyn_destroy_function;
        g_assert(_shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_dynlock_destroy_callback != NULL);
        _shadowtorpreload_getWorker()->active->vtable.CRYPTO_set_dynlock_destroy_callback(_shadowtorpreload_dynlock_destroy);
    }
    G_UNLOCK(shadowtorpreloadTrenaryLock);
    interposer_enable();
}

/********************************************************************************
 * end code that supports multi-threaded openssl and libevent.
 ********************************************************************************/
