/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowProbe {
    /* un-owned objects (we don't free these) */
    TorFlowEventManager* manager;

    OnProbeCompleteFunc onProbeComplete;
    gpointer onProbeCompleteArg;

    /* our objects */
    TorFlowTorCtlClient* torctl;
    in_port_t controlPort;
    TorFlowFileClient* fileClient;
    in_port_t socksPort;

    gchar* entryRelayIdentity;
    gchar* exitRelayIdentity;
    TorFlowPeer* filePeer;
    gsize transferSize;

    gint circuitID;
    gint streamID;
    gchar* targetAddress;
    in_port_t targetPort;
    gchar* sourceAddress;
    in_port_t sourcePort;

    guint workerID;
    gchar* id;
};

static void _torflowprobe_onFileClientComplete(TorFlowProbe* probe, gboolean isSuccess,
        gsize contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
    g_assert(probe);

    /* RTT: round-trip time; TTFB: time to first byte; TTLB: time to last byte */
    info("%s: Probe complete: Success=%s, Bytes=%zu, RTT=%zu, TTFB=%zu, TTLB=%zu",
            probe->id,
            isSuccess ? "true" : "false",
            contentLength, roundTripTime, payloadTime, totalTime);

    /* this could have been an error or success. in either case we are done.
     * forward the result to the authority */
    if(probe->onProbeComplete) {
        probe->onProbeComplete(probe->onProbeCompleteArg, probe->workerID,
                probe->entryRelayIdentity, probe->exitRelayIdentity, isSuccess,
                contentLength, roundTripTime, payloadTime, totalTime);
    }
}

static void _torflowprobe_updateNetInfo(TorFlowProbe* probe,
        gchar* targetAddress, in_port_t targetPort, gchar* sourceAddress, in_port_t sourcePort) {
    g_assert(probe);

    if(targetAddress) {
        if(probe->targetAddress) {
            g_free(probe->targetAddress);
        }
        probe->targetAddress = g_strdup(targetAddress);
    }
    if(targetPort > 0) {
        probe->targetPort = targetPort;
    }
    if(sourceAddress) {
        if(probe->sourceAddress) {
            g_free(probe->sourceAddress);
        }
        probe->sourceAddress = g_strdup(sourceAddress);
    }
    if(sourcePort > 0) {
        probe->sourcePort = sourcePort;
    }
}

static void _torflowprobe_onStreamSucceeded(TorFlowProbe* probe, gint streamID, gint circuitID,
        gchar* sourceAddress, in_port_t sourcePort, gchar* targetAddress, in_port_t targetPort) {
    g_assert(probe);

    if(streamID == probe->streamID && circuitID == probe->circuitID) {

        /* store the latest net info about the stream */
        _torflowprobe_updateNetInfo(probe, targetAddress, targetPort, sourceAddress, sourcePort);

        message("%s: Attached stream %i on circuit %i from source %s:%u successfully connected to target %s:%u",
                probe->id, streamID, circuitID, sourceAddress, sourcePort, targetAddress, targetPort);
    }
}

static void _torflowprobe_onStreamNew(TorFlowProbe* probe, gint streamID,
        gchar* sourceAddress, in_port_t sourcePort, gchar* targetAddress, in_port_t targetPort) {
    g_assert(probe);

    /* only handle the stream if it's one of ours, otherwise the authority will
     * attach other regular streams to circuits. */
    in_port_t clientSocksPort = 0;

    if(probe->fileClient) {
        clientSocksPort = torflowfileclient_getHostClientSocksPort(probe->fileClient);
    }

    message("%s: new stream %i for port %u, probe port is %u", probe->id, streamID, sourcePort, clientSocksPort);

    if(clientSocksPort > 0 && clientSocksPort == sourcePort) {
        message("%s: new stream %i from source %s:%u to target %s:%u", probe->id,
                streamID, sourceAddress, sourcePort, targetAddress, targetPort);

        /* our file client created this stream, store the latest net info */
        _torflowprobe_updateNetInfo(probe, targetAddress, targetPort, sourceAddress, sourcePort);

        /* save the stream ID */
        probe->streamID = streamID;

        /* attach the stream to our already built circuit */
        torflowtorctlclient_commandAttachStreamToCircuit(probe->torctl, probe->streamID, probe->circuitID,
                (OnStreamSucceededFunc)_torflowprobe_onStreamSucceeded, probe);
    } else {
        info("%s: ignoring stream %i, source port %u doesn't match client port %u",
                probe->id, streamID, sourcePort, clientSocksPort);
    }
}

