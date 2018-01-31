/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowAuthority {
    gchar* id;

    TorFlowConfig* config;
    TorFlowEventManager* manager;
    TorFlowDatabase* database;
    TorFlowTorCtlClient* torctl;
    TorFlowFileListener* listener;

    GQueue* slices;
    GHashTable* probes;
    guint workerIDCounter;
    guint totalProbesThisRound;
    guint completeProbesThisRound;

    gboolean isTorControllerSetup;
};

/* necessary forward declarations */
static void _torflowauthority_launchProbes(TorFlowAuthority* authority);
static void _torflowauthority_getDescriptors(TorFlowAuthority* authority);

static void _torflowauthority_resumeScanning(TorFlowAuthority* authority, gpointer userData) {
    g_assert(authority);

    message("%s: starting new round", authority->id);

    /* get descriptors, and once they are here, parse them and start the next round */
    _torflowauthority_getDescriptors(authority);
}

static void _torflowauthority_scanPauseTimerReadable(TorFlowTimer* timer, TorFlowEventFlag type) {
    g_assert(timer);
    g_assert(type & TORFLOW_EV_READ);

    /* if the timer triggered, this will call _torflowauthority_resumeScanning above */
    gboolean calledNotify = torflowtimer_check(timer);
    if(calledNotify) {
        /* done with timer */
        torflowtimer_free(timer);
    } else {
        warning("Authority unable to resume scanning using pause timer! "
                "Hopefully another read event will trigger it.");
    }
}

static void _torflowauthority_onRoundComplete(TorFlowAuthority* authority) {
    info("round complete after completing %u probes", authority->completeProbesThisRound);

    /* write the new v3bw file */
    torflowdatabase_writeBandwidthFile(authority->database);

    guint seconds = torflowconfig_getScanIntervalSeconds(authority->config);
    if(seconds > 0) {
        /* pause before resuming */
        TorFlowTimer* timer = torflowtimer_new((GFunc)_torflowauthority_resumeScanning, authority, NULL);
        torflowtimer_arm(timer, seconds);
        gint timerFD = torflowtimer_getFD(timer);
        torfloweventmanager_register(authority->manager, timerFD, TORFLOW_EV_READ,
                (TorFlowOnEventFunc)_torflowauthority_scanPauseTimerReadable, timer);
    } else {
        /* no pause needed */
        _torflowauthority_resumeScanning(authority, NULL);
    }
}

static void _torflowauthority_countProbesRemaining(TorFlowSlice* slice, guint* numProbesRemaining) {
    g_assert(slice);
    g_assert(numProbesRemaining);
    *numProbesRemaining += torflowslice_getNumProbesRemaining(slice);
    torflowslice_logStatus(slice);
}

static void _torflowauthority_logProgress(TorFlowAuthority* authority) {
    g_assert(authority);

    if(authority->totalProbesThisRound == 0) {
        return;
    }

    guint inProgress = 0;
    guint remaining = 0;

    if(authority->probes) {
        inProgress = g_hash_table_size(authority->probes);
    }
    if(authority->slices) {
        g_queue_foreach(authority->slices, (GFunc)_torflowauthority_countProbesRemaining, &remaining);
    }

    guint progressComplete = remaining >= authority->totalProbesThisRound ? 0 : authority->totalProbesThisRound-remaining;
    gdouble percentage = (gdouble)progressComplete / (gdouble)authority->totalProbesThisRound;
    percentage *= 100.0f;

    message("%s: %u probes in progress, %u probes complete, %u probes remaining, round progress %u/%u (%.02f\%)",
            authority->id, inProgress, authority->completeProbesThisRound, remaining,
            progressComplete, authority->totalProbesThisRound, percentage);
}

