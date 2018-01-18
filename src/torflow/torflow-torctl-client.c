/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

typedef enum {
    CTL_NONE, CTL_AUTHENTICATE, CTL_BOOTSTRAP, CTL_PROCESSING
} TorFlowControlState;

struct _TorFlowTorCtlClient {
    TorFlowEventManager* manager;

    /* controlling the tor client */
    gint descriptor;
    TorFlowControlState state;
    GQueue* commands;

    /* flags */
    gboolean waitingMeasurementCircuit;
    gint cachedCircId;
    gchar* cachedCircPath;
    gboolean isStatusEventSet;

    gboolean waitingGetDescriptorsResponse;
    GQueue* descriptorLines;

    gboolean waitingAttachStreamResponse;

    GString* receiveLineBuffer;

    in_port_t streamFilterPort;

    OnConnectedFunc onConnected;
    gpointer onConnectedArg;
    OnAuthenticatedFunc onAuthenticated;
    gpointer onAuthenticatedArg;
    OnBootstrappedFunc onBootstrapped;
    gpointer onBootstrappedArg;
    OnDescriptorsReceivedFunc onDescriptorsReceived;
    gpointer onDescriptorsReceivedArg;
    OnCircuitBuiltFunc onCircuitBuilt;
    gpointer onCircuitBuiltArg;
    OnStreamNewFunc onStreamNew;
    gpointer onStreamNewArg;
    OnStreamSucceededFunc onStreamSucceeded;
    gpointer onStreamSucceededArg;

    gchar* id;
};

static gint _torflowtorctlclient_parseCode(gchar* line) {
    gchar** parts1 = g_strsplit(line, " ", 0);
    gchar** parts2 = g_strsplit_set(parts1[0], "-+", 0);
    gint code = atoi(parts2[0]);
    g_strfreev(parts1);
    g_strfreev(parts2);
    return code;
}

static gint _torflowtorctlclient_parseBootstrapProgress(gchar* line) {
    gint progress = -1;
    gchar** parts = g_strsplit(line, " ", 0);
    gchar* part = NULL;
    gboolean foundBootstrap = FALSE;
    for(gint j = 0; (part = parts[j]) != NULL; j++) {
        gchar** subparts = g_strsplit(part, "=", 0);
        if(!g_ascii_strncasecmp(subparts[0], "BOOTSTRAP", 9)) {
            foundBootstrap = TRUE;
        } else if(foundBootstrap && !g_ascii_strncasecmp(subparts[0], "PROGRESS", 8)) {
            progress = atoi(subparts[1]);
        }
        g_strfreev(subparts);
    }
    g_strfreev(parts);
    return progress;
}

static void _torflowtorctlclient_processDescriptorLine(TorFlowTorCtlClient* torctl, GString* linebuf) {
    /* handle descriptor info */
    if(!torctl->descriptorLines && g_strstr_len(linebuf->str, linebuf->len, "250+ns/all=")) {
        info("%s: 'GETINFO ns/all\\r\\n' command successful, descriptor response coming next", torctl->id);
        torctl->descriptorLines = g_queue_new();
    }

    if(torctl->descriptorLines) {
        /* descriptors coming */
        if(g_strstr_len(linebuf->str, linebuf->len, "250+ns/all=")) {
            /* header */
            info("%s: got descriptor response header '%s'", torctl->id, linebuf->str);
        } else if(linebuf->str[0] == '.') {
            /* footer */
            info("%s: got descriptor response footer '%s'", torctl->id, linebuf->str);
        } else if(g_strstr_len(linebuf->str, linebuf->len, "250 OK")) {
            /* all done with descriptors */
            info("%s: got descriptor response success code '%s'", torctl->id, linebuf->str);

            torctl->waitingGetDescriptorsResponse = FALSE;

            if(torctl->onDescriptorsReceived) {
                torctl->onDescriptorsReceived(torctl->onDescriptorsReceivedArg, torctl->descriptorLines);
            }

            /* clean up any leftover lines */
            while(g_queue_get_length(torctl->descriptorLines) > 0) {
                g_free(g_queue_pop_head(torctl->descriptorLines));
            }
            g_queue_free(torctl->descriptorLines);
            torctl->descriptorLines = NULL;
        } else {
            /* real descriptor lines */
            g_queue_push_tail(torctl->descriptorLines, g_strdup(linebuf->str));
        }
    }
}

