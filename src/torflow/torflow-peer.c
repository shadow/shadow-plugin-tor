/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowPeer {
    gchar* name;
    gchar* hostIPString;
    in_addr_t netIP;
    in_port_t netPort;

    gint refcount;
};


static gchar* _torflowpeer_ipToNewString(in_addr_t netIP) {
    gchar* ipStringBuffer = g_malloc0(INET6_ADDRSTRLEN+1);
    const gchar* ipString = inet_ntop(AF_INET, &netIP, ipStringBuffer, INET6_ADDRSTRLEN);
    GString* result = ipString ? g_string_new(ipString) : g_string_new("NULL");
    g_free(ipStringBuffer);
    return g_string_free(result, FALSE);
}

static in_addr_t _torflowpeer_lookupAddress(const gchar* name) {
    struct addrinfo* info = NULL;
    gint ret = getaddrinfo((gchar*) name, NULL, NULL, &info);
    if(ret != 0 || !info) {
        error("hostname lookup failed '%s'", name);
        return 0;
    }
    in_addr_t netIP = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
    freeaddrinfo(info);
    return netIP;
}

TorFlowPeer* torflowpeer_new(const gchar* name, in_port_t networkPort) {
    in_addr_t networkIP = _torflowpeer_lookupAddress(name);
    if(networkIP == 0 || networkPort == 0) {
        return NULL;
    }

    TorFlowPeer* peer = g_new0(TorFlowPeer, 1);
    peer->refcount = 1;

    peer->netIP = networkIP;
    peer->netPort = networkPort;
    peer->name = g_strdup(name);
    peer->hostIPString = _torflowpeer_ipToNewString(networkIP);

    return peer;
}

static void _torflowpeer_free(TorFlowPeer* peer) {
    g_assert(peer);
    if(peer->name) {
        g_free(peer->name);
    }
    if(peer->hostIPString) {
        g_free(peer->hostIPString);
    }
    g_free(peer);
}

void torflowpeer_ref(TorFlowPeer* peer) {
    g_assert(peer);
    peer->refcount++;
}

void torflowpeer_unref(TorFlowPeer* peer) {
    g_assert(peer);
    if(--peer->refcount == 0) {
        _torflowpeer_free(peer);
    }
}

in_addr_t torflowpeer_getNetIP(TorFlowPeer* peer) {
    g_assert(peer);
    return peer->netIP;
}

in_port_t torflowpeer_getNetPort(TorFlowPeer* peer) {
    g_assert(peer);
    return peer->netPort;
}

const gchar* torflowpeer_getName(TorFlowPeer* peer) {
    g_assert(peer);
    return peer->name;
}

const gchar*  torflowpeer_getHostIPStr(TorFlowPeer* peer) {
    g_assert(peer);
    return peer->hostIPString;
}

