/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowProberInternal {
	GSList* relays;
	gint workerID;
	gint numWorkers;
	gint pausetime;
	gint sliceSize;
	gint currSlice;
	gint numRelays;
	gint minSlice;
	gint maxSlice;
	gint measurementCircID;
	gint measurementStreamID;
	gboolean measurementStreamSucceeded;
	gint downloadSD;
	gboolean initialized;
	TorFlowFileServer* fileserver;
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
	torflow_startDownload((TorFlow*)tfp, tfp->internal->downloadSD, fname);
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
	torflowbase_closeCircuit((TorFlowBase*)tfp, tfp->internal->measurementCircID);
	tfp->internal->measurementCircID = 0;
	tfp->internal->measurementStreamID = 0;

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
		// Report stats to aggregator and to log file
		torflowaggregator_reportMeasurements(tfp->_tf.tfa, tfp->internal->relays, tfp->internal->sliceSize, tfp->internal->currSlice);
		torflowbase_reportMeasurements((TorFlowBase*) tfp, tfp->internal->sliceSize, tfp->internal->currSlice);

		tfp->internal->currSlice++;
		if (tfp->internal->currSlice >= tfp->internal->maxSlice) {
			// Prepare for next measurement and schedule it for the future
			//g_slist_foreach(tfp->internal->relays, (GFunc)torflowutil_resetRelay, NULL);
			tfp->internal->relays = NULL;
			tfp->internal->currSlice = tfp->internal->minSlice;
			tfp->_tf._base.scbf((BootstrapCompleteFunc)_torflowprober_onBootstrapComplete,
					tfp, tfp->internal->pausetime*1000);
			return;
		} else {
			// this call will return false and make us loop only if the new slice is bad
			doneSlice = !torflowbase_buildNewMeasurementCircuit((TorFlowBase*)tfp, tfp->internal->sliceSize, tfp->internal->currSlice);
		}
	}
}

static void _torflowprober_onDescriptorsReceived(TorFlowProber* tfp, GSList* relayList) {
	g_assert(tfp);

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Descriptors Received, Building First Circuit");

	tfp->internal->relays = relayList;
	tfp->internal->numRelays = g_slist_length(relayList);

	// If not initialized, load initial advertised values
	if(!tfp->internal->initialized) {
		torflowaggregator_initialLoad(tfp->_tf.tfa, tfp->internal->relays);
		tfp->internal->initialized = TRUE;
	}

	// Calculate the first slice this worker can work on
	gint numSlices = (tfp->internal->numRelays + tfp->internal->sliceSize - 1) / tfp->internal->sliceSize;
	gdouble minPct = ((gdouble)tfp->internal->workerID)/tfp->internal->numWorkers;
	gdouble maxPct = ((gdouble)tfp->internal->workerID+1.0)/tfp->internal->numWorkers;
	tfp->internal->minSlice = (gint)(numSlices * minPct);
	tfp->internal->maxSlice = (gint)(numSlices * maxPct);
	tfp->internal->currSlice =  tfp->internal->minSlice - 1;

	gboolean goodSlice;
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
	tfp->internal->measurementCircID = circid;
	tfp->internal->downloadSD = torflow_newDownload((TorFlow*)tfp, tfp->internal->fileserver);
}

static void _torflowprober_onStreamNew(TorFlowProber* tfp, gint streamid, gint circid,
        gchar* targetAddress, gint targetPort, gchar* sourceAddress, gint sourcePort) {
	g_assert(tfp);

	/* if this stream is not from our socks port, dont mess with it */
	if(sourcePort != (gint)torflow_getHostBoundSocksPort((TorFlow*) tfp)) {
	    tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_INFO, tfp->_tf._base.id,
	                "New stream %i on circ %i from %s:%i to %s:%i is not ours, ignoring it",
	                streamid, circid, sourceAddress, sourcePort, targetAddress, targetPort);
	    return;
	}

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"New Stream, Attaching to Circuit");
	if(tfp->internal->measurementCircID) {
		/* attach to our measurement circuit */
		tfp->internal->measurementStreamID = streamid;
		torflowbase_attachStreamToCircuit((TorFlowBase*)tfp, tfp->internal->measurementStreamID, tfp->internal->measurementCircID);
	} else {
		/* let tor choose a circuit */
		torflowbase_attachStreamToCircuit((TorFlowBase*)tfp, streamid, 0);
	}
}

