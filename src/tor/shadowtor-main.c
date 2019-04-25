/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <glib.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

extern int tor_main(int argc, char *argv[]);
extern const RAND_METHOD* RAND_get_rand_method();

typedef void (*CRYPTO_lock_func)(int, int, const char*, int);
typedef unsigned long (*CRYPTO_id_func)(void);

/* only define this variable in older versions of Tor.
 * it was moved to src/or/git_revision.c right before version 0.3.3.1
 * in commit 72b5e4a2db4282002fe50e11c2b8a79e108d30f8*/
#ifndef NO_GIT_REVISION
const char tor_git_revision[] =
#ifndef _MSC_VER
#include "micro-revision.i"
#endif
        "";
#endif

static char* _shadowtor_get_formatted_arg_str(char* arg_str,
        const char* homedir_str, const char* hostname_str) {
    char* found = NULL;
    GString* sbuffer = g_string_new(arg_str);

    /* replace first ~ with the home directory */
    if ((found = g_strstr_len(sbuffer->str, sbuffer->len, "~"))) {
        ssize_t position = (ssize_t) (found - sbuffer->str);
        sbuffer = g_string_erase(sbuffer, position, (ssize_t) 1);
        sbuffer = g_string_insert(sbuffer, position, homedir_str);
    }

    /* replace first ${NODEID} with the hostname */
    if ((found = g_strstr_len(sbuffer->str, sbuffer->len, "${NODEID}"))) {
        ssize_t position = (ssize_t) (found - sbuffer->str);
        sbuffer = g_string_erase(sbuffer, position, (ssize_t) 9);
        sbuffer = g_string_insert(sbuffer, position, hostname_str);
    }

    return g_string_free(sbuffer, FALSE);
}

static char* _shadowtor_get_hostname_str() {
    char hostname_buf[128];
    memset(hostname_buf, 0, 128);
    if (gethostname(&hostname_buf[0], 127) < 0) {
        return NULL;
    }
    return strdup(hostname_buf);
}

static const char* _shadowtor_get_homedir_str() {
    return g_get_home_dir();
}

static int _shadowtor_run(int argc, char *argv[]) {
    int retval = -1;

    /* make sure openssl uses Shadow's random sources */
    const RAND_METHOD* shadowtor_randomMethod = RAND_get_rand_method();
    RAND_set_rand_method(shadowtor_randomMethod);

    /* get some basic info about this node */
    const char* homedir_str = _shadowtor_get_homedir_str();
    char* hostname_str = _shadowtor_get_hostname_str();

    if (homedir_str && hostname_str) {
        /* convert special formatted arguments, like expanding '~' and '${NODEID}' */
        char** formatted_args = calloc(argc, sizeof(char*));

        int i = 0;
        for (i = 0; i < argc; i++) {
            formatted_args[i] = _shadowtor_get_formatted_arg_str(argv[i], homedir_str, hostname_str);
        }

        /* launch tor! */
        retval = tor_main(argc, formatted_args);

        for (i = 0; i < argc; i++) {
            free(formatted_args[i]);
        }
        free(formatted_args);
    }

    /* cleanup before return */

    /* We get the homedir_str from glib right now, which seems to be returning
     * a pointer to the text area of memory. Therefore, we do not need to free
     * homedir_str */
    //if (homedir_str) {
    //   free(homedir_str);
    //}
    if (hostname_str) {
        free(hostname_str);
    }

    return retval;
}

int main(int argc, char *argv[]) {
    return _shadowtor_run(argc, argv);
}
