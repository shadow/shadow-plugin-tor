/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowConfig {
    /* See the README file for explanation of these arguments */
    TorFlowMode mode;

    gchar* v3bwInitFilePath;

    guint numParallelProbes;
    guint numRelaysPerSlice;
    guint scanIntervalSeconds;
    gdouble maxRelayWeightFraction;

    in_port_t torSocksPort;
    in_port_t torControlPort;
    in_port_t listenPort;

    guint probeTimeoutSeconds;
    guint numProbesPerRelay;
    GLogLevelFlags logLevel;

    GQueue* fileServerPeers;
};

static gboolean _torflowconfig_parseV3BWInitFilePath(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    if(config->v3bwInitFilePath != NULL) {
        g_free(config->v3bwInitFilePath);
        config->v3bwInitFilePath = NULL;
    }

    /* make sure we can open the path */
    gchar* path = g_strdup(value);

    /* make sure the path exists */
    if(g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        config->v3bwInitFilePath = path;
        return TRUE;
    } else {
        g_free(path);
        return FALSE;
    }
}

static gboolean _torflowconfig_parseScanInterval(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint intValue = atoi(value);
    if(intValue < 0) {
        return FALSE;
    }

    config->scanIntervalSeconds = (guint)intValue;

    return TRUE;
}

static gboolean _torflowconfig_parseNumProbes(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint intValue = atoi(value);
    if(intValue < 1) {
        return FALSE;
    }

    config->numParallelProbes = (guint)intValue;

    return TRUE;
}

static gboolean _torflowconfig_parseNumRelaysPerSlice(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint intValue = atoi(value);
    if(intValue < 1) {
        return FALSE;
    }

    config->numRelaysPerSlice = (guint)intValue;

    return TRUE;
}

static gboolean _torflowconfig_parseProbeTimeoutSeconds(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint intValue = atoi(value);
    if(intValue < 1) {
        return FALSE;
    }

    config->probeTimeoutSeconds = (guint)intValue;

    return TRUE;
}

static gboolean _torflowconfig_parseNumProbesPerRelay(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint intValue = atoi(value);
    if(intValue < 1) {
        return FALSE;
    }

    config->numProbesPerRelay = (guint)intValue;

    return TRUE;
}

static gboolean _torflowconfig_parseTorSocksPort(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint port = atoi(value);

    if(port < 1 || port > G_MAXUINT16) {
        return FALSE;
    }

    config->torSocksPort = (in_port_t)htons((in_port_t)port);

    return TRUE;
}

static gboolean _torflowconfig_parseTorControlPort(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint port = atoi(value);

    if(port < 1 || port > G_MAXUINT16) {
        return FALSE;
    }

    config->torControlPort = (in_port_t)htons((in_port_t)port);

    return TRUE;
}

static gboolean _torflowconfig_parseListenPort(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    gint port = atoi(value);

    if(port < 1 || port > G_MAXUINT16) {
        return FALSE;
    }

    config->listenPort = (in_port_t)htons((in_port_t)port);

    return TRUE;
}

static gboolean _torflowconfig_parseMaxRelayWeightFraction(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    config->maxRelayWeightFraction = atof(value);

    return TRUE;
}

static gboolean _torflowconfig_parseFileServerInfo(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    /* get file server network name and port info */
    gchar** parts = g_strsplit(value, ":", 2);

    /* the server domain name */
    gchar* name = parts[0];

    /* port in host order */
    gchar* hostFilePortStr = parts[1];
    gint hostFilePort = atoi(hostFilePortStr);

    if(hostFilePort < 0 || hostFilePort > G_MAXUINT16) {
        g_strfreev(parts);
        return FALSE;
    }

    in_port_t netFilePort = htons((in_port_t)hostFilePort);

    TorFlowPeer* peer = torflowpeer_new(name, netFilePort);
    if(peer == NULL) {
        g_strfreev(parts);
        return FALSE;
    }

    g_queue_push_tail(config->fileServerPeers, peer);

    info("parsed TorFlow file server '%s' at %s:%u",
            torflowpeer_getName(peer),
            torflowpeer_getHostIPStr(peer),
            ntohs(torflowpeer_getNetPort(peer)));

    g_strfreev(parts);
    return TRUE;
}

