/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowFileListener {
    TorFlowEventManager* manager;
    in_port_t listenPort;

    gint descriptor;

    guint workerID;
    gchar* id;

    GHashTable* servers;
    guint numServerSuccesses;
    guint numServerFailures;
    gsize bytesSent;
};

/* necessary forward declaration */
static gboolean _torflowfilelistener_setupListener(TorFlowFileListener* listener);

static void _torflowfilelistener_onFileServerComplete(TorFlowFileListener* listener, gint descriptor,
        gboolean isSuccess, gsize bytesSent) {
    g_assert(listener);

    /* a connection finished */
    listener->bytesSent += bytesSent;
    if(isSuccess) {
        listener->numServerSuccesses++;
        info("%s: server socket %i succeeded", listener->id, descriptor);
    } else {
        listener->numServerFailures++;
        info("%s: server socket %i failed", listener->id, descriptor);
    }

    /* this will call free on the child connection object */
    g_hash_table_remove(listener->servers, GINT_TO_POINTER(descriptor));

    message("%s: listener socket %i status: %u total successes, %u total failures, %zu total bytes sent, %i servers open",
            listener->id, listener->descriptor,
            listener->numServerSuccesses, listener->numServerFailures, listener->bytesSent,
            g_hash_table_size(listener->servers));
}

static void _torflowfilelistener_onListenerReadable(TorFlowFileListener* listener, TorFlowEventFlag type) {
    g_assert(listener);

    if(!(type & TORFLOW_EV_READ)) {
        return;
    }

    gint childDescriptor = accept(listener->descriptor, NULL, NULL);

    if(childDescriptor < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
        /* non-blocking, wait for the next one. really, this shouldn't happen since
         * the descriptor is marked as readable we should be able to accept one. */
        return;
    }

    if(childDescriptor == 0 || (childDescriptor < 0 && errno == EBADF)) {
        /* the listener socket closed? lets try to build a new one */
        torfloweventmanager_deregister(listener->manager, listener->descriptor);
        close(listener->descriptor);
        listener->descriptor = 0;

        if(_torflowfilelistener_setupListener(listener)) {
            message("%s: listener socket closed, we recovered with a new socket %i", listener->id, listener->descriptor);
        } else {
            critical("%s: listener socket problem, tried to recover but failed!", listener->id);
        }
        return;
    }

    if(childDescriptor < 0) {
        warning("%s: unable to accept connection on listener socket %i: error %i in accept(): %s",
             listener->id, listener->descriptor, errno, g_strerror(errno));
        return;
    }

    /* ok, now we know we got a valid child socket */
    TorFlowFileServer* server = torflowfileserver_new(listener->manager, listener->workerID, childDescriptor,
            (OnFileServerCompleteFunc)_torflowfilelistener_onFileServerComplete, listener);

    g_hash_table_replace(listener->servers, GINT_TO_POINTER(childDescriptor), server);
}

static gboolean _torflowfilelistener_setupListener(TorFlowFileListener* listener) {
    g_assert(listener);

    /* create the socket and get a socket descriptor */
    listener->descriptor = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if(listener->descriptor < 0) {
        warning("%s: unable to create listener socket: error %i in socket(): %s",
             listener->id, errno, g_strerror(errno));
        return FALSE;
    }

    /* setup the socket address info, listener will listen for incoming
    * connections on listen_port on all interfaces */
    struct sockaddr_in listenInfo;
    memset(&listenInfo, 0, sizeof(struct sockaddr_in));
    listenInfo.sin_family = AF_INET;
    listenInfo.sin_addr.s_addr = htonl(INADDR_ANY);
    listenInfo.sin_port = listener->listenPort;

    socklen_t listenerLen = (socklen_t)sizeof(struct sockaddr_in);

    /* bind the socket to the listener port */
    gint result = bind(listener->descriptor, (struct sockaddr *) &listenInfo, listenerLen);
    if(result < 0) {
        warning("%s: unable to bind listener socket %i: error %i in bind(): %s",
             listener->id, listener->descriptor, errno, g_strerror(errno));
        return FALSE;
    }

    /* set as listener listening socket */
    result = listen(listener->descriptor, 1000);
    if(result < 0) {
        warning("%s: unable to listen on socket %i: error %i in listen(): %s",
             listener->id, listener->descriptor, errno, g_strerror(errno));
        return FALSE;
    }

    /* notify us when we are ready to accept connects */
    gboolean success = torfloweventmanager_register(listener->manager, listener->descriptor, TORFLOW_EV_READ,
            (TorFlowOnEventFunc)_torflowfilelistener_onListenerReadable, listener);

    return success;
}

TorFlowFileListener* torflowfilelistener_new(TorFlowEventManager* manager, guint workerID, in_port_t listenPort) {
    TorFlowFileListener* listener = g_new0(TorFlowFileListener, 1);

    listener->manager = manager;
    listener->workerID = workerID;
    listener->listenPort = listenPort;

    /* hash table to store child connection objects */
    listener->servers = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)torflowfileserver_free);

    /* set up our id for log messages */
    GString* idbuf = g_string_new(NULL);
    g_string_printf(idbuf, "Worker%u-FileListener", workerID);
    listener->id = g_string_free(idbuf, FALSE);

    /* setup the listener, if it fails, we fail */
    if(!_torflowfilelistener_setupListener(listener)) {
        critical("%s: unable to set up file listener listener socket", listener->id);
        torflowfilelistener_free(listener);
        return NULL;
    }

    message("%s: file listener running on port %u", listener->id, ntohs(listener->listenPort));

    return listener;
}

void torflowfilelistener_free(TorFlowFileListener* listener) {
    g_assert(listener);

    if(listener->descriptor > 0) {
        torfloweventmanager_deregister(listener->manager, listener->descriptor);
        close(listener->descriptor);
    }

    /* this will free all of the children connection objects */
    if(listener->servers) {
        g_hash_table_destroy(listener->servers);
    }

    if(listener->id) {
        g_free(listener->id);
    }

    g_free(listener);
}
