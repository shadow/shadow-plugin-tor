/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

#define TORFLOW_FILESERVER_BUF_SIZE 64

struct _TorFlowFileServer {
    TorFlowEventManager* manager;

    guint workerID;
    gchar* id;

    OnFileServerCompleteFunc onFileServerComplete;
    gpointer onFileServerCompleteArg;

    gint descriptor;

    gchar buffer[TORFLOW_FILESERVER_BUF_SIZE];
    gsize offset;
    gsize bytesRequested;
    gsize bytesSent;
};

static void _torflowfileserver_finish(TorFlowFileServer* server, gboolean isSuccess) {
    g_assert(server);

    if(server->onFileServerComplete) {
        /* our owner should free us */
        server->onFileServerComplete(server->onFileServerCompleteArg, server->descriptor, isSuccess, server->bytesSent);
    } else {
        /* we free ourselves */
        torflowfileserver_free(server);
    }
}

static void _torflowfileserver_onEventSendResponse(TorFlowFileServer* server, TorFlowEventFlag type) {
    g_assert(server);

    if(!(type & TORFLOW_EV_WRITE)) {
        return;
    }

    const gsize bufferSize = 8192;
    gchar buffer[bufferSize];
    memset(buffer, 6, bufferSize);

    while(TRUE) {
        gsize amountToSend = MIN(bufferSize, MAX(0, server->bytesRequested - server->bytesSent));
        gssize result = send(server->descriptor, buffer, amountToSend, 0);

        /* check potential problems */
        if(result < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            /* non-blocking, we send everything we could for now */
            break;
        } else if(result < 0) {
            /* some other error, which we probably can't recover from */
            warning("%s: problem sending on socket %i: error %i in send(): %s",
                    server->id, server->descriptor, errno, g_strerror(errno));

            _torflowfileserver_finish(server, FALSE);
            return;
        } else if(result == 0) {
            /* the socket closed before we finished */
            warning("%s: socket %i closed before expected during send", server->id, server->descriptor);

            _torflowfileserver_finish(server, FALSE);
            return;
        }

        /* successful send */
        gsize amountSent = (gsize) result;
        server->bytesSent += amountSent;

        if(server->bytesSent >= server->bytesRequested) {
            /* we finished sending everything */
            message("%s: successfully sent %zu bytes on socket %i", server->id, server->bytesSent, server->descriptor);

            /* stop trying to write */
            torfloweventmanager_deregister(server->manager, server->descriptor);

            /* we are done */
            _torflowfileserver_finish(server, TRUE);
            return;
        }
    }
}

static void _torflowfileserver_onEventReceiveRequest(TorFlowFileServer* server, TorFlowEventFlag type) {
    g_assert(server);

    if(!(type & TORFLOW_EV_READ)) {
        return;
    }

    while(TRUE) {
        gsize amountToRead = MAX(0, TORFLOW_FILESERVER_BUF_SIZE - server->offset - 1);

        gssize result = recv(server->descriptor, &(server->buffer[server->offset]), amountToRead, 0);

        /* check potential problems */
        if(result < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
            /* non-blocking, we read everything we could for now */
            break;
        } else if(result < 0) {
            /* some other error, which we probably can't recover from */
            warning("%s: problem reading on socket %i: error %i in recv(): %s",
                    server->id, server->descriptor, errno, g_strerror(errno));

            _torflowfileserver_finish(server, FALSE);
            return;
        } else if(result == 0) {
            /* the socket closed before we finished */
            warning("%s: socket %i closed before expected during receive", server->id, server->descriptor);

            _torflowfileserver_finish(server, FALSE);
            return;
        }

        /* successful read */
        gsize amountRead = (gsize) result;
        server->offset += amountRead;

        /* make sure we have a null byte after the payload */
        server->buffer[server->offset] = 0x0;

        /* check if we have everything. client sends:
         *   "TORFLOW GET %lu\r\n\r\n" */
        gchar* suffix = g_strstr_len(server->buffer, (gssize)server->offset, "\r\n\r\n");

        if(suffix) {
            /* we have the full request, stop waiting for read events */
            torfloweventmanager_deregister(server->manager, server->descriptor);

            gchar* start = g_strstr_len(server->buffer, (gssize)server->offset, "TORFLOW GET ");
            if(!start) {
                warning("%s: socket %i malformed request '%s', failing",
                        server->id, server->descriptor, server->buffer);
               _torflowfileserver_finish(server, FALSE);
               return;
            }

            /* get the start of the requested bytes amount */
            gchar* requestedBytesString = &start[12];

            /* overwrite the '\r\n\r\n' suffix with null bytes */
            for(gint i = 0; i < 4 ; i++) {
                suffix[i] = 0x0;
            }

            /* parse the value */
            server->bytesRequested = (gsize)g_ascii_strtoull(requestedBytesString, NULL, 10);

            /* now we want to start sending the response */
            gboolean success = torfloweventmanager_register(server->manager, server->descriptor, TORFLOW_EV_WRITE,
                        (TorFlowOnEventFunc)_torflowfileserver_onEventSendResponse, server);

            if(!success) {
                warning("%s: socket %i can't wait for write events, failing", server->id, server->descriptor);
                _torflowfileserver_finish(server, FALSE);
            }
            return;
        }

        /* we dont have everything yet, loop and try to read more */
    }
}

TorFlowFileServer* torflowfileserver_new(TorFlowEventManager* manager, guint workerID, gint descriptor,
        OnFileServerCompleteFunc onFileServerComplete, gpointer onFileServerCompleteArg) {
    TorFlowFileServer* server = g_new0(TorFlowFileServer, 1);

    server->manager = manager;
    server->workerID = workerID;
    server->descriptor = descriptor;
    server->onFileServerComplete = onFileServerComplete;
    server->onFileServerCompleteArg = onFileServerCompleteArg;

    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Worker%u-FileServer-FD%i", workerID, descriptor);
    server->id = g_string_free(idbuf, FALSE);

    gboolean success = torfloweventmanager_register(server->manager, server->descriptor, TORFLOW_EV_READ,
            (TorFlowOnEventFunc)_torflowfileserver_onEventReceiveRequest, server);

    return server;
}

void torflowfileserver_free(TorFlowFileServer* server) {
    g_assert(server);

    if(server->descriptor > 0) {
        torfloweventmanager_deregister(server->manager, server->descriptor);
        close(server->descriptor);
        server->descriptor = 0;
    }

    if(server->id) {
        g_free(server->id);
    }

    g_free(server);
}
