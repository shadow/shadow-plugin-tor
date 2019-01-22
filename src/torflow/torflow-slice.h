/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_SLICE_H_
#define SRC_TORFLOW_TORFLOW_SLICE_H_

#include <glib.h>

typedef struct _TorFlowSlice TorFlowSlice;

TorFlowSlice* torflowslice_new(guint sliceID, gdouble percentile, guint numProbesPerRelay);
void torflowslice_free(TorFlowSlice* slice);

void torflowslice_addRelay(TorFlowSlice* slice, TorFlowRelay* relay);
gboolean torflowslice_chooseRelayPair(TorFlowSlice* slice, gchar** entryRelayIdentity, gchar** exitRelayIdentity);

void torflowslice_logStatus(TorFlowSlice* slice);

guint torflowslice_getLength(TorFlowSlice* slice);
guint torflowslice_getNumProbesRemaining(TorFlowSlice* slice);
gsize torflowslice_getTransferSize(TorFlowSlice* slice);

gboolean torflowslice_contains(TorFlowSlice* slice, const gchar* relayID);

#endif /* SRC_TORFLOW_TORFLOW_SLICE_H_ */