static void _torflowauthority_onProbeComplete(TorFlowAuthority* authority, guint probeID,
        gchar* entryIdentity, gchar* exitIdentity, gboolean isSuccess,
        gsize contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
    g_assert(authority);

    message("%s: probe complete: path=%s,%s success=%s, size=%zu, RTT=%zu, TTFB=%zu, TTLB=%zu",
            authority->id,
            entryIdentity, exitIdentity, isSuccess ? "true" : "false",
            contentLength, roundTripTime, payloadTime, totalTime);

    authority->completeProbesThisRound++;

    /* store the measurement result */
    torflowdatabase_storeMeasurementResult(authority->database, entryIdentity, exitIdentity,
            isSuccess, contentLength, roundTripTime, payloadTime, totalTime);

    /* we are done with the probe, this will free the probe */
    g_hash_table_remove(authority->probes, GUINT_TO_POINTER(probeID));

    // TODO after the first round, once we have measurements for all relays,
    // then we can write new v3bw file after every slice to speed up bw file creation

    /* if we still have slices, start some probes on their relays */
    if(!g_queue_is_empty(authority->slices)) {
        /* we still need more measurements. note that the remaining slices may fail if
         * they have no exits, which would mean the round is over. */
        _torflowauthority_launchProbes(authority);
    }

    /* check if we need more probes or if the round is done */
    gboolean startNextRound = FALSE;
    if(g_queue_is_empty(authority->slices)) {
        /* all slices are done, are we waiting for any more probes? */
        guint numProbes = g_hash_table_size(authority->probes);
        if(numProbes > 0) {
            /* wait for the final probes to finish */
            info("%s: all slices done, waiting for final %u probes to finish", authority->id, numProbes);
        } else {
            /* all probes are done */
            startNextRound = TRUE;
        }
    }

    _torflowauthority_logProgress(authority);

    if(startNextRound) {
        _torflowauthority_onRoundComplete(authority);
    }
}

static void _torflowauthority_checkProbe(TorFlowAuthority* authority, gpointer probeID) {
    g_assert(authority);

    /* the probe would have timed out if it still exists, otherwise it completed successfully */
    TorFlowProbe* probe = g_hash_table_lookup(authority->probes, probeID);
    if(probe != NULL) {
        info("%s: probe %u timed out, canceling now", authority->id, GPOINTER_TO_UINT(probeID));

        /* this will cause a call to _torflowauthority_onProbeComplete to delete the probe */
        torflowprobe_onTimeout(probe);
    }
}

static void _torflowauthority_probeTimerReadable(TorFlowTimer* timer, TorFlowEventFlag type) {
    g_assert(timer);
    g_assert(type & TORFLOW_EV_READ);

    /* if the timer triggered, this will call _torflowauthority_resumeScanning above */
    gboolean calledNotify = torflowtimer_check(timer);
    if(calledNotify) {
        /* done with timer */
        torflowtimer_free(timer);
    } else {
        warning("Authority unable to check probe timeout using pause timer! "
                "Hopefully another read event will trigger it.");
    }
}

static void _torflowauthority_launchProbes(TorFlowAuthority* authority) {
    g_assert(authority);

    /* start measuring relays with probes */
    guint numParallelProbes = torflowconfig_getNumParallelProbes(authority->config);
    guint probeTimeoutSeconds = torflowconfig_getProbeTimeoutSeconds(authority->config);

    in_port_t controlPort = torflowconfig_getTorControlPort(authority->config);
    in_port_t socksPort = torflowconfig_getTorSocksPort(authority->config);

    while(g_hash_table_size(authority->probes) < numParallelProbes && !g_queue_is_empty(authority->slices)) {
        TorFlowSlice* slice = g_queue_pop_head(authority->slices);

        gchar* entryRelayIdentity = NULL;
        gchar* exitRelayIdentity = NULL;
        gboolean found = torflowslice_chooseRelayPair(slice, &entryRelayIdentity, &exitRelayIdentity);

        if(found && entryRelayIdentity != NULL && exitRelayIdentity != NULL) {
            /* measure the relays */
            guint probeID = authority->workerIDCounter++;
            TorFlowPeer* filePeer = torflowconfig_cycleFileServerPeers(authority->config);
            gsize transferSize = torflowslice_getTransferSize(slice);

            TorFlowProbe* probe = torflowprobe_new(authority->manager, probeID,
                    controlPort, socksPort, filePeer, transferSize,
                    entryRelayIdentity, exitRelayIdentity,
                    (OnProbeCompleteFunc)_torflowauthority_onProbeComplete, authority);

            if(probe != NULL) {
                g_hash_table_replace(authority->probes, GUINT_TO_POINTER(probeID), probe);

                if(probeTimeoutSeconds > 0) {
                    /* check on the probe after a timeout */
                    TorFlowTimer* timer = torflowtimer_new((GFunc)_torflowauthority_checkProbe, authority, GUINT_TO_POINTER(probeID));
                    torflowtimer_arm(timer, probeTimeoutSeconds);
                    gint timerFD = torflowtimer_getFD(timer);
                    torfloweventmanager_register(authority->manager, timerFD, TORFLOW_EV_READ,
                            (TorFlowOnEventFunc)_torflowauthority_probeTimerReadable, timer);
                }
            } else {
                warning("%s: error creating probe %u; ignoring", authority->id, probeID);
            }

            /* we still need to probe relays in this slice */
            g_queue_push_tail(authority->slices, slice);
        } else {
            /* no longer need to measure any more relays.
             * either they had no exits or entries, or we are done measuring all relays */
            torflowslice_free(slice);
        }
    }
}

