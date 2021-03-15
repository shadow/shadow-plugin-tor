/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <event2/dns.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

/********************************************************************************
 * start interposition functions
 ********************************************************************************/

/* tor family */

//int write_str_to_file(const char *fname, const char *str, int bin) {
//    /* check if filepath is a consenus file. store it in separate files
//     * so we don't lose old consenus info on overwrites. */
//    if (g_str_has_suffix(fname, "cached-consensus")) {
//        //if(g_strrstr(filepath, "cached-consensus") != NULL) {
//        GString* newPath = g_string_new(fname);
//        GError* error = NULL;
//        g_string_append_printf(newPath, ".%03i", _shadowtorpreload_getWorker()->consensusCounter++);
//        if (!g_file_set_contents(newPath->str, str, -1, &error)) {
//            log_warn(LD_GENERAL, "Error writing file '%s' to track consensus update: error %i: %s",
//                    newPath->str, error->code, error->message);
//        }
//        g_string_free(newPath, TRUE);
//    }
//
//    return _shadowtorpreload_getWorker()->active->vtable.write_str_to_file(fname, str, bin);
//}

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

// Callback return type changed from void to int in OpenSSL_1_1_0-pre1.
#if OPENSSL_VERSION_NUMBER >= 0x010100001L
static int nop_seed(const void* buf, int num) { return 1; }
#else
static void nop_seed(const void* buf, int num) {}
#endif

void RAND_add(const void *buf, int num, double entropy) {
    return;
}

// Callback return type changed from void to int in OpenSSL_1_1_0-pre1.
#if OPENSSL_VERSION_NUMBER >= 0x010100001L
static int nop_add(const void* buf, int num, double entropy) { return 1; }
#else
static void nop_add(const void* buf, int num, double entropy) {}
#endif

int RAND_poll() {
    return 1;
}

static int _shadowtorpreload_getRandomBytes(unsigned char* buf, int numBytes) {
    // shadow interposes this and will fill the buffer for us
    // return 1 on success, 0 otherwise
    return (numBytes == syscall(SYS_getrandom, buf, (size_t)numBytes, 0)) ? 1 : 0;
}

int RAND_bytes(unsigned char *buf, int num) {
    return _shadowtorpreload_getRandomBytes(buf, num);
}

int RAND_pseudo_bytes(unsigned char *buf, int num) {
    return _shadowtorpreload_getRandomBytes(buf, num);
}

// Some versions of openssl define RAND_cleanup as a macro.
// Get that definition out of the way so we can override the library symbol in
// case it still exists.
#ifdef RAND_cleanup
#undef RAND_cleanup
#endif

void RAND_cleanup(void) {
    return;
}

int RAND_status(void) {
    return 1;
}

static const RAND_METHOD shadowtorpreload_customRandMethod = {
    .seed = nop_seed,
    .bytes = RAND_bytes,
    .cleanup = RAND_cleanup,
    .add = nop_add,
    .pseudorand = RAND_pseudo_bytes,
    .status = RAND_status,
};

const RAND_METHOD* RAND_get_rand_method() {
    return (const RAND_METHOD*)(&shadowtorpreload_customRandMethod);
}

RAND_METHOD* RAND_SSLeay() {
    return (RAND_METHOD*)(&shadowtorpreload_customRandMethod);
}