static gboolean _torflowconfig_parseLogLevel(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    if(!g_ascii_strcasecmp(value, "debug")) {
        config->logLevel = G_LOG_LEVEL_DEBUG;
    } else if(!g_ascii_strcasecmp(value, "info")) {
        config->logLevel = G_LOG_LEVEL_INFO;
    } else if(!g_ascii_strcasecmp(value, "message")) {
        config->logLevel = G_LOG_LEVEL_MESSAGE;
    } else if(!g_ascii_strcasecmp(value, "warning")) {
        config->logLevel = G_LOG_LEVEL_WARNING;
    } else {
        warning("invalid log level '%s' provided, see README for valid values", value);
        return FALSE;
    }

    return TRUE;
}

static gboolean _torflowconfig_parseMode(TorFlowConfig* config, gchar* value) {
    g_assert(config && value);

    if(!g_ascii_strcasecmp(value, "TorFlow")) {
        config->mode = TORFLOW_MODE_TORFLOW;
    } else if(!g_ascii_strcasecmp(value, "FileServer")) {
        config->mode = TORFLOW_MODE_FILESERVER;
    } else {
        warning("invalid mode '%s' provided, see README for valid values", value);
        return FALSE;
    }

    return TRUE;
}

TorFlowConfig* torflowconfig_new(gint argc, gchar* argv[]) {
    gboolean hasError = FALSE;

    TorFlowConfig* config = g_new0(TorFlowConfig, 1);

    /* set defaults, which will get overwritten if set in args */
    config->mode = TORFLOW_MODE_TORFLOW;
    config->probeTimeoutSeconds = 300;
    config->numProbesPerRelay = 5;
    config->numRelaysPerSlice = 50;
    config->numParallelProbes = 4;
    config->scanIntervalSeconds = 0;
    config->maxRelayWeightFraction = 0.05;
    config->logLevel = G_LOG_LEVEL_INFO;
    config->listenPort = (in_port_t)htons((in_port_t)18080);

    /* hold fileserver peer info */
    config->fileServerPeers = g_queue_new();

    /* parse all of the key=value pairs, skip the first program name arg */
    for(gint i = 1; i < argc; i++) {
        gchar* entry = argv[i];

        gchar** parts = g_strsplit(entry, "=", 2);
        gchar* key = parts[0];
        gchar* value = parts[1];

        if(key != NULL && value != NULL) {
            /* we have both key and value in key=value entry */
            if(!g_ascii_strcasecmp(key, "V3BWFilePath")) {
                if(!_torflowconfig_parseV3BWInitFilePath(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "ScanIntervalSeconds")) {
                if(!_torflowconfig_parseScanInterval(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "NumParallelProbes")) {
                if(!_torflowconfig_parseNumProbes(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "NumRelaysPerSlice")) {
                if(!_torflowconfig_parseNumRelaysPerSlice(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "TorSocksPort")) {
                if(!_torflowconfig_parseTorSocksPort(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "TorControlPort")) {
                if(!_torflowconfig_parseTorControlPort(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "ListenPort")) {
                if(!_torflowconfig_parseListenPort(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "MaxRelayWeightFraction")) {
                if(!_torflowconfig_parseMaxRelayWeightFraction(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "FileServerInfo")) {
                if(!_torflowconfig_parseFileServerInfo(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "ProbeTimeoutSeconds")) {
                if(!_torflowconfig_parseProbeTimeoutSeconds(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "NumProbesPerRelay")) {
                if(!_torflowconfig_parseNumProbesPerRelay(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "LogLevel")) {
                if(!_torflowconfig_parseLogLevel(config, value)) {
                    hasError = TRUE;
                }
            } else if(!g_ascii_strcasecmp(key, "Mode")) {
                if(!_torflowconfig_parseMode(config, value)) {
                    hasError = TRUE;
                }
            }

            if(hasError) {
                critical("error in config: key='%s' value='%s", key, value);
            } else {
                message("successfully parsed key='%s' value='%s'", key, value);
            }
        } else {
            /* we are missing either the key or the value */
            hasError = TRUE;

            if(key != NULL) {
                critical("can't find key in config entry");
            } else {
                critical("can't find value in config entry; key='%s'", key);
            }
        }

        g_strfreev(parts);

        if(hasError) {
            torflowconfig_free(config);
            return NULL;
        }
    }

    /* now make sure we have the required arguments (FileServer mode has no required args) */
    if(config->mode == TORFLOW_MODE_TORFLOW) {
        if(config->torSocksPort == 0) {
            critical("missing required valid Tor SOCKS port argument `TorSocksPort`");
            torflowconfig_free(config);
            return NULL;
        }
        if(config->torControlPort == 0) {
            critical("missing required valid Tor control port argument `TorControlPort`");
            torflowconfig_free(config);
            return NULL;
        }
        if(config->v3bwInitFilePath == NULL) {
            critical("missing required valid V3BW file path `V3BWFilePath`");
            torflowconfig_free(config);
            return NULL;
        }
        if(g_queue_is_empty(config->fileServerPeers)) {
            critical("missing required TorFlow file server infos `FileServerInfo`");
            torflowconfig_free(config);
            return NULL;
        }
    }

    return config;
}

void torflowconfig_free(TorFlowConfig* config) {
    g_assert(config);

    if(config->v3bwInitFilePath != NULL) {
        g_free(config->v3bwInitFilePath);
    }

    while(config->fileServerPeers != NULL && !g_queue_is_empty(config->fileServerPeers)) {
        TorFlowPeer* peer = g_queue_pop_head(config->fileServerPeers);
        if(peer) {
            torflowpeer_unref(peer);
        }
    }

    g_free(config);
}

const gchar* torflowconfig_getV3BWFilePath(TorFlowConfig* config) {
    g_assert(config);
    return config->v3bwInitFilePath;
}

in_port_t torflowconfig_getTorSocksPort(TorFlowConfig* config) {
    g_assert(config);
    return config->torSocksPort;
}

in_port_t torflowconfig_getTorControlPort(TorFlowConfig* config) {
    g_assert(config);
    return config->torControlPort;
}

in_port_t torflowconfig_getListenerPort(TorFlowConfig* config) {
    g_assert(config);
    return config->listenPort;
}

guint torflowconfig_getScanIntervalSeconds(TorFlowConfig* config) {
    g_assert(config);
    return config->scanIntervalSeconds;
}

guint torflowconfig_getNumParallelProbes(TorFlowConfig* config) {
    g_assert(config);
    return config->numParallelProbes;
}

guint torflowconfig_getNumRelaysPerSlice(TorFlowConfig* config) {
    g_assert(config);
    return config->numRelaysPerSlice;
}

gdouble torflowconfig_getMaxRelayWeightFraction(TorFlowConfig* config) {
    g_assert(config);
    return config->maxRelayWeightFraction;
}

guint torflowconfig_getProbeTimeoutSeconds(TorFlowConfig* config) {
    g_assert(config);
    return config->probeTimeoutSeconds;
}

guint torflowconfig_getNumProbesPerRelay(TorFlowConfig* config) {
    g_assert(config);
    return config->numProbesPerRelay;
}

GLogLevelFlags torflowconfig_getLogLevel(TorFlowConfig* config) {
    g_assert(config);
    return config->logLevel;
}

TorFlowMode torflowconfig_getMode(TorFlowConfig* config) {
    g_assert(config);
    return config->mode;
}

TorFlowPeer* torflowconfig_cycleFileServerPeers(TorFlowConfig* config) {
    g_assert(config);
    TorFlowPeer* peer = g_queue_pop_head(config->fileServerPeers);
    if(peer) {
        g_queue_push_tail(config->fileServerPeers, peer);
    }
    return peer;
}
