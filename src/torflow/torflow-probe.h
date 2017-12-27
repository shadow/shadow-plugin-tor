/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_PROBE_H_
#define SRC_TORFLOW_TORFLOW_PROBE_H_

#include <glib.h>

typedef struct _TorFlowProbe TorFlowProbe;

typedef void (*OnProbeCompleteFunc)(gpointer userData, guint workerID,
        gchar* entryIdentity, gchar* exitIdentity, gboolean isSuccess,
        gsize contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime);

TorFlowProbe* torflowprobe_new(TorFlowEventManager* manager, guint workerID,
        in_port_t controlPort, in_port_t socksPort, TorFlowPeer* filePeer, gsize transferSize,
        const gchar* entryRelayIdentity, const gchar* exitRelayIdentity,
        OnProbeCompleteFunc onProbeComplete, gpointer onProbeCompleteArg);
void torflowprobe_free(TorFlowProbe* probe);

in_port_t torflowprobe_getHostClientSocksPort(TorFlowProbe* probe);
void torflowprobe_onTimeout(TorFlowProbe* probe);

#endif /* SRC_TORFLOW_TORFLOW_PROBE_H_ */
