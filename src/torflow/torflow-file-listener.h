/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_FILE_LISTENER_H_
#define SRC_TORFLOW_TORFLOW_FILE_LISTENER_H_

#include <netdb.h>

#include <glib.h>

typedef struct _TorFlowFileListener TorFlowFileListener;

TorFlowFileListener* torflowfilelistener_new(TorFlowEventManager* manager, guint workerID, in_port_t listenPort);
void torflowfilelistener_free(TorFlowFileListener* listener);

#endif /* SRC_TORFLOW_TORFLOW_FILE_LISTENER_H_ */
