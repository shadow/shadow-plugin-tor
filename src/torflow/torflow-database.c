/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowDatabase {
    TorFlowConfig* config;

    GHashTable* relaysByIdentity;
    guint v3bwVersion;
};

static void _torflowdatabase_updateAuthoritativeLink(const gchar* configuredPath, const gchar* newPath){
    /* sanity check for directory */
    if(g_file_test(configuredPath, G_FILE_TEST_IS_DIR)) {
        error("configured path '%s' for v3bw file must not be a directory", configuredPath);
        return;
    }

    /* first lets make sure the path we want to write is clear */
    gint result = 0;
    if(g_file_test(configuredPath, G_FILE_TEST_IS_SYMLINK)) {
        /* it is a symlink, so its safe to remove */
        result = g_unlink(configuredPath);
        if(result < 0) {
            warning("g_unlink() returned %i error %i: %s",
                    result, errno, g_strerror(errno));
        }
    } else if(g_file_test(configuredPath, G_FILE_TEST_IS_REGULAR)) {
        /* not a symlink, but a regular file. dont overwrite it */
        GString* backupFilepathBuffer = g_string_new(configuredPath);
        g_string_append_printf(backupFilepathBuffer, ".init");

        result = g_rename(configuredPath, backupFilepathBuffer->str);
        if(result < 0) {
            warning("g_rename() returned %i error %i: %s",
                    result, errno, g_strerror(errno));
        } else {
            /* the file better not exist anymore */
            g_assert(!g_file_test(configuredPath, G_FILE_TEST_IS_REGULAR));
        }

        g_string_free(backupFilepathBuffer, TRUE);
    } else {
        /* its not a symlink, and not a file. so we are creating the first one */
    }

    if(result < 0) {
        return;
    }

    /* the target path for the link is now clear, now we need to set up
     * the reference (where the link is going to point).
     * the linkref should point in the same directory as the link itself.
     */
    const gchar* linkRef = NULL;
    /* get the base filename without directory components */
    gchar* baseFilename = g_strrstr(newPath, "/");
    if(baseFilename) {
        /* chop off the directories */
        linkRef = baseFilename+1;
    } else {
        /* newPath had no directories */
        linkRef = newPath;
    }

    /* now make the configured path exist, pointing to the new file */
    result = symlink(linkRef, configuredPath);
    if(result < 0) {
        warning("Unable to create symlink at %s pointing to %s; symlink() returned %i error %i: %s",
                configuredPath, linkRef, result, errno, g_strerror(errno));
    } else {
        /* that better not be a dangling link */
        g_assert(g_file_test(configuredPath, G_FILE_TEST_IS_SYMLINK) &&
                g_file_test(configuredPath, G_FILE_TEST_IS_REGULAR));

        message("new v3bw file '%s' now linked at '%s'", linkRef, configuredPath);
    }
}

/* Takes in base 64 GString and returns new base 16 GString.
 * Output string should be freed with g_string_free. */
static gchar* _torflowdatabase_base64ToBase16(gchar* base64) {
    gsize* bin_len = g_malloc(sizeof(gsize));

    guchar* base2 = g_base64_decode(base64, bin_len);
    GString* base16 = g_string_sized_new(2 * *bin_len + 1);

    for (gint i = 0; i < *bin_len; i++) {
        g_string_append_printf(base16, "%02x", base2[i]);
    }

    base16 = g_string_ascii_up(base16);

    g_free(base2);
    g_free(bin_len);

    return g_string_free(base16, FALSE);
}

static void _torflowdatabase_markRelayOffline(const gchar* identity, TorFlowRelay* relay, gpointer none) {
    if(relay) {
        torflowrelay_setIsRunning(relay, FALSE);
    }
}

static void _torflowdatabase_storeRelay(TorFlowDatabase* database, TorFlowRelay* newRelay) {
    g_assert(database);

    const gchar* identity = torflowrelay_getIdentity(newRelay);
    TorFlowRelay* storedRelay = g_hash_table_lookup(database->relaysByIdentity, identity);

    if(storedRelay != NULL) {
        torflowrelay_setIsRunning(storedRelay, torflowrelay_getIsRunning(newRelay));
        torflowrelay_setIsFast(storedRelay, torflowrelay_getIsFast(newRelay));
        torflowrelay_setIsExit(storedRelay, torflowrelay_getIsExit(newRelay));
        torflowrelay_setDescriptorBandwidth(storedRelay, torflowrelay_getDescriptorBandwidth(newRelay));
        torflowrelay_setAdvertisedBandwidth(storedRelay, torflowrelay_getAdvertisedBandwidth(newRelay));

        torflowrelay_free(newRelay);
    } else {
        g_hash_table_replace(database->relaysByIdentity, (gpointer)identity, newRelay);
        info("stored relay %s", identity);
    }
}