static void _torflowtorctlclient_processLineSync(TorFlowTorCtlClient* torctl, GString* linebuf) {
    /*
     * '250 OK' msgs are fine. the only thing we want to extract info from
     * is when we are building a new measurement circuit
     * or when we are getting descriptor information and need to stop.
     */
    if(g_strstr_len(linebuf->str, linebuf->len, "250 EXTENDED ")) {
        /* are we waiting for a circuit we built? */
        if(torctl->waitingMeasurementCircuit && !torctl->cachedCircId) {
            /* response to EXTENDCIRCUIT:
             * '250 EXTENDED circid'
             */
            gchar** parts = g_strsplit(linebuf->str, " ", 0);
            torctl->cachedCircId = atoi(parts[2]);
            g_strfreev(parts);

            if(torctl->cachedCircId <= 0) {
                warning("%s: measurement circuit build failure '%s'", torctl->id, torctl->cachedCircPath);
            } else {
                info("%s: started building measurement circuit '%i' with path '%s'", torctl->id,
                        torctl->cachedCircId, torctl->cachedCircPath);
            }
        }
    } else if (torctl->waitingGetDescriptorsResponse) {
        _torflowtorctlclient_processDescriptorLine(torctl, linebuf);
    } else if(torctl->waitingAttachStreamResponse) {
        /* got response for attach stream */
        torctl->waitingAttachStreamResponse = FALSE;

        gint code = _torflowtorctlclient_parseCode(linebuf->str);
        gboolean isSuccess = (code == 250) ? TRUE : FALSE;

        if(isSuccess) {
            info("%s: stream was successfully attached to circuit", torctl->id);
        } else {
            /* failure attaching stream */
            warning("%s: error %i from Tor when trying to attach stream", torctl->id, code);
        }
    } else {
        info("%s: ignoring synchronous response '%s'", torctl->id, linebuf->str);
    }
}

static void _torflowtorctlclient_processCircuitLine(TorFlowTorCtlClient* torctl, const gchar* line) {
    /* responses:
     *   650 CIRC 21 BUILT ...
     *   650 CIRC 3 CLOSED ...
     */
    gchar** parts = g_strsplit(line, " ", 0);
    gint circuitID = atoi(parts[2]);

    /* we only care about our circuit here */
    if(circuitID == torctl->cachedCircId) {
        /* check the status */
        if(g_strstr_len(parts[3], 5, "BUILT")) {
            /* a circuit was built, is it one we tried to build? */
            if(torctl->waitingMeasurementCircuit) {
                /* this was the circuit we built */
                info("%s: successfully built new requested circuit '%i' with path '%s'",
                        torctl->id, circuitID, torctl->cachedCircPath);

                /* we are no longer waiting for a circuit */
                torctl->waitingMeasurementCircuit = FALSE;

                if(torctl->onCircuitBuilt) {
                    torctl->onCircuitBuilt(torctl->onCircuitBuiltArg, circuitID);
                }
            } else {
                info("%s: built circuit '%i' after circuit was already built??", torctl->id, circuitID);
            }
        } else if(g_strstr_len(parts[3], 6, "CLOSED")) {
            /* did it close prematurely? */
            if(torctl->waitingMeasurementCircuit) {
                /* CLOSED came before we got a BUILT */
                torctl->waitingMeasurementCircuit = FALSE;

                info("%s: requested circuit '%i' with path '%s' was closed before it finished building",
                        torctl->id, circuitID, torctl->cachedCircPath);
            } else {
                /* it CLOSED after it was BUILT */
                info("%s: requested circuit '%i' with path '%s' was closed after it was built",
                        torctl->id, circuitID, torctl->cachedCircPath);
            }
        } else if(g_strstr_len(parts[3], 6, "FAILED")) {
            /* did it close prematurely? */
            if(torctl->waitingMeasurementCircuit) {
                /* FAILED came before we got a BUILT */
                torctl->waitingMeasurementCircuit = FALSE;

                info("%s: requested circuit '%i' with path '%s' failed before it finished building",
                        torctl->id, circuitID, torctl->cachedCircPath);
            } else {
                /* it FAILED after it was BUILT */
                if(g_strstr_len(parts[7], 14, "REASON=TIMEOUT")) {
                    /* failed because of a timeout */
                    message("%s: requested circuit '%i' with path '%s' has timed out after it was built",
                            torctl->id, circuitID, torctl->cachedCircPath);

//                if(tfb->eventHandlers.onFileServerTimeout) {
//                    tfb->eventHandlers.onFileServerTimeout(tfb);
//                }
                } else {
                    /* failed for another reason */
                    message("%s: requested circuit '%i' with path '%s' has failed after it was built for a reason that we didn't parse",
                            torctl->id, circuitID, torctl->cachedCircPath);
                }
            }
        } else {
            info("%s: ignoring status on requested circuit '%i' with path '%s'",
                    torctl->id, circuitID, torctl->cachedCircPath);
        }
    } else {
        info("%s: ignoring event on unrequested circuit '%i'", torctl->id, circuitID);
    }

    g_strfreev(parts);
}

