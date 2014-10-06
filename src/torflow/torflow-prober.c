/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

#define MEASUREMENTS_PER_SLICE 5

struct _TorFlowProberInternal {
	TorFlowSybil* sybil;
	GSList* relays;
	gdouble minPct;
	gdouble maxPct;
	gint thinktime;
	gint sliceSize;
	gdouble nodeCap;
	gint currSlice;
	gint numRelays;
	gint minSlice;
	gint maxSlice;
};

static void _torflowprober_downloadFile(TorFlowProber* tfp) {
	g_assert(tfp);
	gdouble percentile = tfp->internal->sliceSize * tfp->internal->currSlice / (gdouble)(tfp->internal->numRelays);
	gchar* fname;

#ifdef SMALLFILES
	/* Have torflow download smaller files than the real Torflow does.
	 * This improves actual running time but should have little effect on
	 * simulated timings. */
	if (percentile < 0.25) {
		fname = "/256KiB.urnd";
	} else if (percentile < 0.5) {
		fname = "/128KiB.urnd";
	} else if (percentile < 0.75) {
		fname = "/64KiB.urnd";
	} else {
		fname = "/32KiB.urnd";
	}
#else
	/* This is based not on the spec, but on the file read by TorFlow,
	 * NetworkScanners/BwAuthority/data/bwfiles. */
	if (percentile < 0.01) {
		fname = "/4MiB.urnd";
	} else if (percentile < 0.07) {
		fname = "/2MiB.urnd";
	} else if (percentile < 0.23) {
		fname = "/1MiB.urnd";
	} else if (percentile < 0.53) {
		fname = "/512KiB.urnd";
	} else if (percentile < 0.82) {
		fname = "/256KiB.urnd";
	} else if (percentile < 0.95) {
		fname = "/128KiB.urnd";
	} else if (percentile < 0.99) {
		fname = "/64KiB.urnd";
	} else {
		fname = "/32KiB.urnd";
	}
#endif
	torflow_startDownload((TorFlow*)tfp, tfp->internal->sybil->downloadSD, fname);
}

static void _torflowprober_onBootstrapComplete(TorFlowProber* tfp) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Ready to Measure, Getting Descriptors");
	torflowbase_requestInfo((TorFlowBase*)tfp);
}


static void _torflowprober_startNextProbeCallback(TorFlowProber* tfp) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Probe Complete, Building Next Circuit");
	torflowbase_closeCircuit((TorFlowBase*)tfp, tfp->internal->sybil->measurementCircID);
	tfp->internal->sybil->measurementCircID = 0;
	tfp->internal->sybil->measurementStreamID = 0;

	// See if we are done with this slice
	GSList* currentNode = g_slist_nth(tfp->internal->relays,
					tfp->internal->sliceSize * tfp->internal->currSlice);
	gint i = 0;
	gboolean doneSlice = TRUE;	
	while (currentNode && i < tfp->internal->sliceSize) {
		TorFlowRelay* current = currentNode->data;
		if (current->measureCount < MEASUREMENTS_PER_SLICE) {
			doneSlice = FALSE;
			break;
		}
		i++;
		currentNode = g_slist_next(currentNode);
	}

	// If not done, build new circuit and stop worrying
	if (!doneSlice) {
		torflowbase_buildNewMeasurementCircuit((TorFlowBase*)tfp, tfp->internal->sliceSize, tfp->internal->currSlice);
	}

	// If we're done with this slice, see if we're done with all slices
	while (doneSlice) {
		torflowbase_reportMeasurements((TorFlowBase*) tfp, tfp->internal->sliceSize, tfp->internal->currSlice);
		tfp->internal->currSlice++;
		if (tfp->internal->currSlice >= tfp->internal->maxSlice) {
			// Do aggregation step
			torflowbase_aggregateToFile((TorFlowBase*) tfp, tfp->internal->nodeCap);

			// Prepare for next measurement and schedule it for the future
			//g_slist_foreach(tfp->internal->relays, (GFunc)torflowutil_resetRelay, NULL);
			tfp->internal->relays = NULL;
			tfp->internal->currSlice = tfp->internal->minSlice;
			tfp->_tf._base.scbf((BootstrapCompleteFunc)_torflowprober_onBootstrapComplete,
					tfp, tfp->internal->thinktime*1000);
			return;
		} else {
			// this call will return false and make us loop only if the new slice is bad
			doneSlice = !torflowbase_buildNewMeasurementCircuit((TorFlowBase*)tfp, tfp->internal->sliceSize, tfp->internal->currSlice);
		}
	}
}

static void _torflowprober_onDescriptorsReceived(TorFlowProber* tfp, GSList* relayList) {
	g_assert(tfp);
	tfp->internal->relays = relayList;
	tfp->internal->numRelays = g_slist_length(relayList);
	gint numSlices = (tfp->internal->numRelays + tfp->internal->sliceSize - 1) / tfp->internal->sliceSize;

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Descriptors Received, Building First Circuit");

	gboolean goodSlice;
	// Calculate the first slice this worker can work on (subtract one to correct for addition in loop)
	tfp->internal->minSlice = (gint)(numSlices * tfp->internal->minPct);
	tfp->internal->maxSlice = (gint)(numSlices * tfp->internal->maxPct);
	tfp->internal->currSlice =  tfp->internal->minSlice - 1;

	// Create the first circuit; skip the slice if the build function returns FALSE
	do {
		tfp->internal->currSlice++;
		goodSlice = torflowbase_buildNewMeasurementCircuit((TorFlowBase*)tfp, tfp->internal->sliceSize, tfp->internal->currSlice);
	} while (!goodSlice && tfp->internal->currSlice + 1 < tfp->internal->maxSlice);

	// Report an odd corner case where all slices are bad
	if (tfp->internal->currSlice >= tfp->internal->maxSlice) {
		tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_WARNING, tfp->_tf._base.id,
			"No measureable slices for TorFlow!");
	}
}

