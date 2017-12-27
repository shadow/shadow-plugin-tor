/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_AUTHORITY_H_
#define SRC_TORFLOW_TORFLOW_AUTHORITY_H_

#include "torflow-config.h"
#include "torflow-event-manager.h"

typedef struct _TorFlowAuthority TorFlowAuthority;

TorFlowAuthority* torflowauthority_new(TorFlowConfig* config, TorFlowEventManager* manager);
void torflowauthority_free(TorFlowAuthority* authority);

#endif /* SRC_TORFLOW_TORFLOW_AUTHORITY_H_ */
