/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowProberInternal {
	gint workerID;
	gint numWorkers;
	gint pausetime;
	gint measurementCircID;
	gint measurementStreamID;
	gboolean measurementStreamSucceeded;
	gint downloadSD;
	gboolean initialized;
	TorFlowManager* tfm;
	TorFlowSlice* currentSlice;
	TorFlowFileServer* fileserver;
	GRand* rand;
};

typedef struct _TimeoutData {
	TorFlowProber* tfp;
	gint measurementCircID;
} TimeoutData;

static void _torflowprober_downloadFile(TorFlowProber* tfp) {
	g_assert(tfp);
	g_assert(tfp->internal->currentSlice);
	torflow_startDownload((TorFlow*)tfp, tfp->internal->downloadSD, tfp->internal->currentSlice->filename);
}

static gchar* _torflowprober_selectNewPath(TorFlowProber* tfp) {
    g_assert(tfp);

    gint exitMinCounted = G_MAXINT; //minimum number of counts for any one exit
    gint numExits = 0; //number of exits whose count is this minimum
    gint entryMinCounted = G_MAXINT; //minimum number of counts for any one entry
    gint numEntries = 0; //number of entries whose count is this minimum

    gint unmeasured = 0; //unmeasured relays
    gint partial = 0; //partially measured relays
    gint measured = 0; //fully measured relays
    gint finishedRequiredProbes = 0; //num probes that we finished
    gint totalRequiredProbes = 0; //total number of probes needed to measure all relays

    /* Make a pass to count the number of entries/exits with the lowest
     * measurement count, then select one of those at random from each
     * category. */
    GSList* currentNode = tfp->internal->currentSlice->relays;
    while (currentNode) {
        TorFlowRelay* current = currentNode->data;
        if (current->exit) {
            if (current->measureCount == exitMinCounted) {
                numExits++;
            } else if (current->measureCount < exitMinCounted) {
                exitMinCounted = current->measureCount;
                numExits = 1;
            }
        } else {
            if (current->measureCount == entryMinCounted) {
                numEntries++;
            } else if (current->measureCount < entryMinCounted) {
                entryMinCounted = current->measureCount;
                numEntries = 1;
            }
        }
        //counting for the sake of logging progress
        totalRequiredProbes += MEASUREMENTS_PER_SLICE;
        finishedRequiredProbes += MIN(MEASUREMENTS_PER_SLICE, current->measureCount);
        if (current->measureCount == 0) {
            unmeasured++;
        } else if (current->measureCount >= MEASUREMENTS_PER_SLICE) {
            measured++;
        } else {
            partial++;
        }

        currentNode = g_slist_next(currentNode);
    }
    int all = unmeasured+measured+partial;
    if (!numExits) {
        tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_WARNING, tfp->_tf._base.id,
                "No exits in slice %u - skipping slice", tfp->internal->currentSlice->sliceNumber);
        return NULL;
    } else if (!numEntries) {
        tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_WARNING, tfp->_tf._base.id,
                "No entries in slice %u - skipping slice", tfp->internal->currentSlice->sliceNumber);
        return NULL;
    } else if (MEASUREMENTS_PER_SLICE == 1) {
        tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_MESSAGE, tfp->_tf._base.id,
                "Slice %i progress (measured/total): %i/%i (%i/%i probes complete)",
                tfp->internal->currentSlice->sliceNumber,
                measured, all, finishedRequiredProbes, totalRequiredProbes);
    } else {
        tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_MESSAGE, tfp->_tf._base.id,
                "Slice %i progress (measured/total): %i/%i (%i partially measured, %i/%i probes complete)",
                tfp->internal->currentSlice->sliceNumber,
                measured, all, partial, finishedRequiredProbes, totalRequiredProbes);
    }

    //choose uniformly from all entries and exits with the lowest measure count.
    gint exitPicked = g_rand_int_range(tfp->internal->rand, 0, numExits);
    gint entryPicked = g_rand_int_range(tfp->internal->rand, 0, numEntries);

    //Make another pass to find the chosen exit and entry
    currentNode = tfp->internal->currentSlice->relays;
    while (currentNode) {
        TorFlowRelay* current = currentNode->data;
        if (current->exit && current->measureCount == exitMinCounted) {
            if (exitPicked == 0) {
                tfp->internal->currentSlice->currentExitRelay = current;
            }
            exitPicked--;
        } else if (!current->exit && current->measureCount == entryMinCounted) {
            if (entryPicked == 0) {
                tfp->internal->currentSlice->currentEntryRelay = current;
            }
            entryPicked--;
        }
        currentNode = g_slist_next(currentNode);
    }

    return g_strconcat(tfp->internal->currentSlice->currentEntryRelay->nickname->str, ",", tfp->internal->currentSlice->currentExitRelay->nickname->str, NULL);
}