static void _torflowprober_onMeasurementCircuitBuilt(TorFlowProber* tfp, gint circid) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Circuit Built, Starting Download");
	tfp->internal->sybil->measurementCircID = circid;
	tfp->internal->sybil->downloadSD = torflow_newDownload((TorFlow*)tfp, tfp->internal->sybil->fileserver);
}

static void _torflowprober_onStreamNew(TorFlowProber* tfp, gint streamid, gint circid, gchar* targetAddress, gint targetPort) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"New Stream, Attaching to Circuit");
	if(tfp->internal->sybil->measurementCircID) {
		/* attach to our measurement circuit */
		tfp->internal->sybil->measurementStreamID = streamid;
		torflowbase_attachStreamToCircuit((TorFlowBase*)tfp, tfp->internal->sybil->measurementStreamID, tfp->internal->sybil->measurementCircID);
	} else {
		/* let tor choose a circuit */
		torflowbase_attachStreamToCircuit((TorFlowBase*)tfp, streamid, 0);
	}
}

static void _torflowprober_onStreamSucceeded(TorFlowProber* tfp, gint streamid, gint circid, gchar* targetAddress, gint targetPort) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Stream Attach Succeeded");

	if(circid == tfp->internal->sybil->measurementCircID &&
			streamid == tfp->internal->sybil->measurementStreamID) {
		tfp->internal->sybil->measurementStreamSucceeded = TRUE;
	}
}

static void _torflowprober_onFileServerConnected(TorFlowProber* tfp, gint socksd) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"FileServer Connected");

	g_assert(tfp->internal->sybil->measurementStreamSucceeded);
	if(socksd == tfp->internal->sybil->downloadSD) {
		_torflowprober_downloadFile(tfp);
	}
}

static void _torflowprober_onFileDownloadComplete(TorFlowProber* tfp, gint contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
	g_assert(tfp);

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_MESSAGE, tfp->_tf._base.id,
			"probe-ping %i bytes rtt=%zu payload=%zu total=%zu",
			contentLength, roundTripTime, payloadTime, totalTime);

	torflowbase_recordMeasurement((TorFlowBase*) tfp, contentLength, roundTripTime, payloadTime, totalTime);

	/* do another probe now */
	_torflowprober_startNextProbeCallback(tfp);
}

static void _torflowprober_onFree(TorFlowProber* tfp) {
	g_assert(tfp);

	torflowbase_reportMeasurements((TorFlowBase*) tfp, tfp->internal->sliceSize, tfp->internal->currSlice);

	if(tfp->internal->sybil->fileserver) {
		g_free(tfp->internal->sybil->fileserver->name);
		g_free(tfp->internal->sybil->fileserver->addressString);
		g_free(tfp->internal->sybil->fileserver->portString);
		g_free(tfp->internal->sybil->fileserver);
	}

	g_free(tfp->internal->sybil);
	g_free(tfp->internal);
}

TorFlowProber* torflowprober_new(ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf,
		gdouble minPct, gdouble maxPct,
		gint thinktime, gint sliceSize, gdouble nodeCap, 
		gint controlPort, gint socksPort, TorFlowFileServer* fileserver) {

	TorFlowEventCallbacks events;
	memset(&events, 0, sizeof(TorFlowEventCallbacks));
	events.onBootstrapComplete = (BootstrapCompleteFunc) _torflowprober_onBootstrapComplete;
	events.onDescriptorsReceived = (DescriptorsReceivedFunc) _torflowprober_onDescriptorsReceived;
	events.onMeasurementCircuitBuilt = (MeasurementCircuitBuiltFunc) _torflowprober_onMeasurementCircuitBuilt;
	events.onStreamNew = (StreamNewFunc) _torflowprober_onStreamNew;
	events.onStreamSucceeded = (StreamSucceededFunc) _torflowprober_onStreamSucceeded;
	events.onFileServerConnected = (FileServerConnectedFunc) _torflowprober_onFileServerConnected;
	events.onFileDownloadComplete = (FileDownloadCompleteFunc) _torflowprober_onFileDownloadComplete;
	events.onFree = (FreeFunc) _torflowprober_onFree;

	TorFlowProber* tfp = g_new0(TorFlowProber, 1);
	tfp->internal = g_new0(TorFlowProberInternal, 1);
	tfp->internal->minPct = minPct;
	tfp->internal->maxPct = maxPct;
	tfp->internal->thinktime = thinktime;
	tfp->internal->sliceSize = sliceSize;
	tfp->internal->nodeCap = nodeCap;
	tfp->internal->currSlice = 0;
	tfp->internal->sybil = g_new0(TorFlowSybil, 1);
	tfp->internal->sybil->fileserver = fileserver;

	torflow_init((TorFlow*)tfp, &events, slogf, scbf, controlPort, socksPort);

	return tfp;
}
