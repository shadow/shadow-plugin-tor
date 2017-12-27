/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_RELAY_H_
#define SRC_TORFLOW_TORFLOW_RELAY_H_

#include <glib.h>

typedef struct _TorFlowRelay TorFlowRelay;

TorFlowRelay* torflowrelay_new(gchar* nickname, gchar* identity);
void torflowrelay_free(TorFlowRelay* relay);

gboolean torflowrelay_isMeasureable(TorFlowRelay* relay);
void torflowrelay_addMeasurement(TorFlowRelay* relay,
        gsize contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime);
void torflowrelay_getBandwidths(TorFlowRelay* relay, guint useLastNumMeasurements, guint* meanBW, guint* filteredBW);

gint torflowrelay_compare(TorFlowRelay* relayA, TorFlowRelay* relayB);
gint torflowrelay_compareData(TorFlowRelay* relayA, TorFlowRelay* relayB, gpointer userData);
gboolean torflowrelay_isEqual(TorFlowRelay* relayA, TorFlowRelay* relayB);

void torflowrelay_setIsRunning(TorFlowRelay* relay, gboolean isRunning);
void torflowrelay_setIsFast(TorFlowRelay* relay, gboolean isFast);
void torflowrelay_setIsExit(TorFlowRelay* relay, gboolean isExit);
void torflowrelay_setV3Bandwidth(TorFlowRelay* relay, guint v3Bandwidth);
void torflowrelay_setDescriptorBandwidth(TorFlowRelay* relay, guint descriptorBandwidth);
void torflowrelay_setAdvertisedBandwidth(TorFlowRelay* relay, guint advertisedBandwidth);

const gchar* torflowrelay_getIdentity(TorFlowRelay* relay);
const gchar* torflowrelay_getNickname(TorFlowRelay* relay);
gboolean torflowrelay_getIsRunning(TorFlowRelay* relay);
gboolean torflowrelay_getIsFast(TorFlowRelay* relay);
gboolean torflowrelay_getIsExit(TorFlowRelay* relay);
guint torflowrelay_getDescriptorBandwidth(TorFlowRelay* relay);
guint torflowrelay_getAdvertisedBandwidth(TorFlowRelay* relay);
guint torflowrelay_getV3Bandwidth(TorFlowRelay* relay);

#endif /* SRC_TORFLOW_TORFLOW_RELAY_H_ */
