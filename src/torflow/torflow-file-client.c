/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

#define BUFSIZE 16384

typedef enum {
    TORFLOWSOCKSCLIENT_NONE,
    TORFLOWSOCKSCLIENT_CONNECTING,
    TORFLOWSOCKSCLIENT_SOCKSSENDCONNECT,
    TORFLOWSOCKSCLIENT_SOCKSRECVCONNECT,
    TORFLOWSOCKSCLIENT_SOCKSSENDINIT,
    TORFLOWSOCKSCLIENT_SOCKSRECVINIT,
    TORFLOWSOCKSCLIENT_HTTPSENDREQUEST,
    TORFLOWSOCKSCLIENT_HTTPRECVREPLY,
    TORFLOWSOCKSCLIENT_ERROR
} TorFlowSocksClientState;

struct _TorFlowFileClient {
    TorFlowEventManager* manager;

    TorFlowPeer* fileServer;
    gsize transferSizeBytes;

    in_port_t networkSocksServerPort;
    in_port_t hostSocksClientPort;

    OnFileClientCompleteFunc onFileClientComplete;
    gpointer onFileClientCompleteArg;

    gint descriptor;

    TorFlowSocksClientState state;

    guint sendOffset;

    gchar recvbuf[BUFSIZE+1];
    guint recvOffset;
    gsize remaining;
    gdouble nextRecvPercLog;

    struct timespec start;
    struct timespec first;
    struct timespec end;

    gchar* id;
};

static gsize _torflowfileclient_computeTime(struct timespec* start, struct timespec* end) {
    g_assert(start && end);
    struct timespec result;
    result.tv_sec = end->tv_sec - start->tv_sec;
    result.tv_nsec = end->tv_nsec - start->tv_nsec;
    while(result.tv_nsec < 0) {
        result.tv_sec--;
        result.tv_nsec += 1000000000;
    }
    gsize millis = (gsize)((result.tv_sec * 1000) + (result.tv_nsec / 1000000));
    return millis;
}