static GQueue* _torflowauthority_sliceRelays(TorFlowAuthority* authority) {
    g_assert(authority);

    /* get relays sorted by decreasing bandwidth */
    GQueue* relaysToMeasure = torflowdatabase_getMeasureableRelays(authority->database);
    guint totalMeasurableRelays = g_queue_get_length(relaysToMeasure);

    message("%s: we have %u measurable relays", authority->id, totalMeasurableRelays);

    /* break relays into slices */
    GQueue* slices = g_queue_new();
    TorFlowSlice* slice = NULL;

    guint numProbesPerRelay = torflowconfig_getNumProbesPerRelay(authority->config);

    while(!g_queue_is_empty(relaysToMeasure)) {

        if(!slice) {
            guint sliceID = g_queue_get_length(slices);
            gdouble percentile = 1.0f - ((gdouble)g_queue_get_length(relaysToMeasure)/(gdouble)totalMeasurableRelays);
            slice = torflowslice_new(sliceID, percentile, numProbesPerRelay);
            g_queue_push_tail(slices, slice);
        }

        torflowslice_addRelay(slice, g_queue_pop_head(relaysToMeasure));

        if(torflowslice_getLength(slice) >= torflowconfig_getNumRelaysPerSlice(authority->config)) {
            /* log slice info */
            torflowslice_logStatus(slice);
            /* set to NULL so we create a new one next time */
            slice = NULL;
        }
    }

    /* the last slice may not be full, but log info anyway */
    if(slice) {
        torflowslice_logStatus(slice);
    }

    return slices;
}

static void _torflowauthority_startNewScanningRound(TorFlowAuthority* authority) {
    g_assert(authority);

    /* break relays into 'slices' for measurement */
    if(authority->slices) {
        g_queue_free_full(authority->slices, (GDestroyNotify) torflowslice_free);
    }
    authority->slices = _torflowauthority_sliceRelays(authority);

    /* count the total probes needed this round */
    authority->totalProbesThisRound = 0;
    if(authority->slices) {
        g_queue_foreach(authority->slices, (GFunc)_torflowauthority_countProbesRemaining, &authority->totalProbesThisRound);
    }
    authority->completeProbesThisRound = 0;

    /* clear out previous probes that might be hanging around */
    if(authority->probes) {
        g_hash_table_destroy(authority->probes);
    }
    authority->probes = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)torflowprobe_free);

    /* start probing relays in the slices */
    _torflowauthority_launchProbes(authority);
}

static void _torflowauthority_onNewStream(TorFlowAuthority* authority, gint streamID,
        gchar* sourceAddress, in_port_t sourcePort, gchar* targetAddress, in_port_t targetPort) {
    g_assert(authority);

    debug("%s: new stream %i for port %u", authority->id, streamID, sourcePort);

    /* if none of our probes created this stream, then we need to let Tor attach it */
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, authority->probes);

    /* check if this is a probe stream */
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        guint probeID = GPOINTER_TO_UINT(key);
        TorFlowProbe* probe = value;

        if(probe) {
            in_port_t probePort = torflowprobe_getHostClientSocksPort(probe);
            if(probePort > 0 && sourcePort == probePort) {
                info("%s: ignoring stream %i, source port %u matches client port %u and should be handled by client",
                        authority->id, streamID, sourcePort, probePort);
                return;
            }
        }
    }

    /* the port did not match any client ports.
     * let Tor attach the stream, and don't notify us when attached */
    info("%s: letting Tor attach stream %i to any circuit", authority->id, streamID);
    torflowtorctlclient_commandAttachStreamToCircuit(authority->torctl, streamID, 0, NULL, NULL);
}

static void _torflowauthority_setupTorController(TorFlowAuthority* authority) {
    g_assert(authority);

    /* set the config for Tor so streams stay unattached */
    torflowtorctlclient_commandSetupTorConfig(authority->torctl);

    /* we need to attach all streams that the probes do not create */
    torflowtorctlclient_setNewStreamCallback(authority->torctl, 0,
            (OnStreamNewFunc)_torflowauthority_onNewStream, authority);

    /* start watching for stream events */
    torflowtorctlclient_commandEnableEvents(authority->torctl);
}

