/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_TORCTL_CLIENT_H_
#define SRC_TORFLOW_TORFLOW_TORCTL_CLIENT_H_

#include <glib.h>

#include "torflow-event-manager.h"

typedef struct _TorFlowTorCtlClient TorFlowTorCtlClient;

typedef void (*OnConnectedFunc)(gpointer userData);
typedef void (*OnAuthenticatedFunc)(gpointer userData);
typedef void (*OnBootstrappedFunc)(gpointer userData);
typedef void (*OnDescriptorsReceivedFunc)(gpointer userData, GQueue* descriptorLines);
typedef void (*OnCircuitBuiltFunc)(gpointer userData, gint circuitID);
typedef void (*OnStreamNewFunc)(gpointer userData, gint streamID,
        gchar* sourceAddress, in_port_t sourcePort, gchar* targetAddress, in_port_t targetPort);
typedef void (*OnStreamSucceededFunc)(gpointer userData, gint streamID, gint circuitID,
        gchar* sourceAddress, in_port_t sourcePort, gchar* targetAddress, in_port_t targetPort);

TorFlowTorCtlClient* torflowtorctlclient_new(TorFlowEventManager* manager, in_port_t controlPort, guint workerID,
        OnConnectedFunc onConnected, gpointer onConnectedArg);
void torflowtorctlclient_free(TorFlowTorCtlClient* torctl);

/* controller commands with callbacks when they complete */
void torflowtorctlclient_commandAuthenticate(TorFlowTorCtlClient* torctl,
        OnAuthenticatedFunc onAuthenticated, gpointer onAuthenticatedArg);
void torflowtorctlclient_commandGetBootstrapStatus(TorFlowTorCtlClient* torctl,
        OnBootstrappedFunc onBootstrapped, gpointer onBootstrappedArg);
void torflowtorctlclient_commandGetDescriptorInfo(TorFlowTorCtlClient* torctl,
        OnDescriptorsReceivedFunc onDescriptorsReceived, gpointer onDescriptorsReceivedArg);
void torflowtorctlclient_commandBuildNewCircuit(TorFlowTorCtlClient* torctl, gchar* path,
        OnCircuitBuiltFunc onCircuitBuilt, gpointer onCircuitBuiltArg);
void torflowtorctlclient_commandAttachStreamToCircuit(TorFlowTorCtlClient* torctl, gint streamID, gint circuitID,
        OnStreamSucceededFunc onStreamSucceeded, gpointer onStreamSucceededArg);

void torflowtorctlclient_setNewStreamCallback(TorFlowTorCtlClient* torctl, in_port_t clientSocksPort,
        OnStreamNewFunc onStreamNew, gpointer onStreamNewArg);

/* controller commands without callbacks */
void torflowtorctlclient_commandSetupTorConfig(TorFlowTorCtlClient* torctl);
void torflowtorctlclient_commandEnableEvents(TorFlowTorCtlClient* torctl);
void torflowtorctlclient_commandDisableEvents(TorFlowTorCtlClient* torctl);

void torflowtorctlclient_commandCloseCircuit(TorFlowTorCtlClient* torctl, gint crcuitID);

/* other funcs */
const gchar* torflowtorctlclient_getCurrentPath(TorFlowTorCtlClient* tfb);

#endif /* SRC_TORFLOW_TORFLOW_TOR_CONTROL_CLIENT_H_ */