static void _torflowprober_startNextProbe(TorFlowProber* tfp) {
    if(tfp->internal->currentSlice) {
        gchar* path = _torflowprober_selectNewPath(tfp);
        gboolean success = torflowbase_buildNewMeasurementCircuit(&tfp->_tf._base, path);
    }
}

void torflowprober_continue(TorFlowProber* tfp) {
    tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
            "Ready to start probing; getting next slice from manager");

    tfp->internal->currentSlice = torflowmanager_getNextSlice(tfp->internal->tfm);
    _torflowprober_startNextProbe(tfp);
}

static void _torflowprober_onBootstrapComplete(TorFlowProber* tfp) {
    g_assert(tfp);
    torflowprober_continue(tfp);
}

static gboolean _torflowprober_isDoneWithCurrentSlice(TorFlowProber* tfp) {
    // See if we are done with this slice
    GSList* currentNode = tfp->internal->currentSlice->relays;
    while (currentNode) {
        TorFlowRelay* current = currentNode->data;
        if (current->measureCount < MEASUREMENTS_PER_SLICE) {
            return FALSE;
        }
        currentNode = g_slist_next(currentNode);
    }
    return TRUE;
}

static void _torflowprober_startNextProbeCallback(TorFlowProber* tfp) {
	g_assert(tfp);
	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Probe Complete, Building Next Circuit");

	/* clear out last circuit */
	if (tfp->internal->measurementCircID) {
		torflowbase_closeCircuit((TorFlowBase*)tfp, tfp->internal->measurementCircID);
	}
	tfp->internal->measurementCircID = 0;
	tfp->internal->measurementStreamID = 0;

	if(_torflowprober_isDoneWithCurrentSlice(tfp)) {
	    // report results back to manager and let him handle it
	    TorFlowSlice* slice = tfp->internal->currentSlice;
	    tfp->internal->currentSlice = NULL;
	    torflowmanager_notifySliceMeasured(tfp->internal->tfm, slice);
	    torflowprober_continue(tfp);
	} else {
        // If not done, build new circuit and stop worrying
	    _torflowprober_startNextProbe(tfp);
	}
}

static void _torflowprober_recordTimeout(TorFlowProber* tfp) {
    g_assert(tfp);

    /* This is a buggy behavior deliberately carried over from TorFlow, where failed circuits are
     * counted as a "successful measurement" but have no bearing on the stats.
     * However, we do not replicate it if this would likely cause a relay to be skipped entirely.
     */
    if (MEASUREMENTS_PER_SLICE > 1) {
        tfp->internal->currentSlice->currentEntryRelay->measureCount++;
        tfp->internal->currentSlice->currentExitRelay->measureCount++;
    }
}

static void _torflowprober_onFileServerTimeout(TorFlowProber* tfp) {
	g_assert(tfp);

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_DEBUG, tfp->_tf._base.id,
			"Connection Failed");

	_torflowprober_recordTimeout(tfp);

	/* do another probe now */
	_torflowprober_startNextProbeCallback(tfp);
}

