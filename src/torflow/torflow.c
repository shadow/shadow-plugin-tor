/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

static GLogLevelFlags torflowLogFilterLevel = G_LOG_LEVEL_INFO;

static const gchar* _torflow_logLevelToString(GLogLevelFlags logLevel) {
    switch (logLevel) {
        case G_LOG_LEVEL_ERROR:
            return "error";
        case G_LOG_LEVEL_CRITICAL:
            return "critical";
        case G_LOG_LEVEL_WARNING:
            return "warning";
        case G_LOG_LEVEL_MESSAGE:
            return "message";
        case G_LOG_LEVEL_INFO:
            return "info";
        case G_LOG_LEVEL_DEBUG:
            return "debug";
        default:
            return "default";
    }
}

void torflow_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...) {
    if(level > torflowLogFilterLevel) {
        return;
    }

    va_list vargs;
    va_start(vargs, format);

    GDateTime* dt = g_date_time_new_now_local();
    GString* newformat = g_string_new(NULL);

    g_string_append_printf(newformat, "%04i-%02i-%02i %02i:%02i:%02i %"G_GINT64_FORMAT".%06i [%s] [%s] %s",
            g_date_time_get_year(dt), g_date_time_get_month(dt), g_date_time_get_day_of_month(dt),
            g_date_time_get_hour(dt), g_date_time_get_minute(dt), g_date_time_get_second(dt),
            g_date_time_to_unix(dt), g_date_time_get_microsecond(dt),
            _torflow_logLevelToString(level), functionName, format);

    gchar* message = g_strdup_vprintf(newformat->str, vargs);
    g_print("%s\n", message);
    g_free(message);

    g_string_free(newformat, TRUE);
    g_date_time_unref(dt);

    va_end(vargs);
}

int main(int argc, char *argv[]) {
    gchar hostname[128];
    memset(hostname, 0, 128);
    gethostname(hostname, 128);
	message("Starting torflow program on host %s process id %i", hostname, (gint)getpid());

	message("Parsing program arguments");
	TorFlowConfig* config = torflowconfig_new(argc, argv);
	if(config == NULL) {
	    message("Parsing config failed, exiting with failure");
	    return EXIT_FAILURE;
	}

	/* update to the configured log level */
	torflowLogFilterLevel = torflowconfig_getLogLevel(config);

	message("Creating event manager to run main loop");
	TorFlowEventManager* manager = torfloweventmanager_new();
    if(manager == NULL) {
        message("Creating event manager failed, exiting with failure");
        return EXIT_FAILURE;
    }

    TorFlowMode mode = torflowconfig_getMode(config);
    TorFlowAuthority* authority = NULL;
    TorFlowFileListener* listener = NULL;

    if(mode == TORFLOW_MODE_TORFLOW) {
        message("Starting in TorFlow mode, creating TorFlow authority");

        authority = torflowauthority_new(config, manager);
        if(authority == NULL) {
            message("Creating authority failed, exiting with failure");
            return EXIT_FAILURE;
        }
    } else {
        in_port_t listenPort = torflowconfig_getListenerPort(config);

        message("Starting in FileServer mode, creating file server listener on port %u", ntohs(listenPort));

        listener = torflowfilelistener_new(manager, 0, listenPort);
        if(listener == NULL) {
            message("Creating listener failed, exiting with failure");
            return EXIT_FAILURE;
        }
    }

	/* now the authority should have caused descriptors to get created that are waiting
	 * for events. when those events occur, new actions will be taken. */
	message("Running main loop");
	gboolean success = torfloweventmanager_runMainLoop(manager);

	message("Main loop returned, cleaning up");
	if(authority != NULL) {
        torflowauthority_free(authority);
	}
	if(listener != NULL) {
	    torflowfilelistener_free(listener);
	}
	torfloweventmanager_free(manager);
	torflowconfig_free(config);

	message("Exiting cleanly with %s code", success ? "success" : "failure");
	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
