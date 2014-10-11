/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowFileServer {
    gchar* name;
    gchar* hostIPString;
    in_addr_t netIP;
    in_port_t netPort;

    gint refcount;
};

TorFlowFileServer* torflowfileserver_new(const gchar* name, in_port_t networkPort) {
    in_addr_t networkIP = torflowutil_lookupAddress(name, NULL);
    if(networkIP == 0 || networkPort == 0) {
        return NULL;
    }

    TorFlowFileServer* tffs = g_new0(TorFlowFileServer, 1);
    tffs->refcount = 1;

    tffs->netIP = networkIP;
    tffs->netPort = networkPort;
    tffs->name = g_strdup(name);
    tffs->hostIPString = torflowutil_ipToNewString(networkIP);

    return tffs;
}

static void _torflowfileserver_free(TorFlowFileServer* tffs) {
    g_assert(tffs);
    if(tffs->name) {
        g_free(tffs->name);
    }
    if(tffs->hostIPString) {
        g_free(tffs->hostIPString);
    }
    g_free(tffs);
}

void torflowfileserver_ref(TorFlowFileServer* tffs) {
    g_assert(tffs);
    tffs->refcount++;
}

void torflowfileserver_unref(TorFlowFileServer* tffs) {
    g_assert(tffs);
    if(--tffs->refcount == 0) {
        _torflowfileserver_free(tffs);
    }
}

in_addr_t torflowfileserver_getNetIP(TorFlowFileServer* tffs) {
    g_assert(tffs);
    return tffs->netIP;
}

in_port_t torflowfileserver_getNetPort(TorFlowFileServer* tffs) {
    g_assert(tffs);
    return tffs->netPort;
}

const gchar* torflowfileserver_getName(TorFlowFileServer* tffs) {
    g_assert(tffs);
    return tffs->name;
}

const gchar*  torflowfileserver_getHostIPStr(TorFlowFileServer* tffs) {
    g_assert(tffs);
    return tffs->hostIPString;
}

