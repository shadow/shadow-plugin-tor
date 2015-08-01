/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <gmodule.h>

extern int tor_main(int argc, char *argv[]);
extern void shadowtorpreload_init(GModule*);
extern void shadowtorpreload_clear(void);

const char tor_git_revision[] =
#ifndef _MSC_VER
#include "micro-revision.i"
#endif
  "";

static char* _shadowtor_get_formatted_arg_str(char* arg_str, const char* homedir_str, const char* hostname_str) {
	char* found = NULL;
	GString* sbuffer = g_string_new(arg_str);

	/* replace all ~ with the home directory */
	while((found = g_strstr_len(sbuffer->str, sbuffer->len, "~"))) {
		ssize_t position = (ssize_t) (found - sbuffer->str);
		sbuffer = g_string_erase(sbuffer, position, (ssize_t) 1);
		sbuffer = g_string_insert(sbuffer, position, homedir_str);
	}

	/* replace all ${NODEID} with the hostname */
	while((found = g_strstr_len(sbuffer->str, sbuffer->len, "${NODEID}"))) {
		ssize_t position = (ssize_t) (found - sbuffer->str);
		sbuffer = g_string_erase(sbuffer, position, (ssize_t) 9);
		sbuffer = g_string_insert(sbuffer, position, hostname_str);
	}

	return g_string_free(sbuffer, FALSE);
}

static const char* _shadowtor_get_hostname_str() {
	char hostname_buf[128];
	memset(hostname_buf, 0, 128);
	if(gethostname(&hostname_buf, 127) < 0) {
		return NULL;
	}
	return strdup(hostname_buf);
}

static const char* _shadowtor_get_homedir_str() {
	return g_get_home_dir();
}

int main(int argc, char *argv[]) {
	/* get some basic info about this node */
	const char* homedir_str = _shadowtor_get_homedir_str();
	const char* hostname_str = _shadowtor_get_hostname_str();
	int retval = -1;

	if(homedir_str && hostname_str) {
		/* convert special formatted arguments, like expanding '~' and '${NODEID}' */
		char* formatted_args[argc];
		for(int i = 0; i < argc; i++) {
			formatted_args[i] = _shadowtor_get_formatted_arg_str(argv[i], homedir_str, hostname_str);
		}

		/* launch tor! */
		retval = tor_main(argc, argv);
	}

	/* cleanup before return */
	if(homedir_str) free(homedir_str);
	if(hostname_str) free(hostname_str);

	return retval;
}

/* called immediately after the plugin is loaded. shadow loads plugins once for
 * each worker thread. the GModule* is needed as a handle for g_module_symbol()
 * symbol lookups.
 * return NULL for success, or a string describing the error */
const gchar* g_module_check_init(GModule *module) {
	shadowtorpreload_init(module);
	return NULL;
}

/* called immediately after the plugin is unloaded. shadow unloads plugins
 * once for each worker thread. */
void g_module_unload(GModule *module) {
    shadowtorpreload_clear();
}