static void _torflowtorctlclient_processStreamLine(TorFlowTorCtlClient* torctl, const gchar* line) {
    /* responses:
     *   650 STREAM 21 NEW 0 52.1.0.0:80 ...
     *   650 STREAM 21 SUCCEEDED 22 52.1.0.0:80
     *   650 STREAM 18 CLOSED 5 53.1.0.0:9111 ...
     */
    gchar** parts = g_strsplit(line, " ", 0);

    gint streamID = atoi(parts[2]);
    gint circuitID = atoi(parts[4]);

    gchar* targetAddress = NULL;
    in_port_t targetPort = 0;
    {
        gchar** targetParts = g_strsplit(parts[5], ":", 0);

        if(targetParts[0] && targetParts[1]) {
            targetAddress = g_strdup(targetParts[0]);
            targetPort = (in_port_t)atoi(targetParts[1]);
        }

        g_strfreev(targetParts);
    }

    gchar* sourceAddress = NULL;
    in_port_t sourcePort = 0;
    {
        for(gint i = 6; parts[i]; i++) {
            if(!g_ascii_strncasecmp(parts[i], "SOURCE_ADDR=", 12)) {
                gchar** sourceParts = g_strsplit(&parts[i][12], ":", 0);

                if(sourceParts[0] && sourceParts[1]) {
                    sourceAddress = g_strdup(sourceParts[0]);
                    sourcePort = (in_port_t)atoi(sourceParts[1]);
                }

                g_strfreev(sourceParts);
            }
        }
    }

    if(g_strstr_len(parts[3], 3, "NEW")) {
        info("%s: new stream %i with source %s:%u and target %s:%u", torctl->id, streamID,
                sourceAddress, sourcePort, targetAddress, targetPort);

        if(torctl->streamFilterPort == 0 || torctl->streamFilterPort == sourcePort) {
            if(torctl->onStreamNew) {
                torctl->onStreamNew(torctl->onStreamNewArg, streamID,
                        sourceAddress, sourcePort, targetAddress, targetPort);
            }
        } else {
            info("%s: new stream %i with source %s:%u and target %s:%u was filtered", torctl->id, streamID,
                    sourceAddress, sourcePort, targetAddress, targetPort);
        }

    } else {
        if(circuitID == torctl->cachedCircId) {
            if(g_strstr_len(parts[3], 9, "SUCCEEDED")) {
                info("%s: attached stream %i on circuit %i with source %s:%u has succeeded connecting to target %s:%u",
                        torctl->id, streamID, circuitID,
                        sourceAddress, sourcePort, targetAddress, targetPort);

                if(torctl->onStreamSucceeded && circuitID == torctl->cachedCircId) {
                    torctl->onStreamSucceeded(torctl->onStreamSucceededArg, streamID, circuitID,
                            sourceAddress, sourcePort, targetAddress, targetPort);
                }
            } else if(g_strstr_len(parts[3], 6, "CLOSED")) {
                info("%s: closed stream %i on circuit %i with source %s:%u and target %s:%u",
                        torctl->id, streamID, circuitID,
                        sourceAddress, sourcePort, targetAddress, targetPort);
            } else {
                info("%s: got unhandled event for stream %i on circuit %i "
                        "with source %s:%u and target %s:%u",
                        torctl->id, streamID, circuitID,
                        sourceAddress, sourcePort, targetAddress, targetPort);
            }
        } else {
            info("%s: ignoring stream %i on unrequested circuit %i "
                    "with source %s:%u and target %s:%u",
                    torctl->id, streamID, circuitID,
                    sourceAddress, sourcePort, targetAddress, targetPort);
        }
    }

    if(targetAddress) {
        g_free(targetAddress);
    }
    if(sourceAddress) {
        g_free(sourceAddress);
    }

    g_strfreev(parts);
}