static void _torflowfileclient_state(TorFlowFileClient* client, TorFlowEventFlag type) {
    g_assert(client);

    if(type & TORFLOW_EV_WRITE) {
        debug("%s: EPOLLOUT is set", client->id);
    }
    if(type & TORFLOW_EV_READ) {
        debug("%s: EPOLLIN is set", client->id);
    }

beginsocks:
    switch(client->state) {
    case TORFLOWSOCKSCLIENT_SOCKSSENDINIT: {
        g_assert(type & TORFLOW_EV_WRITE);

        gchar sendbuf[16];
        sendbuf[0] = 0x05;
        sendbuf[1] = 0x01;
        sendbuf[2] = 0x00;
        gint bytes = send(client->descriptor, &sendbuf[client->sendOffset], 3-client->sendOffset, 0);

        if(bytes < 0) {
            /* socket has an error */
            warning("%s: error on socket %i in TORFLOWSOCKSCLIENT_SOCKSSENDINIT: %i: %s",
                    client->id, client->descriptor, bytes, g_strerror(errno));
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        } else if(bytes == 0) {
            /* socket closed */
            warning("%s: on socket %i socket closed in TORFLOWSOCKSCLIENT_SOCKSSENDINIT",
                    client->id, client->descriptor);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        info("%s: on socket %i socket accepted %i/3 bytes in TORFLOWSOCKSCLIENT_SOCKSSENDINIT",
                client->id, client->descriptor, bytes);
        client->sendOffset += (guint)bytes;

        if(client->sendOffset < 3) {
            /* we couldn't send all 3 bytes, try more next time */
            break;
        }

        /* if we got here, we are done sending INIT */
        g_assert(client->sendOffset == 3);
        client->sendOffset = 0;

        /* next we wait for socks init response */
        client->state = TORFLOWSOCKSCLIENT_SOCKSRECVINIT;
        torfloweventmanager_register(client->manager, client->descriptor, TORFLOW_EV_READ,
                        (TorFlowOnEventFunc)_torflowfileclient_state, client);
        break;
    }

    case TORFLOWSOCKSCLIENT_SOCKSRECVINIT: {
        g_assert(type & TORFLOW_EV_READ);

        gint bytes = recv(client->descriptor, &client->recvbuf[client->recvOffset], BUFSIZE-client->recvOffset, 0);

        if(bytes < 0) {
            /* socket has an error */
            warning("%s: error on socket %i in TORFLOWSOCKSCLIENT_SOCKSRECVINIT: %i: %s",
                    client->id, client->descriptor, bytes, g_strerror(errno));
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        } else if(bytes == 0) {
            /* socket closed */
            warning("%s: on socket %i socket closed in TORFLOWSOCKSCLIENT_SOCKSRECVINIT",
                    client->id, client->descriptor);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        info("%s: on socket %i socket got %i/2 bytes in TORFLOWSOCKSCLIENT_SOCKSRECVINIT",
                client->id, client->descriptor, bytes);
        client->recvOffset += (guint)bytes;

        if(client->recvOffset < 2) {
            /* we couldn't recv all 2 bytes, try more next time */
            break;
        }

        g_assert(client->recvOffset == 2);
        client->recvOffset = 0;

        if(client->recvbuf[0] != 0x05 || client->recvbuf[1] != 0x00) {
            critical("%s: socks init error: code %x%x", client->id, bytes, client->recvbuf[0], client->recvbuf[1]);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        info("%s: socks init success", client->id);

        /* next we write socks connect command */
        client->state = TORFLOWSOCKSCLIENT_SOCKSSENDCONNECT;
        torfloweventmanager_register(client->manager, client->descriptor, TORFLOW_EV_WRITE,
                        (TorFlowOnEventFunc)_torflowfileclient_state, client);

        break;
    }

    case TORFLOWSOCKSCLIENT_SOCKSSENDCONNECT: {
        g_assert(type & TORFLOW_EV_WRITE);

        in_addr_t netIP = torflowpeer_getNetIP(client->fileServer);
        in_port_t netPort = torflowpeer_getNetPort(client->fileServer);

        gchar sendbuf[64];
        memset(sendbuf, 0, sizeof(gchar)*64);
        sendbuf[0] = 0x05;
        sendbuf[1] = 0x01;
        sendbuf[2] = 0x00;
        sendbuf[3] = 0x01;
        memcpy(&sendbuf[4], &(netIP), 4);
        memcpy(&sendbuf[8], &(netPort), 2);

        gint bytes = send(client->descriptor, &sendbuf[client->sendOffset], 10-client->sendOffset, 0);

        if(bytes < 0) {
            /* socket has an error */
            warning("%s: error on socket %i in TORFLOWSOCKSCLIENT_SOCKSSENDCONNECT: %i: %s",
                    client->id, client->descriptor, bytes, g_strerror(errno));
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        } else if(bytes == 0) {
            /* socket closed */
            warning("%s: on socket %i socket closed in TORFLOWSOCKSCLIENT_SOCKSSENDCONNECT",
                    client->id, client->descriptor);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        info("%s: on socket %i socket accepted %i/10 bytes in TORFLOWSOCKSCLIENT_SOCKSSENDCONNECT",
                client->id, client->descriptor, bytes);
        client->sendOffset += (guint)bytes;

        if(client->sendOffset < 10) {
            /* we couldn't send all 10 bytes, try more next time */
            break;
        }

        g_assert(client->sendOffset == 10);
        client->sendOffset = 0;

        info("%s: sent socks server connect to %s at %s:%u",
                client->id,
                torflowpeer_getName(client->fileServer),
                torflowpeer_getHostIPStr(client->fileServer), ntohs(netPort));

        /* next we receive the socks connect response */
        client->state = TORFLOWSOCKSCLIENT_SOCKSRECVCONNECT;
        torfloweventmanager_register(client->manager, client->descriptor, TORFLOW_EV_READ,
                        (TorFlowOnEventFunc)_torflowfileclient_state, client);
        break;
    }

    case TORFLOWSOCKSCLIENT_SOCKSRECVCONNECT: {
        g_assert(type & TORFLOW_EV_READ);

        gint bytes = recv(client->descriptor, &client->recvbuf[client->recvOffset], BUFSIZE-client->recvOffset, 0);

        if(bytes < 0) {
            /* socket has an error */
            warning("%s: error on socket %i in TORFLOWSOCKSCLIENT_SOCKSRECVCONNECT: %i: %s",
                    client->id, client->descriptor, bytes, g_strerror(errno));
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        } else if(bytes == 0) {
            /* socket closed */
            warning("%s: on socket %i socket closed in TORFLOWSOCKSCLIENT_SOCKSRECVCONNECT",
                    client->id, client->descriptor);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        info("%s: on socket %i socket got %i/10 bytes in TORFLOWSOCKSCLIENT_SOCKSRECVCONNECT",
                client->id, client->descriptor, bytes);
        client->recvOffset += (guint)bytes;

        if(client->recvOffset < 10) {
            /* we couldn't recv all 10 bytes, try more next time */
            break;
        }

        g_assert(client->recvOffset == 10);
        client->recvOffset = 0;

        if(client->recvbuf[0] == 0x05 && client->recvbuf[1] == 0x00 && client->recvbuf[3] == 0x01) {
            /* socks server may tell us to connect somewhere else ... */
            in_addr_t serverAddress;
            in_port_t serverPort;
            memcpy(&serverAddress, &(client->recvbuf[4]), 4);
            memcpy(&serverPort, &(client->recvbuf[8]), 2);

            /* ... but we dont support it */
            g_assert(serverAddress == 0 && serverPort == 0);

            info("%s: socks connect success", client->id);

            /* next write the request */
            client->state = TORFLOWSOCKSCLIENT_HTTPSENDREQUEST;
            torfloweventmanager_register(client->manager, client->descriptor, TORFLOW_EV_WRITE,
                            (TorFlowOnEventFunc)_torflowfileclient_state, client);
        } else if(client->recvbuf[0] == 0x05 && client->recvbuf[1] == 0x06 && client->recvbuf[2] == 0x00 && client->recvbuf[3] == 0x01) {
            message("%s: socks connect timed out", client->id);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        } else {
            critical("%s: socks connect error (read %i bytes, code %x%x%x%x)", client->id, bytes,
                client->recvbuf[0], client->recvbuf[1], client->recvbuf[2], client->recvbuf[3]);
            //tf->_base.slogf(G_LOG_LEVEL_CRITICAL, client->id,
            //  "socks connect error (read %i bytes)", bytes);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        /* reset */
        memset(client->recvbuf, 0, BUFSIZE);

        break;
    }

    case TORFLOWSOCKSCLIENT_HTTPSENDREQUEST: {
        g_assert(type & TORFLOW_EV_WRITE);

        GString* request = g_string_new(NULL);
        g_string_printf(request, "TORFLOW GET %"G_GUINT64_FORMAT"\r\n\r\n", (guint64)client->transferSizeBytes);

        gint bytes = send(client->descriptor, &request->str[client->sendOffset], request->len-client->sendOffset, 0);

        if(bytes < 0) {
            /* socket has an error */
            warning("%s: error on socket %i in TORFLOWSOCKSCLIENT_HTTPSENDREQUEST: %i: %s",
                    client->id, client->descriptor, bytes, g_strerror(errno));
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        } else if(bytes == 0) {
            /* socket closed */
            warning("%s: on socket %i socket closed in TORFLOWSOCKSCLIENT_HTTPSENDREQUEST",
                    client->id, client->descriptor);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        info("%s: on socket %i socket accepted %i/%i bytes in TORFLOWSOCKSCLIENT_HTTPSENDREQUEST",
                client->id, client->descriptor, bytes, (gint)request->len);
        client->sendOffset += (guint)bytes;

        if(client->sendOffset < ((guint)request->len)) {
            /* we couldn't send all bytes, try more next time */
            break;
        }

        g_assert(client->sendOffset == ((guint)request->len));
        client->sendOffset = 0;
        g_string_free(request, TRUE);

        clock_gettime(CLOCK_REALTIME, &(client->start));

        /* now start reading (downloading) the response */
        client->remaining = client->transferSizeBytes;
        client->nextRecvPercLog = 20.0f;
        client->state = TORFLOWSOCKSCLIENT_HTTPRECVREPLY;
        torfloweventmanager_register(client->manager, client->descriptor, TORFLOW_EV_READ,
                        (TorFlowOnEventFunc)_torflowfileclient_state, client);
        break;
    }

    case TORFLOWSOCKSCLIENT_HTTPRECVREPLY: {
        g_assert(type & TORFLOW_EV_READ);

        gssize result = recv(client->descriptor, client->recvbuf, BUFSIZE, 0);

        if(result < 0) {
            /* socket has an error */
            warning("%s: error on socket %i in TORFLOWSOCKSCLIENT_HTTPRECVREPLY: %i: %s",
                    client->id, client->descriptor, errno, g_strerror(errno));
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        } else if(result == 0) {
            /* socket closed */
            warning("%s: on socket %i socket closed in TORFLOWSOCKSCLIENT_HTTPRECVREPLY",
                    client->id, client->descriptor);
            client->state = TORFLOWSOCKSCLIENT_ERROR;
            goto beginsocks;
        }

        gsize bytesReceived = (gsize)result;

        if(client->remaining == client->transferSizeBytes) {
            clock_gettime(CLOCK_REALTIME, &(client->first));
        }

        if(bytesReceived <= client->remaining) {
            client->remaining -= bytesReceived;
        } else {
            client->remaining = 0;
        }

        gsize totalRecv = client->transferSizeBytes - client->remaining;
        gdouble percRecv = (gdouble)totalRecv / (gdouble)client->transferSizeBytes;
        percRecv *= 100.0f;

        while(percRecv >= client->nextRecvPercLog) {
            info("%s: on socket %i socket got %zu/%zu bytes (%.02f\%) in TORFLOWSOCKSCLIENT_HTTPRECVREPLY",
                    client->id, client->descriptor, totalRecv, client->transferSizeBytes, percRecv);
            /* log a message every 20 percent */
            client->nextRecvPercLog += 20.0f;
        }

        /* finished a download probe - only the prober will get here bc senders stop reading */
        if(client->remaining <= 0) {
            clock_gettime(CLOCK_REALTIME, &(client->end));

            gsize roundTripTime = _torflowfileclient_computeTime(&client->start, &client->first);
            gsize payloadTime = _torflowfileclient_computeTime(&client->first, &client->end);
            gsize totalTime = _torflowfileclient_computeTime(&client->start, &client->end);
            gsize contentLength = client->transferSizeBytes;

            client->state = TORFLOWSOCKSCLIENT_NONE;
            torfloweventmanager_deregister(client->manager, client->descriptor);

            info("%s: finished download of %zu bytes", client->id, client->transferSizeBytes);

            if(client->onFileClientComplete) {
                client->onFileClientComplete(client->onFileClientCompleteArg,
                        TRUE, contentLength, roundTripTime, payloadTime, totalTime);
            }
        }

        break;
    }

    case TORFLOWSOCKSCLIENT_ERROR: {
        /* this is an error or timeout */
        torfloweventmanager_deregister(client->manager, client->descriptor);
        close(client->descriptor);
        client->descriptor = 0;

        if(client->onFileClientComplete) {
            client->onFileClientComplete(client->onFileClientCompleteArg, FALSE, 0, 0, 0, 0);
        }
        break;
    }

    default:
        break;
    }
}

static void _torflowfileclient_onConnected(TorFlowFileClient* client, TorFlowEventFlag type) {
    g_assert(client);

    /* deregister so this function doesn't get called again */
    torfloweventmanager_deregister(client->manager, client->descriptor);

    /* we are connected */
    client->state = TORFLOWSOCKSCLIENT_SOCKSSENDINIT;

    /* we want to write the socks handshake next */
    torfloweventmanager_register(client->manager, client->descriptor, TORFLOW_EV_WRITE,
                (TorFlowOnEventFunc)_torflowfileclient_state, client);
}

TorFlowFileClient* torflowfileclient_new(TorFlowEventManager* manager, guint workerID,
        in_port_t socksPort, TorFlowPeer* fileServer, gsize transferSizeBytes,
        OnFileClientCompleteFunc onFileClientComplete, gpointer onFileClientCompleteArg) {
    g_assert(manager);
    g_assert(fileServer);

    TorFlowFileClient* client = g_new0(TorFlowFileClient, 1);

    client->manager = manager;
    client->onFileClientComplete = onFileClientComplete;
    client->onFileClientCompleteArg = onFileClientCompleteArg;

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Worker%u-FileClient", workerID);
    client->id = g_string_free(idbuf, FALSE);

    client->networkSocksServerPort = socksPort;
    client->fileServer = fileServer;
    torflowpeer_ref(fileServer);
    client->transferSizeBytes = transferSizeBytes;

    /* create the client socket and get a socket descriptor */
    client->descriptor = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);

    /* check socket error */
    if(client->descriptor < 0) {
        warning("%s: unable to create SOCKS socket: error in socket", client->id);
        torflowfileclient_free(client);
        return NULL;
    }

    /* our client socket address information for connecting to the socks server */
    struct sockaddr_in server;
    memset(&server, 0, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);;
    server.sin_port = client->networkSocksServerPort;

    /* connect to server. since we are non-blocking, we expect this to return -1 and EINPROGRESS */
    gint result = connect(client->descriptor, (struct sockaddr *) &server, sizeof(struct sockaddr_in));

    /* check connection error */
    if (result == -1 && errno != EINPROGRESS) {
        warning("%s: unable to connect SOCKS socket: error in connect", client->id);
        torflowfileclient_free(client);
        return NULL;
    }

    /* we want to get client side name info for the socks socket */
    struct sockaddr name;
    memset(&name, 0, sizeof(struct sockaddr));
    socklen_t nameLen = (socklen_t)sizeof(struct sockaddr);

    /* get the socket name, i.e., address and port */
    result = getsockname(client->descriptor, &name, &nameLen);

    /* check for sockname error */
    if(result < 0) {
        warning("%s: unable to get client port on SOCKS socket: error in getsockname", client->id);
        torflowfileclient_free(client);
        return NULL;
    }

    client->hostSocksClientPort = (in_port_t)ntohs(((struct sockaddr_in*)&name)->sin_port);

    /* we need to know when we are connected and can send a message. */
    gboolean success = torfloweventmanager_register(client->manager, client->descriptor, TORFLOW_EV_WRITE,
            (TorFlowOnEventFunc)_torflowfileclient_onConnected, client);

    if(!success) {
        critical("%s: unable to register descriptor %i with event manager", client->id, client->descriptor);
        torflowfileclient_free(client);
        return NULL;
    }

    client->state = TORFLOWSOCKSCLIENT_CONNECTING;

    return client;
}

void torflowfileclient_free(TorFlowFileClient* client) {
    g_assert(client);

    if(client->fileServer) {
        torflowpeer_unref(client->fileServer);
    }

    if(client->descriptor > 0) {
        torfloweventmanager_deregister(client->manager, client->descriptor);
        close(client->descriptor);
        client->descriptor = 0;
    }

    if(client->id) {
        g_free(client->id);
    }

    g_free(client);
}

in_port_t torflowfileclient_getHostClientSocksPort(TorFlowFileClient* client) {
    g_assert(client);
    return client->hostSocksClientPort;
}