static void _torflowprober_onDownloadTimeout(TimeoutData* td) {
	g_assert(td && td->tfp);

	if(td->measurementCircID == td->tfp->internal->measurementCircID) {
		td->tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_MESSAGE, td->tfp->_tf._base.id,
				"Downloading over circ %i timed out", td->measurementCircID);
		_torflowprober_recordTimeout(td->tfp);

		/* do another probe now */
		_torflowprober_startNextProbeCallback(td->tfp);
	}
	g_free(td);
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
		TimeoutData* td = g_new0(TimeoutData, 1);
		td->tfp = tfp;
		td->measurementCircID = tfp->internal->measurementCircID;
		tfp->_tf._base.scbf((ShadowPluginCallbackFunc)_torflowprober_onDownloadTimeout,
			td, 1000*DOWNLOAD_TIMEOUT);
	}
}

void _torflowprober_recordMeasurement(TorFlowProber* tfb, gint contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
    g_assert(tfb);

    TorFlowRelay* entry = tfb->internal->currentSlice->currentEntryRelay;
    entry->t_rtt = g_slist_prepend(entry->t_rtt, GINT_TO_POINTER(roundTripTime));
    entry->t_payload = g_slist_prepend(entry->t_payload, GINT_TO_POINTER(payloadTime));
    entry->t_total = g_slist_prepend(entry->t_total, GINT_TO_POINTER(totalTime));
    entry->bytesPushed = g_slist_prepend(entry->bytesPushed, GINT_TO_POINTER(contentLength));
    entry->measureCount++;

    TorFlowRelay* exit = tfb->internal->currentSlice->currentExitRelay;
    exit->t_rtt = g_slist_prepend(exit->t_rtt, GINT_TO_POINTER(roundTripTime));
    exit->t_payload = g_slist_prepend(exit->t_payload, GINT_TO_POINTER(payloadTime));
    exit->t_total = g_slist_prepend(exit->t_total, GINT_TO_POINTER(totalTime));
    exit->bytesPushed = g_slist_prepend(exit->bytesPushed, GINT_TO_POINTER(contentLength));
    exit->measureCount++;
}

static void _torflowprober_onFileDownloadComplete(TorFlowProber* tfp, gint contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime) {
	g_assert(tfp);

	tfp->_tf._base.slogf(SHADOW_LOG_LEVEL_MESSAGE, tfp->_tf._base.id,
			"Probe complete. path=%s bytes=%i time-to: rtt=%zu payload=%zu total=%zu",
			torflowbase_getCurrentPath((TorFlowBase*) tfp),
			contentLength, roundTripTime, payloadTime, totalTime);

	_torflowprober_recordMeasurement(tfp, contentLength, roundTripTime, payloadTime, totalTime);

	/* do another probe now */
	_torflowprober_startNextProbeCallback(tfp);
}

static void _torflowprober_onFree(TorFlowProber* tfp) {
	g_assert(tfp);

	if(tfp->internal->fileserver) {
	    torflowfileserver_unref(tfp->internal->fileserver);
	}

	g_rand_free(tfp->internal->rand);

	g_free(tfp->internal);
}

TorFlowProber* torflowprober_new(ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf,
        TorFlowManager* tfm, gint workerID, gint numWorkers,
		gint pausetime, in_port_t controlPort, in_port_t socksPort, TorFlowFileServer* fileserver) {

	TorFlowEventCallbacks events;
	memset(&events, 0, sizeof(TorFlowEventCallbacks));
	events.onBootstrapComplete = (BootstrapCompleteFunc) _torflowprober_onBootstrapComplete;
	events.onDescriptorsReceived = NULL;
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
	tfp->internal->initialized = FALSE;
	tfp->internal->tfm = tfm;
	tfp->internal->rand = g_rand_new();

	if(fileserver) {
        torflowfileserver_ref(fileserver);
        tfp->internal->fileserver = fileserver;
	}

	torflow_init((TorFlow*)tfp, &events, slogf, scbf, controlPort, socksPort, workerID);

	return tfp;
}

void torflowprober_start(TorFlowProber* tfp) {
    torflow_start(&tfp->_tf);
}