static void _torflowtorctlclient_processLineASync(TorFlowTorCtlClient* torctl, GString* linebuf) {
    gboolean isConsumed = FALSE;

    /* ignore internal .exit circuits */
    if(g_strstr_len(linebuf->str, linebuf->len, ".exit")) {
        info("%s: ignoring tor-internal response '%s'", torctl->id, linebuf->str);
        return;
    }

    if(g_strstr_len(linebuf->str, linebuf->len, " CIRC ")) {
        _torflowtorctlclient_processCircuitLine(torctl, linebuf->str);
    } else if(g_strstr_len(linebuf->str, linebuf->len, " STREAM ")) {
        _torflowtorctlclient_processStreamLine(torctl, linebuf->str);
    } else {
        info("%s: ignoring asynchronous response '%s'", torctl->id, linebuf->str);
    }
}

static void _torflowtorctlclient_processLine(TorFlowTorCtlClient* torctl, GString* linebuf) {
    switch(torctl->state) {

        case CTL_AUTHENTICATE: {
            gint code = _torflowtorctlclient_parseCode(linebuf->str);
            if(code == 250) {
                info("%s: successfully received auth response '%s'", torctl->id, linebuf->str);

                if(torctl->onAuthenticated) {
                    torctl->onAuthenticated(torctl->onAuthenticatedArg);
                }
            } else {
                critical("%s: received failed auth response '%s'", torctl->id, linebuf->str);
            }
            break;
        }

        case CTL_BOOTSTRAP: {
            /* we will be getting all client status events, not all of them have bootstrap status */
            gint progress = _torflowtorctlclient_parseBootstrapProgress(linebuf->str);
            if(progress >= 0) {
                debug("%s: successfully received bootstrap phase response '%s'", torctl->id, linebuf->str);
                if(progress >= 100) {
                    message("%s: torflow client is now ready (Bootstrapped 100)", torctl->id);

                    torctl->isStatusEventSet = FALSE;
                    torctl->state = CTL_PROCESSING;

                    if(torctl->onBootstrapped) {
                        torctl->onBootstrapped(torctl->onBootstrappedArg);
                    }
                } else if(!(torctl->isStatusEventSet)) {
                    /* not yet at 100%, register the async status event to wait for it */
                    g_queue_push_tail(torctl->commands, g_string_new("SETEVENTS EXTENDED STATUS_CLIENT\r\n"));
                    torctl->isStatusEventSet = TRUE;
                }
            }
            break;
        }

        case CTL_PROCESSING: {
            gint code = _torflowtorctlclient_parseCode(linebuf->str);

            if(code == 650) {/* asynchronous responses */
                _torflowtorctlclient_processLineASync(torctl, linebuf);
            } else {/* synchronous responses */
                _torflowtorctlclient_processLineSync(torctl, linebuf);
            }
            break;
        }

        case CTL_NONE:
        default:
            /* this should never happen */
            g_assert(FALSE);
            break;
    }
}

