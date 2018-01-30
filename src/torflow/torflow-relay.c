/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowRelay {
    gchar* nickname;
    gchar* identity;

    gboolean isRunning;
    gboolean isFast;
    gboolean isExit;

    guint v3Bandwidth;
    guint descriptorBandwidth;
    guint advertisedBandwidth;

    GSList* transferTimes;
    GSList* transferSizes;
};

static guint _torflowrelay_computeBandwidth(TorFlowRelay* relay, guint useLastNumMeasurements, gdouble cutoff) {
    gdouble bandwidth = 0.0;
    GSList* currentTimeItem = relay->transferTimes;
    GSList* currentSizeItem = relay->transferSizes;
    guint i = 0;

    while (currentTimeItem && currentSizeItem && i < useLastNumMeasurements) {
        gsize millis = GPOINTER_TO_SIZE(currentTimeItem->data);
        gsize bytes = GPOINTER_TO_SIZE(currentSizeItem->data);

        gdouble currentBandwidth = (gdouble) bytes / (gdouble) millis;
        if (currentBandwidth >= cutoff) {
            bandwidth += currentBandwidth;
            i++;
        }

        currentTimeItem = g_slist_next(currentTimeItem);
        currentSizeItem = g_slist_next(currentSizeItem);
    }

    if (i > 0) {
        gdouble meanBandwidth = bandwidth / i;
        return (guint) meanBandwidth;
    } else {
        return 0;
    }
}

TorFlowRelay* torflowrelay_new(gchar* nickname, gchar* identity) {
    TorFlowRelay* relay = g_new0(TorFlowRelay, 1);

    relay->nickname = nickname;
    relay->identity = identity;

    return relay;
}

void torflowrelay_free(TorFlowRelay* relay) {
    g_assert(relay);

    if(relay->nickname) {
        g_free(relay->nickname);
    }
    if(relay->identity) {
        g_free(relay->identity);
    }

    g_free(relay);
}

gboolean torflowrelay_isMeasureable(TorFlowRelay* relay) {
    g_assert(relay);
    // we should try to measure all relays; Fast and Running flags are only used to compute
    // the fraction of measured relays for logging purposes in the real torflow
    // https://gitweb.torproject.org/torflow.git/tree/NetworkScanners/BwAuthority/aggregate.py#n810
    //return torflowrelay_getIsRunning(relay) ? TRUE : FALSE;
    return TRUE;
}

void torflowrelay_addMeasurement(TorFlowRelay* relay,
        gsize contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
    g_assert(relay);

    relay->transferTimes = g_slist_prepend(relay->transferTimes, GSIZE_TO_POINTER(totalTime));
    relay->transferSizes = g_slist_prepend(relay->transferSizes, GSIZE_TO_POINTER(contentLength));
}

void torflowrelay_getBandwidths(TorFlowRelay* relay, guint useLastNumMeasurements, guint* meanBW, guint* filteredBW) {
    g_assert(relay);

    /* mean bandwidth is the full mean bandwidth, i.e., no minimum required cutoff */
    if(meanBW) {
        *meanBW = _torflowrelay_computeBandwidth(relay, useLastNumMeasurements, 0.0f);
    }
    /* filtered bandwidth implies compute the mean of only the bandwidths above the mean */
    if(filteredBW) {
        gdouble cuttoff = meanBW ? (gdouble)*meanBW : 0.0f;
        *filteredBW = _torflowrelay_computeBandwidth(relay, useLastNumMeasurements, cuttoff);
    }
}

/* Compare function to sort in descending order by bandwidth. */
gint torflowrelay_compare(TorFlowRelay* relayA, TorFlowRelay* relayB){
  return relayB->descriptorBandwidth - relayA->descriptorBandwidth;
}

gint torflowrelay_compareData(TorFlowRelay* relayA, TorFlowRelay* relayB, gpointer userData){
    return torflowrelay_compare(relayA, relayB);
}

gboolean torflowrelay_isEqual(TorFlowRelay* relayA, TorFlowRelay* relayB) {
    return g_str_equal(relayA->identity, relayB->identity);
}

void torflowrelay_setIsRunning(TorFlowRelay* relay, gboolean isRunning) {
    g_assert(relay);
    relay->isRunning = isRunning;
}

void torflowrelay_setIsFast(TorFlowRelay* relay, gboolean isFast) {
    g_assert(relay);
    relay->isFast = isFast;
}

void torflowrelay_setIsExit(TorFlowRelay* relay, gboolean isExit) {
    g_assert(relay);
    relay->isExit = isExit;
}

void torflowrelay_setV3Bandwidth(TorFlowRelay* relay, guint v3Bandwidth) {
    g_assert(relay);
    relay->v3Bandwidth = v3Bandwidth;
}

void torflowrelay_setDescriptorBandwidth(TorFlowRelay* relay, guint descriptorBandwidth) {
    g_assert(relay);
    relay->descriptorBandwidth = descriptorBandwidth;
}

void torflowrelay_setAdvertisedBandwidth(TorFlowRelay* relay, guint advertisedBandwidth) {
    g_assert(relay);
    relay->advertisedBandwidth = advertisedBandwidth;
}

const gchar* torflowrelay_getIdentity(TorFlowRelay* relay) {
    g_assert(relay);
    return (const gchar*)relay->identity;
}

const gchar* torflowrelay_getNickname(TorFlowRelay* relay) {
    g_assert(relay);
    return (const gchar*)relay->nickname;
}

gboolean torflowrelay_getIsRunning(TorFlowRelay* relay) {
    g_assert(relay);
    return relay->isRunning;
}

gboolean torflowrelay_getIsFast(TorFlowRelay* relay) {
    g_assert(relay);
    return relay->isFast;
}

gboolean torflowrelay_getIsExit(TorFlowRelay* relay) {
    g_assert(relay);
    return relay->isExit ;
}

guint torflowrelay_getDescriptorBandwidth(TorFlowRelay* relay) {
    g_assert(relay);
    return relay->descriptorBandwidth;
}

guint torflowrelay_getAdvertisedBandwidth(TorFlowRelay* relay) {
    g_assert(relay);
    return relay->advertisedBandwidth;
}

guint torflowrelay_getV3Bandwidth(TorFlowRelay* relay) {
    g_assert(relay);
    return relay->v3Bandwidth;
}