static TorFlowRelay* _torflowdatabase_parseRelay(TorFlowDatabase* database,
        gchar* relayInfo, gchar* flagInfo, gchar* weightInfo) {
    g_assert(database);

    /* first start with the relay info line */
    gchar** parts = g_strsplit(relayInfo, " ", 4);

    if(parts[0] == NULL || parts[1] == NULL || parts[2] == NULL) {
        warning("relay info in descriptor line is invalid: %s", relayInfo);
        g_strfreev(parts);
        return NULL;
    }

    gchar* nickname = g_strdup(parts[1]);
    gchar* identity = NULL;
    {
        GString* identityBase64Buffer = g_string_new(NULL);
        g_string_printf(identityBase64Buffer, "%s=", parts[2]);
        identity = _torflowdatabase_base64ToBase16(identityBase64Buffer->str);
        g_string_free(identityBase64Buffer, TRUE);
    }

    g_strfreev(parts);

    /* create the relay */
    TorFlowRelay* relay = torflowrelay_new(nickname, identity);

    /* now parse and set the flags */
    gboolean isRunning = (g_strstr_len(flagInfo, -1, " Running") != NULL) ? TRUE : FALSE;
    gboolean isFast = (g_strstr_len(flagInfo, -1, " Fast") != NULL) ? TRUE : FALSE;
    gboolean isExit = (g_strstr_len(flagInfo, -1, " Exit") != NULL) ? TRUE : FALSE;
    if(isExit && isRunning && g_strstr_len(flagInfo, -1, " BadExit") != NULL) {
        isRunning = FALSE;
    }

    torflowrelay_setIsRunning(relay, isRunning);
    torflowrelay_setIsFast(relay, isFast);
    torflowrelay_setIsExit(relay, isExit);

    /* now parse the bandwidth weight line and set the bandwidth in the relay */
    guint descriptorBandwidth = 0;
    gchar* bandwidthStr = g_strstr_len(weightInfo, -1, "Bandwidth=");
    if(bandwidthStr) {
        descriptorBandwidth = (guint)atol(&bandwidthStr[10]);
    }

    torflowrelay_setDescriptorBandwidth(relay, descriptorBandwidth);
    /* normally we would use advertised BW, but that is not available */
    torflowrelay_setAdvertisedBandwidth(relay, descriptorBandwidth);

    return relay;
}

TorFlowDatabase* torflowdatabase_new(TorFlowConfig* config) {
    TorFlowDatabase* database = g_new0(TorFlowDatabase, 1);

    database->config = config;
    database->relaysByIdentity = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify)torflowrelay_free);

    return database;
}

void torflowdatabase_free(TorFlowDatabase* database) {
    g_assert(database);

    g_hash_table_destroy(database->relaysByIdentity);

    g_free(database);
}

guint torflowdatabase_storeNewDescriptors(TorFlowDatabase* database, GQueue* descriptorLines) {
    g_assert(database);

    if(descriptorLines != NULL && !g_queue_is_empty(descriptorLines)) {
        /* mark existing relays offline. online relays will be updated from descriptor content */
        g_hash_table_foreach(database->relaysByIdentity, (GHFunc) _torflowdatabase_markRelayOffline, NULL);
    }

    while(descriptorLines != NULL && !g_queue_is_empty(descriptorLines)) {
        gchar* rLine = g_queue_pop_head(descriptorLines);
        if(!rLine) {
            continue;
        } else if(rLine[0] != 'r') {
            g_free(rLine);
            continue;
        }

        gchar* sLine = g_queue_pop_head(descriptorLines);
        gchar* wLine = g_queue_pop_head(descriptorLines);

        if(sLine && sLine[0] == 's' && wLine && wLine[0] == 'w') {
            TorFlowRelay* relay = _torflowdatabase_parseRelay(database, rLine, sLine, wLine);
            if(relay) {
                _torflowdatabase_storeRelay(database, relay);
            }
        }

        if(rLine) {
            g_free(rLine);
            rLine = NULL;
        }
        if(sLine) {
            g_free(sLine);
            sLine = NULL;
        }
        if(wLine) {
            g_free(wLine);
            wLine = NULL;
        }
    }

    return g_hash_table_size(database->relaysByIdentity);
}

