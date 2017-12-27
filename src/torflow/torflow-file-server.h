/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_FILE_SERVER_H_
#define SRC_TORFLOW_TORFLOW_FILE_SERVER_H_

#include <glib.h>

typedef struct _TorFlowFileServer TorFlowFileServer;

typedef void (*OnFileServerCompleteFunc)(gpointer data, gint descriptor, gboolean isSuccess, gsize bytesSent);

TorFlowFileServer* torflowfileserver_new(TorFlowEventManager* manager, guint workerID, gint descriptor,
        OnFileServerCompleteFunc onFileServerComplete, gpointer onFileServerCompleteArg);
void torflowfileserver_free(TorFlowFileServer* server);

#endif /* SRC_TORFLOW_TORFLOW_FILE_SERVER_H_ */
