/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_PEER_H_
#define SRC_TORFLOW_TORFLOW_PEER_H_

#include <netinet/in.h>

#include <glib.h>

typedef struct _TorFlowPeer TorFlowPeer;

TorFlowPeer* torflowpeer_new(const gchar* name, in_port_t networkPort);
void torflowpeer_ref(TorFlowPeer* peer);
void torflowpeer_unref(TorFlowPeer* peer);

in_addr_t torflowpeer_getNetIP(TorFlowPeer* peer);
in_port_t torflowpeer_getNetPort(TorFlowPeer* peer);
const gchar* torflowpeer_getName(TorFlowPeer* peer);
const gchar*  torflowpeer_getHostIPStr(TorFlowPeer* peer);

#endif /* SRC_TORFLOW_TORFLOW_PEER_H_ */