GQueue* torflowdatabase_getMeasureableRelays(TorFlowDatabase* database) {
    g_assert(database);

    GQueue* relays = g_queue_new();

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, database->relaysByIdentity);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        TorFlowRelay* relay = value;
        if(relay && torflowrelay_isMeasureable(relay)) {
            g_queue_insert_sorted(relays, relay, (GCompareDataFunc)torflowrelay_compareData, NULL);
        }
    }

    return relays;
}

void torflowdatabase_storeMeasurementResult(TorFlowDatabase* database,
        gchar* entryIdentity, gchar* exitIdentity, gboolean isSuccess,
        gsize contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
    g_assert(database);

    if(isSuccess) {
        guint numProbesPerRelay = torflowconfig_getNumProbesPerRelay(database->config);
        TorFlowRelay* entry = g_hash_table_lookup(database->relaysByIdentity, entryIdentity);
        TorFlowRelay* exit = g_hash_table_lookup(database->relaysByIdentity, exitIdentity);
        if(entry) {
            torflowrelay_addMeasurement(entry, contentLength, roundTripTime, payloadTime, totalTime);
        }
        if(exit) {
            torflowrelay_addMeasurement(exit, contentLength, roundTripTime, payloadTime, totalTime);
        }
    }
}

static void _torflowdatabase_aggregateResults(TorFlowDatabase* database) {
    g_assert(database);

    // we only use the most recent measurement of a relay, i.e., the numprobes
    // measurements that were done as part of the most recent slice
    // see https://gitweb.torproject.org/torflow.git/tree/NetworkScanners/BwAuthority/README.spec.txt#n285
    guint numProbesPerRelay = torflowconfig_getNumProbesPerRelay(database->config);

    // loop through measured nodes and aggregate stats
    guint totalMeanBW = 0;
    guint totalFilteredBW = 0;
    guint numMeasuredNodes = 0;

    // we compute averages of the entire network so that we can compare each relay's bandwidth values
    // to the rest of the network
    // see https://gitweb.torproject.org/torflow.git/tree/NetworkScanners/BwAuthority/README.spec.txt#n298
    //
    // note that the actual torflow code may compute averages based on only relays in the same class as
    // the target relay, rather than whole-network averages (classes: Guard, Exit, Middle, Guard+Exit)
    // https://gitweb.torproject.org/torflow.git/tree/NetworkScanners/BwAuthority/aggregate.py#n491
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    g_hash_table_iter_init(&iter, database->relaysByIdentity);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        TorFlowRelay* relay = value;
        if(relay && torflowrelay_isMeasureable(relay)) {
            guint relayMeanBW = 0, relayFilteredBW = 0;
            torflowrelay_getBandwidths(relay, numProbesPerRelay, &relayMeanBW, &relayFilteredBW);

            totalMeanBW += relayMeanBW;
            totalFilteredBW += relayFilteredBW;
            numMeasuredNodes++;
        }
    }

    // calculate averages
    gdouble avgMeanBW = 0.0f;
    gdouble avgFilteredBW = 0.0f;
    if(numMeasuredNodes > 0) {
        avgMeanBW = ((gdouble)totalMeanBW)/((gdouble)numMeasuredNodes);
        avgFilteredBW = ((gdouble)totalFilteredBW)/((gdouble)numMeasuredNodes);
    }

    info("database found: numMeasuredNodes=%u, totalMeanBW=%u, avgMeanBW=%f, totalFilteredBW=%u, avgFilteredBW=%f",
            numMeasuredNodes, totalMeanBW, avgMeanBW, totalFilteredBW, avgFilteredBW);

    guint totalBW = 0;

    // loop through nodes and calculate new bandwidths
    g_hash_table_iter_init(&iter, database->relaysByIdentity);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        TorFlowRelay* relay = value;

        if(relay) {
            if(torflowrelay_isMeasureable(relay)) {
                guint relayMeanBW = 0, relayFilteredBW = 0;
                torflowrelay_getBandwidths(relay, numProbesPerRelay, &relayMeanBW, &relayFilteredBW);
                guint advertisedBW = torflowrelay_getAdvertisedBandwidth(relay);

                // use the better of the mean and filtered ratios, because that's what torflow does
                gdouble bwRatio = 0.0f;
                gboolean bwRatioIsSet = FALSE;

                if(avgMeanBW > 0 && avgFilteredBW > 0) {
                    bwRatio = fmax(((gdouble)relayMeanBW)/avgMeanBW, ((gdouble)relayFilteredBW)/avgFilteredBW);
                    bwRatioIsSet = TRUE;
                } else if(avgMeanBW > 0) {
                    bwRatio = ((gdouble)relayMeanBW)/avgMeanBW;
                    bwRatioIsSet = TRUE;
                } else if(avgFilteredBW > 0) {
                    bwRatio = ((gdouble)relayFilteredBW)/avgFilteredBW;
                    bwRatioIsSet = TRUE;
                }

                guint v3BW = (bwRatioIsSet && bwRatio >= 0.0f) ? (guint)(advertisedBW * bwRatio) : advertisedBW;

                info("Computing bandwidth for relay %s (%s), prev_bw=%u, ratioIsSet=%s, ratio=%f, v3bw=%u",
                        torflowrelay_getNickname(relay), torflowrelay_getIdentity(relay),
                        advertisedBW, bwRatioIsSet ? "True" : "False", bwRatio, v3BW);

                totalBW += v3BW;
                torflowrelay_setV3Bandwidth(relay, v3BW);
            } else {
                info("relay %s (%s) is not measurable, using 0 as v3bw value",
                        torflowrelay_getNickname(relay), torflowrelay_getIdentity(relay));
                torflowrelay_setV3Bandwidth(relay, 0);
            }
        }
    }

    // finally, loop through nodes and cap bandwidths that are too large
    gdouble maxWeightFraction = torflowconfig_getMaxRelayWeightFraction(database->config);
    guint maxBandwidth = (guint)(totalBW * maxWeightFraction);
    guint minBandwidth = 20;

    g_hash_table_iter_init(&iter, database->relaysByIdentity);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        TorFlowRelay* relay = value;

        guint v3bw = torflowrelay_getV3Bandwidth(relay);

        if (v3bw < minBandwidth || v3bw > maxBandwidth) {
            const gchar* identity = torflowrelay_getIdentity(relay);
            const gchar* nickname = torflowrelay_getNickname(relay);

            guint bandwidth = (v3bw < minBandwidth) ? minBandwidth : maxBandwidth;

            message("Adjusting bandwidth from %u to %u for extremely %s relay %s (%s)\n",
                    v3bw, bandwidth, (v3bw < minBandwidth) ? "slow" : "fast", nickname, identity);

            v3bw = bandwidth;
            torflowrelay_setV3Bandwidth(relay, v3bw);
        }
    }
}