static void _torflowauthority_onDescriptorsReceived(TorFlowAuthority* authority, GQueue* descriptorLines) {
    g_assert(authority);

    message("%s: received Tor relay descriptors", authority->id);

    /* now parse the descriptors */
    message("%s: storing relays from descriptors", authority->id);
    guint numRelays = torflowdatabase_storeNewDescriptors(authority->database, descriptorLines);
    message("%s: finished parsing descriptors, found %u relays", authority->id, numRelays);

    if(!authority->isTorControllerSetup) {
        _torflowauthority_setupTorController(authority);
        authority->isTorControllerSetup = TRUE;
    }

    _torflowauthority_startNewScanningRound(authority);
}

static void _torflowauthority_getDescriptors(TorFlowAuthority* authority) {
    g_assert(authority);
    message("%s: requesting current relay descriptors", authority->id);
    torflowtorctlclient_commandGetDescriptorInfo(authority->torctl,
            (OnDescriptorsReceivedFunc)_torflowauthority_onDescriptorsReceived, authority);
}

static void _torflowauthority_onBootstrapped(TorFlowAuthority* authority) {
    g_assert(authority);
    message("%s: Tor instance successfully bootstrapped", authority->id);
    _torflowauthority_getDescriptors(authority);
}

static void _torflowauthority_onAuthenticated(TorFlowAuthority* authority) {
    g_assert(authority);

    message("%s: Tor controller successfully authenticated", authority->id);

    torflowtorctlclient_commandGetBootstrapStatus(authority->torctl,
            (OnBootstrappedFunc)_torflowauthority_onBootstrapped, authority);
}

static void _torflowauthority_onConnected(TorFlowAuthority* authority) {
    g_assert(authority);

    message("%s: successfully connected to Tor control port", authority->id);

    torflowtorctlclient_commandAuthenticate(authority->torctl,
            (OnAuthenticatedFunc)_torflowauthority_onAuthenticated, authority);
}

TorFlowAuthority* torflowauthority_new(TorFlowConfig* config, TorFlowEventManager* manager) {
    TorFlowAuthority* authority = g_new0(TorFlowAuthority, 1);

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Worker0-Authority");
    authority->id = g_string_free(idbuf, FALSE);

    authority->manager = manager;
    authority->config = config;

    message("%s: creating relay database", authority->id);

    /* database to store relay information and measurements */
    authority->database = torflowdatabase_new(config);

    if(authority->database == NULL) {
        message("%s: error creating relay database instance", authority->id);
        torflowauthority_free(authority);
        return NULL;
    }

    message("%s: creating control client to connect to Tor", authority->id);

    /* set up our torctl instance to get the descriptors before starting probers */
    in_port_t controlPort = torflowconfig_getTorControlPort(authority->config);
    authority->torctl = torflowtorctlclient_new(manager, controlPort, authority->workerIDCounter++,
            (OnConnectedFunc)_torflowauthority_onConnected, authority);

    if(authority->torctl == NULL) {
        message("%s: error creating tor controller instance", authority->id);
        torflowauthority_free(authority);
        return NULL;
    }

    message("%s: creating file server listener", authority->id);

    /* set up the file listener that will accept probe connections */
    in_port_t listenerPort = torflowconfig_getListenerPort(authority->config);
    authority->listener = torflowfilelistener_new(manager, 0, listenerPort);

    if(authority->listener == NULL) {
        message("%s: error creating file server listener instance", authority->id);
        torflowauthority_free(authority);
        return NULL;
    }

    return authority;
}

void torflowauthority_free(TorFlowAuthority* authority) {
    g_assert(authority);

    if(authority->probes) {
        g_hash_table_destroy(authority->probes);
    }
    if(authority->slices) {
        g_queue_free_full(authority->slices, (GDestroyNotify) torflowslice_free);
    }
    if(authority->torctl) {
        torflowtorctlclient_free(authority->torctl);
    }
    if(authority->listener) {
        torflowfilelistener_free(authority->listener);
    }
    if(authority->database) {
        torflowdatabase_free(authority->database);
    }

    if(authority->id) {
        g_free(authority->id);
    }

    g_free(authority);
}
