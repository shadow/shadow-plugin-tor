/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include <glib.h>

#include "shadowtor.h"

int shadowtorinterpose_event_base_loopexit(struct event_base * base, const struct timeval * t) {
    ScallionTor* stor = scalliontor_getPointer();
    g_assert(stor);

    scalliontor_loopexit(stor);
    return 0;
}

int shadowtorinterpose_tor_open_socket(int domain, int type, int protocol)
{
  int s = socket(domain, type | SOCK_NONBLOCK, protocol);
  if (s >= 0) {
    socket_accounting_lock();
    ++n_sockets_open;
//    mark_socket_open(s);
    socket_accounting_unlock();
  }
  return s;
}

void shadowtorinterpose_tor_gettimeofday(struct timeval *timeval) {
    struct timespec tp;
    clock_gettime(CLOCK_REALTIME, &tp);
    timeval->tv_sec = tp.tv_sec;
    timeval->tv_usec = tp.tv_nsec/1000;
}

int shadowtorinterpose_spawn_func(void (*func)(void *), void *data)
{
    ScallionTor* stor = scalliontor_getPointer();
    g_assert(stor);

    /* this takes the place of forking a cpuworker and running cpuworker_main.
     * func points to cpuworker_main, but we'll implement a version that
     * works in shadow */
    int *fdarray = data;
    int fd = fdarray[1]; /* this side is ours */

    scalliontor_newCPUWorker(stor, fd);

    /* now we should be ready to receive events in vtor_cpuworker_readable */
    return 0;
}

/* this function is where the relay will return its bandwidth and send to auth.
 * this should be computing an estimate of the relay's actual bandwidth capacity.
 * let the maximum 10 second rolling average bytes be MAX10S; then, this should compute:
 * min(MAX10S read, MAX10S write) */
int shadowtorinterpose_rep_hist_bandwidth_assess() {
    ScallionTor* stor = scalliontor_getPointer();
    g_assert(stor);

    /* return BW in bytes. tor will divide the value we return by 1000 and put it in the descriptor. */
    return stor->bandwidth;
}

/* this is the authority function to compute the consensus "w Bandwidth" line */
uint32_t shadowtorinterpose_router_get_advertised_bandwidth_capped(const routerinfo_t *router)
{
  /* this is what the relay told us. dont worry about caps, since this bandwidth
   * is authoritative in our sims */
  return router->bandwidthcapacity;
}

void shadowtorinterpose_mark_logs_temp(void) {
    scalliontor_setLogging();
}


/* we need to init and clean up all of the node-specific state while only
 * calling the global openssl init and cleanup funcs once.
 *
 * node-specific state can be inited and cleaned up here
 *
 * other stuff can be cleaned up in _shadowtorpreload_cryptoSetup() and
 * _shadowtorpreload_cryptoTeardown() as those ensure a single execution globally.
 *
 * TODO: for now we ignore node-specific cleanup
 */
int shadowtorinterpose_crypto_global_init(int useAccel, const char *accelName, const char *accelDir) {
    return 0;
}
int shadowtorinterpose_crypto_global_cleanup(void) {
    return 0;
}

int shadowtorinterpose_crypto_early_init() {
    if (crypto_seed_rng(1) < 0)
      return -1;
    if (crypto_init_siphash_key() < 0)
      return -1;
    return 0;
}


/**
 * Crypto optimizations when running Tor in Shadow.
 * AES and cipher encryption functions are skipped for efficiency.
 * The random() funcs are skipped so we dont mess up Shadow's determinism.
 */

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void shadowtorinterpose_AES_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void shadowtorinterpose_AES_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void shadowtorinterpose_AES_ctr128_encrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
}

/*
 * const AES_KEY *key
 * The key parameter has been voided to avoid requiring Openssl headers
 */
void shadowtorinterpose_AES_ctr128_decrypt(const unsigned char *in, unsigned char *out, const void *key) {
    return;
}

/*
 * EVP_CIPHER_CTX *ctx
 * The ctx parameter has been voided to avoid requiring Openssl headers
 */
int shadowtorinterpose_EVP_Cipher(void *ctx, unsigned char *out, const unsigned char *in, unsigned int inl) {
    memmove(out, in, (size_t)inl);
    return 1;
}

void shadowtorinterpose_RAND_seed(const void *buf, int num) {
    return;
}

void shadowtorinterpose_RAND_add(const void *buf, int num, double entropy) {
    return;
}

int shadowtorinterpose_RAND_poll() {
    return 1;
}

static gint _shadowtorinterpose_getRandomBytes(guchar* buf, gint numBytes) {
    gint bytesWritten = 0;

    while(numBytes > bytesWritten) {
        gint r = rand();
        gint copyLength = MIN(numBytes-bytesWritten, sizeof(gint));
        g_memmove(buf+bytesWritten, &r, copyLength);
        bytesWritten += copyLength;
    }

    return 1;
}

int shadowtorinterpose_RAND_bytes(unsigned char *buf, int num) {
    return _shadowtorinterpose_getRandomBytes(buf, num);
}

int shadowtorinterpose_RAND_pseudo_bytes(unsigned char *buf, int num) {
    return _shadowtorinterpose_getRandomBytes(buf, num);
}

void shadowtorinterpose_RAND_cleanup() {
    return;
}

int shadowtorinterpose_RAND_status() {
    return 1;
}

static const struct {
    void* seed;
    void* bytes;
    void* cleanup;
    void* add;
    void* pseudorand;
    void* status;
} shadowtorinterpose_customRandMethod = {
    shadowtorinterpose_RAND_seed,
    shadowtorinterpose_RAND_bytes,
    shadowtorinterpose_RAND_cleanup,
    shadowtorinterpose_RAND_add,
    shadowtorinterpose_RAND_pseudo_bytes,
    shadowtorinterpose_RAND_status
};

const void* shadowtorinterpose_RAND_get_rand_method(void) {
    return (const void *)(&shadowtorinterpose_customRandMethod);
}

const void* shadowtorinterpose_RAND_SSLeay(void) {
    return (const void *)(&shadowtorinterpose_customRandMethod);
}