static void _torflowprober_onStreamSucceeded(TorFlowProber* tfp, gint streamid, gint circid, gchar* targetAddress, gint targetPort) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Stream Attach Succeeded");

	if(circid == tfp->internal->measurementCircID &&
			streamid == tfp->internal->measurementStreamID) {
		tfp->internal->measurementStreamSucceeded = TRUE;
	}
}

static void _torflowprober_onFileServerConnected(TorFlowProber* tfp, gint socksd) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"FileServer Connected");

	g_assert(tfp->internal->measurementStreamSucceeded);
	if(socksd == tfp->internal->downloadSD) {
		_torflowprober_downloadFile(tfp);
	}
}

static void _torflowprober_onFileServerTimeout(TorFlowProber* tfp) {
	g_assert(tfp);

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Connection Failed");

	torflowbase_recordTimeout((TorFlowBase*) tfp);

	/* do another probe now */
	_torflowprober_startNextProbeCallback(tfp);
}

static void _torflowprober_onFileDownloadComplete(TorFlowProber* tfp, gint contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
	g_assert(tfp);

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_MESSAGE, tfp->_tf._base.id,
			"probe-complete path=%s bytes=%i time-to: rtt=%zu payload=%zu total=%zu",
			torflowbase_getCurrentPath((TorFlowBase*) tfp),
			contentLength, roundTripTime, payloadTime, totalTime);

	torflowbase_recordMeasurement((TorFlowBase*) tfp, contentLength, roundTripTime, payloadTime, totalTime);

	/* do another probe now */
	_torflowprober_startNextProbeCallback(tfp);
}

static void _torflowprober_onFree(TorFlowProber* tfp) {
	g_assert(tfp);

	torflowbase_reportMeasurements((TorFlowBase*) tfp, tfp->internal->sliceSize, tfp->internal->currSlice);

	if(tfp->internal->fileserver) {
	    torflowfileserver_unref(tfp->internal->fileserver);
	}

	g_free(tfp->internal);
}

TorFlowProber* torflowprober_new(ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf,
		TorFlowAggregator* tfa, gint workerID, gint numWorkers,
		gint pausetime, gint sliceSize,
		in_port_t controlPort, in_port_t socksPort, TorFlowFileServer* fileserver) {

	TorFlowEventCallbacks events;
	memset(&events, 0, sizeof(TorFlowEventCallbacks));
	events.onBootstrapComplete = (BootstrapCompleteFunc) _torflowprober_onBootstrapComplete;
	events.onDescriptorsReceived = (DescriptorsReceivedFunc) _torflowprober_onDescriptorsReceived;
	events.onMeasurementCircuitBuilt = (MeasurementCircuitBuiltFunc) _torflowprober_onMeasurementCircuitBuilt;
	events.onStreamNew = (StreamNewFunc) _torflowprober_onStreamNew;
	events.onStreamSucceeded = (StreamSucceededFunc) _torflowprober_onStreamSucceeded;
	events.onFileServerConnected = (FileServerConnectedFunc) _torflowprober_onFileServerConnected;
	events.onFileServerTimeout = (FileServerTimeoutFunc) _torflowprober_onFileServerTimeout;
	events.onFileDownloadComplete = (FileDownloadCompleteFunc) _torflowprober_onFileDownloadComplete;
	events.onFree = (FreeFunc) _torflowprober_onFree;

	TorFlowProber* tfp = g_new0(TorFlowProber, 1);
	tfp->internal = g_new0(TorFlowProberInternal, 1);
	tfp->internal->workerID = workerID;
	tfp->internal->numWorkers = numWorkers;
	tfp->internal->pausetime = pausetime;
	tfp->internal->sliceSize = sliceSize;
	tfp->internal->currSlice = 0;
	tfp->internal->initialized = FALSE;

	if(fileserver) {
        torflowfileserver_ref(fileserver);
        tfp->internal->fileserver = fileserver;
	}

	torflow_init((TorFlow*)tfp, &events, slogf, scbf, tfa, controlPort, socksPort, workerID);

	return tfp;
}