void torflowdatabase_writeBandwidthFile(TorFlowDatabase* database) {
    g_assert(database);

    // first aggregate the latest results that we have
    _torflowdatabase_aggregateResults(database);

    message("writing new v3bw file now");

    //create new file to print to, and increment version
    const gchar* v3bwFilePath = torflowconfig_getV3BWFilePath(database->config);
    GString* newFilename = g_string_new(NULL);
    g_string_printf(newFilename, "%s.%d", v3bwFilePath, database->v3bwVersion++);

    struct timespec now_ts;
    clock_gettime(CLOCK_REALTIME, &now_ts);

    FILE * fp = fopen(newFilename->str, "w");
    if(fp == NULL) {
        warning("unable to write v3bw file, NULL file stream for file path %s: error %i: %s",
                newFilename->str, errno, g_strerror(errno));
        g_string_free(newFilename, TRUE);
        return;
    }

    fprintf(fp, "%li\n", now_ts.tv_sec);

    /*
     * loop through all relays and print them to file
     * file format is, where first line value is unix timestamp:
     * ```
     * {}\n
     * node_id=${}\tbw={}\tnick={}\n
     * [...]
     * node_id=${}\tbw={}\tnick={}\n
     * ```
     * notice there is a newline on the last line.
     *
     * see https://gitweb.torproject.org/torflow.git/tree/NetworkScanners/BwAuthority/README.spec.txt#n332
     */
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    g_hash_table_iter_init(&iter, database->relaysByIdentity);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        TorFlowRelay* relay = value;

        const gchar* identity = torflowrelay_getIdentity(relay);
        const gchar* nickname = torflowrelay_getNickname(relay);
        guint v3bw = torflowrelay_getV3Bandwidth(relay);

        fprintf(fp, "node_id=$%s\tbw=%u\tnick=%s\n", identity, v3bw, nickname);
    }

    fclose(fp);

    message("wrote new bandwidth file at %s", v3bwFilePath);

    /* update symlink */
    _torflowdatabase_updateAuthoritativeLink(v3bwFilePath, newFilename->str);
    g_string_free(newFilename, TRUE);
}
