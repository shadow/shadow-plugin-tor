/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_FILE_CLIENT_H_
#define SRC_TORFLOW_TORFLOW_FILE_CLIENT_H_

#include <netdb.h>

#include <glib.h>

typedef struct _TorFlowFileClient TorFlowFileClient;

typedef void (*OnFileClientCompleteFunc)(gpointer data, gboolean isSuccess, gsize contentLength,
        gsize roundTripTime, gsize payloadTime, gsize totalTime);

TorFlowFileClient* torflowfileclient_new(TorFlowEventManager* manager, guint workerID,
        in_port_t socksPort, TorFlowPeer* fileServer, gsize transferSizeBytes,
        OnFileClientCompleteFunc onFileClientComplete, gpointer onFileClientCompleteArg);
void torflowfileclient_free(TorFlowFileClient* client);

in_port_t torflowfileclient_getHostClientSocksPort(TorFlowFileClient* client);

#endif /* SRC_TORFLOW_TORFLOW_FILE_CLIENT_H_ */