static void _torflowprobe_onCircuitBuilt(TorFlowProbe* probe, gint circuitID) {
    g_assert(probe);

    message("%s: Tor controller successfully built new circuit %i", probe->id, circuitID);

    probe->circuitID = circuitID;

    message("%s: creating socks client to connect to Tor", probe->id);

    /* this will create a socket, connect via socks, and create the stream to start the download. */
    probe->fileClient = torflowfileclient_new(probe->manager, probe->workerID, probe->socksPort,
            probe->filePeer, probe->transferSize,
            (OnFileClientCompleteFunc)_torflowprobe_onFileClientComplete, probe);

    if(probe->fileClient == NULL) {
        warning("%s: can't create file client instance", probe->id);
        _torflowprobe_onFileClientComplete(probe, FALSE, 0, 0, 0, 0);
        return;
    }

    /* get our client port so we can filter stream events */
    in_port_t clientSocksPort = torflowfileclient_getHostClientSocksPort(probe->fileClient);

    message("%s: file client successful, waiting for stream on client port %u", probe->id, clientSocksPort);

    /* start listening for new streams */
    torflowtorctlclient_setNewStreamCallback(probe->torctl, clientSocksPort,
            (OnStreamNewFunc)_torflowprobe_onStreamNew, probe);
}

static void _torflowprobe_onBootstrapped(TorFlowProbe* probe) {
    g_assert(probe);

    message("%s: Tor instance successfully bootstrapped", probe->id);

    /* make sure we will get the response */
    torflowtorctlclient_commandEnableEvents(probe->torctl);

    GString* pathBuffer = g_string_new(NULL);
    g_string_printf(pathBuffer, "%s,%s", probe->entryRelayIdentity, probe->exitRelayIdentity);

    /* build a circuit and set up callbacks when it is built and when streams arrive on it */
    torflowtorctlclient_commandBuildNewCircuit(probe->torctl, pathBuffer->str,
            (OnCircuitBuiltFunc)_torflowprobe_onCircuitBuilt, probe);

    g_string_free(pathBuffer, TRUE);
}

static void _torflowprobe_onAuthenticated(TorFlowProbe* probe) {
    g_assert(probe);

    message("%s: Tor controller successfully authenticated", probe->id);

    torflowtorctlclient_commandGetBootstrapStatus(probe->torctl,
            (OnBootstrappedFunc)_torflowprobe_onBootstrapped, probe);
}

static void _torflowprobe_onConnected(TorFlowProbe* probe) {
    g_assert(probe);

    message("%s: successfully connected to Tor control port", probe->id);

    torflowtorctlclient_commandAuthenticate(probe->torctl,
            (OnAuthenticatedFunc)_torflowprobe_onAuthenticated, probe);
}

TorFlowProbe* torflowprobe_new(TorFlowEventManager* manager, guint workerID,
        in_port_t controlPort, in_port_t socksPort, TorFlowPeer* filePeer, gsize transferSize,
        const gchar* entryRelayIdentity, const gchar* exitRelayIdentity,
        OnProbeCompleteFunc onProbeComplete, gpointer onProbeCompleteArg) {
    g_assert(manager);
    g_assert(filePeer);

    TorFlowProbe* probe = g_new0(TorFlowProbe, 1);

    probe->manager = manager;

    probe->workerID = workerID;
    probe->controlPort = controlPort;
    probe->socksPort = socksPort;

    probe->exitRelayIdentity = g_strdup(exitRelayIdentity);
    probe->entryRelayIdentity = g_strdup(entryRelayIdentity);
    probe->filePeer = filePeer;
    torflowpeer_ref(filePeer);
    probe->transferSize = transferSize;

    probe->onProbeComplete = onProbeComplete;
    probe->onProbeCompleteArg = onProbeCompleteArg;

    /* set our ID string for logging purposes */
    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Worker%u-Probe", workerID);
    probe->id = g_string_free(idbuf, FALSE);

    message("%s: creating control client to connect to Tor", probe->id);

    /* set up our torctl instance to get the descriptors before starting probers */
    probe->torctl = torflowtorctlclient_new(manager, controlPort, workerID,
            (OnConnectedFunc)_torflowprobe_onConnected, probe);

    if(probe->torctl == NULL) {
        warning("%s: can't create Tor controller instance", probe->id);
        torflowprobe_free(probe);
        return NULL;
    }

    return probe;
}

void torflowprobe_free(TorFlowProbe* probe) {
    g_assert(probe);

    if(probe->exitRelayIdentity) {
        g_free(probe->exitRelayIdentity);
    }
    if(probe->entryRelayIdentity) {
        g_free(probe->entryRelayIdentity);
    }

    if(probe->fileClient) {
        torflowfileclient_free(probe->fileClient);
    }

    if(probe->filePeer) {
        torflowpeer_unref(probe->filePeer);
    }

    if(probe->torctl) {
        if(probe->circuitID) {
            torflowtorctlclient_commandCloseCircuit(probe->torctl, probe->circuitID);
        }
        torflowtorctlclient_free(probe->torctl);
    }

    if(probe->id) {
        g_free(probe->id);
    }

    g_free(probe);
}

in_port_t torflowprobe_getHostClientSocksPort(TorFlowProbe* probe) {
    g_assert(probe);
    in_port_t clientSocksPort = 0;
    if(probe->fileClient) {
        clientSocksPort = torflowfileclient_getHostClientSocksPort(probe->fileClient);
    }
    return clientSocksPort;
}

void torflowprobe_onTimeout(TorFlowProbe* probe) {
    g_assert(probe);
    _torflowprobe_onFileClientComplete(probe, FALSE, 0, 0, 0, 0);
}
