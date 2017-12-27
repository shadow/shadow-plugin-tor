/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_CONFIG_H_
#define SRC_TORFLOW_TORFLOW_CONFIG_H_

#include <netdb.h>

#include <glib.h>

typedef enum _TorFlowMode TorFlowMode;
enum _TorFlowMode {
    TORFLOW_MODE_TORFLOW, TORFLOW_MODE_FILESERVER
};

typedef struct _TorFlowConfig TorFlowConfig;

TorFlowConfig* torflowconfig_new(gint argc, gchar* argv[]);
void torflowconfig_free(TorFlowConfig* config);

const gchar* torflowconfig_getV3BWFilePath(TorFlowConfig* config);
in_port_t torflowconfig_getTorSocksPort(TorFlowConfig* config);
in_port_t torflowconfig_getTorControlPort(TorFlowConfig* config);
in_port_t torflowconfig_getListenerPort(TorFlowConfig* config);
guint torflowconfig_getScanIntervalSeconds(TorFlowConfig* config);
guint torflowconfig_getNumParallelProbes(TorFlowConfig* config);
guint torflowconfig_getNumRelaysPerSlice(TorFlowConfig* config);
gdouble torflowconfig_getMaxRelayWeightFraction(TorFlowConfig* config);
guint torflowconfig_getProbeTimeoutSeconds(TorFlowConfig* config);
guint torflowconfig_getDownloadTimeoutSeconds(TorFlowConfig* config);
guint torflowconfig_getNumProbesPerRelay(TorFlowConfig* config);
GLogLevelFlags torflowconfig_getLogLevel(TorFlowConfig* config);
TorFlowMode torflowconfig_getMode(TorFlowConfig* config);

TorFlowPeer* torflowconfig_cycleFileServerPeers(TorFlowConfig* config);

#endif /* SRC_TORFLOW_TORFLOW_CONFIG_H_ */