static void _torflowtorctlclient_receiveLines(TorFlowTorCtlClient* torctl, TorFlowEventFlag eventType) {
    g_assert(torctl);

    if(eventType & TORFLOW_EV_READ) {
        debug("%s: descriptor %i is readable", torctl->id, torctl->descriptor);

        gchar recvbuf[10240];
        memset(recvbuf, 0, 10240);
        gssize bytes = 0;

        while((bytes = recv(torctl->descriptor, recvbuf, 10000, 0)) > 0) {
            recvbuf[bytes] = '\0';
            debug("%s: recvbuf:%s", torctl->id, recvbuf);

            gboolean isLastLineIncomplete = FALSE;
            if(bytes < 2 || recvbuf[bytes-2] != '\r' || recvbuf[bytes-1] != '\n') {
                isLastLineIncomplete = TRUE;
            }

            //Check for corner case where first element is \r\n
            gboolean isStartCRLF = FALSE;
            if(recvbuf[0] == '\r' && recvbuf[1] == '\n') {
                isStartCRLF = TRUE;
            }

            gchar** lines = g_strsplit(recvbuf, "\r\n", 0);
            gchar* line = NULL;
            for(gint i = 0; (line = lines[i]) != NULL; i++) {
                if(!torctl->receiveLineBuffer) {
                    torctl->receiveLineBuffer = g_string_new(line);
                } else if (isStartCRLF && i == 0 &&
                        !g_ascii_strcasecmp(line, "")) {
                    /* do nothing; we want to process the line in buffer already */
                } else {
                    g_string_append_printf(torctl->receiveLineBuffer, "%s", line);
                }

                if(!(isStartCRLF && i == 0) && (!g_ascii_strcasecmp(line, "") ||
                        (isLastLineIncomplete && lines[i+1] == NULL))) {
                    /* this is '', or the last line, and its not all here yet */
                    continue;
                } else {
                    /* we have a full line in our buffer */
                    info("%s: received '%s'", torctl->id, torctl->receiveLineBuffer->str);

                    _torflowtorctlclient_processLine(torctl, torctl->receiveLineBuffer);

                    g_string_free(torctl->receiveLineBuffer, TRUE);
                    torctl->receiveLineBuffer = NULL;
                }
            }
            g_strfreev(lines);
        }
    }
}

static void _torflowtorctlclient_flushCommands(TorFlowTorCtlClient* torctl, TorFlowEventFlag eventType) {
    g_assert(torctl);

    torfloweventmanager_deregister(torctl->manager, torctl->descriptor);

    /* send all queued commands */
    if(eventType & TORFLOW_EV_WRITE) {
        debug("%s: descriptor %i is writable", torctl->id, torctl->descriptor);

        while(!g_queue_is_empty(torctl->commands)) {
            GString* command = g_queue_pop_head(torctl->commands);

            gssize bytes = send(torctl->descriptor, command->str, command->len, 0);

            if(bytes > 0) {
                /* at least some parts of the command were sent successfully */
                GString* sent = g_string_new(command->str);
                sent = g_string_truncate(sent, bytes);
                info("%s: sent '%s'", torctl->id, g_strchomp(sent->str));
                g_string_free(sent, TRUE);
            }

            if(bytes == command->len) {
                g_string_free(command, TRUE);
            } else {
                /* partial or no send */
                command = g_string_erase(command, (gssize)0, (gssize)bytes);
                g_queue_push_head(torctl->commands, command);
                break;
            }
        }
    }

    gboolean success = TRUE;

    if(g_queue_is_empty(torctl->commands)) {
        /* we wrote all of the commands, go back into reading mode */
        success = torfloweventmanager_register(torctl->manager, torctl->descriptor, TORFLOW_EV_READ,
                (TorFlowOnEventFunc)_torflowtorctlclient_receiveLines, torctl);
    } else {
        /* we still want to write more */
        success = torfloweventmanager_register(torctl->manager, torctl->descriptor, TORFLOW_EV_WRITE,
                (TorFlowOnEventFunc)_torflowtorctlclient_flushCommands, torctl);
    }

    if(!success) {
        warning("%s: Unable to register descriptor %i with event manager", torctl->id, torctl->descriptor);
    }

}

static void _torflowtorctlclient_onConnected(TorFlowTorCtlClient* torctl, TorFlowEventFlag type) {
    g_assert(torctl);
    torfloweventmanager_deregister(torctl->manager, torctl->descriptor);
    if(torctl->onConnected) {
        torctl->onConnected(torctl->onConnectedArg);
    }
}

TorFlowTorCtlClient* torflowtorctlclient_new(TorFlowEventManager* manager, in_port_t controlPort, guint workerID,
        OnConnectedFunc onConnected, gpointer onConnectedArg) {
    TorFlowTorCtlClient* torctl = g_new0(TorFlowTorCtlClient, 1);

    torctl->manager = manager;
    torctl->commands = g_queue_new();

    /* set our ID string for logging purposes */
    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Worker%u-Controller", workerID);
    torctl->id = g_string_free(idbuf, FALSE);

    /* create the control socket */
    torctl->descriptor = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);

    /* check for error */
    if(torctl->descriptor < 0) {
        critical("%s: error %i in socket(): %s", torctl->id, errno, g_strerror(errno));
        torflowtorctlclient_free(torctl);
        return NULL;
    }

    /* connect to the control port */
    struct sockaddr_in serverAddress;
    memset(&serverAddress, 0, sizeof(serverAddress));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);;
    serverAddress.sin_port = controlPort;

    /* connect to server. since we are non-blocking, we expect this to return EINPROGRESS */
    gint res = connect(torctl->descriptor, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
    if (res < 0 && errno != EINPROGRESS) {
        critical("%s: error %i in connect(): %s", torctl->id, errno, g_strerror(errno));
        torflowtorctlclient_free(torctl);
        return NULL;
    }

    /* get notified when the connection succeeds */
    torctl->onConnected = onConnected;
    torctl->onConnectedArg = onConnectedArg;
    gboolean success = torfloweventmanager_register(torctl->manager, torctl->descriptor, TORFLOW_EV_WRITE,
            (TorFlowOnEventFunc)_torflowtorctlclient_onConnected, torctl);

    if(!success) {
        critical("%s: unable to register descriptor %i with event manager", torctl->id, torctl->descriptor);
        torflowtorctlclient_free(torctl);
        return NULL;
    }

    return torctl;
}

void torflowtorctlclient_free(TorFlowTorCtlClient* torctl) {
    g_assert(torctl);

    /* make sure we dont get a callback on our torctl instance which we are about to free and invalidate */
    torfloweventmanager_deregister(torctl->manager, torctl->descriptor);

    if(torctl->descriptor) {
        close(torctl->descriptor);
    }

    if(torctl->receiveLineBuffer) {
        g_string_free(torctl->receiveLineBuffer, TRUE);
    }

    if(torctl->commands) {
        while(!g_queue_is_empty(torctl->commands)) {
            g_string_free(g_queue_pop_head(torctl->commands), TRUE);
        }
        g_queue_free(torctl->commands);
    }

    if(torctl->cachedCircPath) {
        g_free(torctl->cachedCircPath);
    }

    if(torctl->id) {
        g_free(torctl->id);
    }

    g_free(torctl);
}

const gchar* torflowtorctlclient_getCurrentPath(TorFlowTorCtlClient* torctl) {
    g_assert(torctl);
    return torctl->cachedCircPath;
}

void torflowtorctlclient_commandAuthenticate(TorFlowTorCtlClient* torctl,
        OnAuthenticatedFunc onAuthenticated, gpointer onAuthenticatedArg) {
    g_assert(torctl);

    torctl->onAuthenticated = onAuthenticated;
    torctl->onAuthenticatedArg = onAuthenticatedArg;

    /* our control socket is connected, authenticate to control port */
    g_queue_push_tail(torctl->commands, g_string_new("AUTHENTICATE \"password\"\r\n"));
    torctl->state = CTL_AUTHENTICATE;

    /* go into writing mode, write the command, and then go into reading mode */
    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);
}

void torflowtorctlclient_commandGetBootstrapStatus(TorFlowTorCtlClient* torctl,
        OnBootstrappedFunc onBootstrapped, gpointer onBootstrappedArg) {
    g_assert(torctl);

    torctl->onBootstrapped = onBootstrapped;
    torctl->onBootstrappedArg = onBootstrappedArg;

    g_queue_push_tail(torctl->commands, g_string_new("GETINFO status/bootstrap-phase\r\n"));
    torctl->state = CTL_BOOTSTRAP;

    /* go into writing mode, write the command, and then go into reading mode */
    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);
}

void torflowtorctlclient_commandSetupTorConfig(TorFlowTorCtlClient* torctl) {
    g_assert(torctl);
    g_queue_push_tail(torctl->commands, g_string_new("SETCONF __LeaveStreamsUnattached=1 __DisablePredictedCircuits=1 MaxCircuitDirtiness=36000 CircuitStreamTimeout=3600\r\n"));
    g_queue_push_tail(torctl->commands, g_string_new("SIGNAL NEWNYM\r\n"));
    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);
}

void torflowtorctlclient_commandEnableEvents(TorFlowTorCtlClient* torctl) {
    g_assert(torctl);
    g_queue_push_tail(torctl->commands, g_string_new("SETEVENTS CIRC STREAM\r\n"));
    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);
}

void torflowtorctlclient_commandDisableEvents(TorFlowTorCtlClient* torctl) {
    g_assert(torctl);
    g_queue_push_tail(torctl->commands, g_string_new("SETEVENTS\r\n"));
    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);
}

void torflowtorctlclient_commandGetDescriptorInfo(TorFlowTorCtlClient* torctl,
        OnDescriptorsReceivedFunc onDescriptorsReceived, gpointer onDescriptorsReceivedArg) {
    g_assert(torctl);

    torctl->onDescriptorsReceived = onDescriptorsReceived;
    torctl->onDescriptorsReceivedArg = onDescriptorsReceivedArg;

    GString* command = g_string_new(NULL);
    //g_string_printf(command, "GETINFO dir/status-vote/current/consensus\r\n");
    g_string_printf(command, "GETINFO ns/all\r\n");
    g_queue_push_tail(torctl->commands, command);
    torctl->waitingGetDescriptorsResponse = TRUE;

    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);

    debug("%s: queued a GETINFO command", torctl->id);
}

void torflowtorctlclient_commandBuildNewCircuit(TorFlowTorCtlClient* torctl, gchar* path,
        OnCircuitBuiltFunc onCircuitBuilt, gpointer onCircuitBuiltArg) {
    g_assert(torctl);
    g_assert(path);

    torctl->onCircuitBuilt = onCircuitBuilt;
    torctl->onCircuitBuiltArg = onCircuitBuiltArg;

    /* build a new circuit with the given path */
    GString* command = g_string_new(NULL);
    g_string_printf(command, "EXTENDCIRCUIT 0 %s\r\n", path);
    g_queue_push_tail(torctl->commands, command);

    /* make sure we will get the correct circuit */
    torctl->waitingMeasurementCircuit = TRUE;

    if(torctl->cachedCircPath) {
        g_free(torctl->cachedCircPath);
    }
    torctl->cachedCircPath = g_strdup(path);
    torctl->cachedCircId = 0;

    /* send the commands */
    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);

    debug("%s: queued a EXTENDCIRCUIT command for %s", torctl->id, path);
}

void torflowtorctlclient_setNewStreamCallback(TorFlowTorCtlClient* torctl, in_port_t clientSocksPort,
        OnStreamNewFunc onStreamNew, gpointer onStreamNewArg) {
    g_assert(torctl);

    torctl->onStreamNew = onStreamNew;
    torctl->onStreamNewArg = onStreamNewArg;
    torctl->streamFilterPort = clientSocksPort;
}

void torflowtorctlclient_commandAttachStreamToCircuit(TorFlowTorCtlClient* torctl, gint streamID, gint circuitID,
        OnStreamSucceededFunc onStreamSucceeded, gpointer onStreamSucceededArg) {
    g_assert(torctl);

    torctl->onStreamSucceeded = onStreamSucceeded;
    torctl->onStreamSucceededArg = onStreamSucceededArg;

    GString* command = g_string_new(NULL);
    g_string_printf(command, "ATTACHSTREAM %i %i\r\n", streamID, circuitID);
    g_queue_push_tail(torctl->commands, command);

    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);

    torctl->waitingAttachStreamResponse = TRUE;

    debug("%s: queued a ATTACHSTREAM command for stream %i to circuit %i", torctl->id, streamID, circuitID);
}

void torflowtorctlclient_commandCloseCircuit(TorFlowTorCtlClient* torctl, gint crcuitID) {
    g_assert(torctl);

    GString* command = g_string_new(NULL);
    g_string_printf(command, "CLOSECIRCUIT %i\r\n", crcuitID);
    g_queue_push_tail(torctl->commands, command);

    _torflowtorctlclient_flushCommands(torctl, TORFLOW_EV_NONE);

    debug("%s: queued a CLOSECIRCUIT command for %i", torctl->id, crcuitID);
}
