/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowManager {
    TorFlowBase _base;
    gint baseED;
	gint ed;
	ShadowLogFunc slogf;
	ShadowCreateCallbackFunc scbf;
	TorFlowAggregator* tfa;
	GHashTable* probers;
	gboolean probersStarted;
	gint workers;

    /* info about relays */
	GHashTable* AllRelaysByFingerprint;
	gint slicesize;
	GQueue* currentSlices;
	guint numMeasurableRelaysThisRound;
	guint numMeasurableSlicesThisRound;
	guint numRemainingSlicesThisRound;
	gboolean round1Done;
};

static void _torflowmanager_freeRelay(TorFlowRelay* r) {
    if(r) {
        if(r->nickname) {
            g_string_free(r->nickname, TRUE);
        }
        if(r->identity) {
            g_string_free(r->identity, TRUE);
        }
        if(r->t_rtt) {
            g_slist_free(r->t_rtt);
        }
        if(r->t_payload) {
            g_slist_free(r->t_payload);
        }
        if(r->t_total) {
            g_slist_free(r->t_total);
        }
        if(r->bytesPushed) {
            g_slist_free(r->bytesPushed);
        }
        g_free(r);
    }
}

static void _torflowmanager_freeSlice(TorFlowSlice* slice) {
    if(slice) {
        if(slice->allRelays) {
            /* dont actually free the relay objects because they are owned
             * by the master hash table AllRelaysByFingerprint */
            g_slist_free(slice->allRelays);
        }
        g_free(slice);
    }
}

static gchar* _torflowmanager_getSliceDownloadFileName(TorFlowManager* tfm,
        guint sliceNumber, gint numMeasurableRelays) {
    gdouble percentile = tfm->slicesize * sliceNumber / (gdouble)(numMeasurableRelays);
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
    return fname;
}

TorFlowSlice* torflowmanager_getNextSlice(TorFlowManager* tfm) {
    // called by the prober when it finishes bootstrapping or
    // wants to start measuring another slice
    return g_queue_pop_head(tfm->currentSlices);
}

void torflowmanager_notifySliceMeasured(TorFlowManager* tfm, TorFlowSlice* slice) {
    if(!slice) {
        return;
    }

    // called by the prober when it finishes measuring a slice
    g_assert(tfm->numRemainingSlicesThisRound != 0); // cant complete a slice if they have already all been done!
    tfm->numRemainingSlicesThisRound--;

    tfm->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfm->_base.id,
            "Slice %u measurements are complete! We have %u/%u slices remaining to measure in this round.",
            slice->sliceNumber, tfm->numRemainingSlicesThisRound, tfm->numMeasurableSlicesThisRound);

    // see if we're done with all slices
    gboolean thisRoundIsDone = (tfm->numRemainingSlicesThisRound == 0) ? TRUE : FALSE;
    if(thisRoundIsDone) {
        g_assert(g_queue_get_length(tfm->currentSlices) == 0);
        tfm->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfm->_base.id,
                "We have completed %s round of measurements of %u relays in %u slices.",
                tfm->round1Done ? "another" : "the first",
                tfm->numMeasurableRelaysThisRound, tfm->numMeasurableSlicesThisRound);
        tfm->round1Done = TRUE;
    }

    // Report new slice stats to aggregator and to log file, even if the round is not done
    torflowaggregator_reportMeasurements(tfm->tfa, slice, tfm->round1Done);

    /* aggregator saved all stats from the slice so it is safe to free it now */
    _torflowmanager_freeSlice(slice);

    /* after every round, we should fetch updated descriptors and start the next round */
    if(thisRoundIsDone) {
        tfm->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfm->_base.id,
                    "Reloading descriptors for next round");
        torflowbase_requestInfo(&tfm->_base);
    }
}

static void _torflowmanager_onBootstrapComplete(TorFlowManager* tfm) {
    g_assert(tfm);
    tfm->slogf(SHADOW_LOG_LEVEL_DEBUG, tfm->_base.id,
            "Ready to Measure, Getting Descriptors");
    torflowbase_requestInfo(&tfm->_base);
}

static guint _torflowmanager_parseAndStoreRelays(TorFlowManager* tfm, GQueue* descriptorLines) {
    TorFlowRelay* currentRelay = NULL;

    while(descriptorLines != NULL && g_queue_get_length(descriptorLines) > 0) {
        gchar* line = g_queue_pop_head(descriptorLines);
        if(!line) continue;

        switch(line[0]) {
            case 'r': {
                gchar** parts = g_strsplit(line, " ", 4);
                GString* id64 = g_string_new(parts[2]);
                id64 = g_string_append_c(id64, '=');
                GString* id = torflowutil_base64ToBase16(id64);

                currentRelay = g_hash_table_lookup(tfm->AllRelaysByFingerprint, id->str);
                if(!currentRelay) {
                    currentRelay = g_new0(TorFlowRelay, 1);
                    currentRelay->identity = id;
                    currentRelay->nickname = g_string_new(parts[1]);
                    g_hash_table_replace(tfm->AllRelaysByFingerprint, id->str, currentRelay);
                }

                g_string_free(id64, TRUE);
                g_strfreev(parts);

                tfm->slogf(SHADOW_LOG_LEVEL_DEBUG, tfm->_base.id,
                        "now getting descriptor for relay %s", currentRelay->nickname->str);
                break;
            }
            case 's': {
                if(g_strstr_len(line, -1, " Running")) {
                    currentRelay->isRunning = TRUE;
                } else {
                    currentRelay->isRunning = FALSE;
                }
                if(g_strstr_len(line, -1, " Fast")) {
                    currentRelay->isFast = TRUE;
                } else {
                    currentRelay->isFast = FALSE;
                }
                if(g_strstr_len(line, -1, " Exit")) {
                    currentRelay->isExit = TRUE;
                    if(g_strstr_len(line, -1, " BadExit")) {
                        currentRelay->isRunning = FALSE;
                    }
                } else {
                    currentRelay->isExit = FALSE;
                }

                break;
            }
            case 'w': {
                currentRelay->descriptorBandwidth =
                            atoi(g_strstr_len(line, -1, "Bandwidth=") + 10);
                /* normally we would use advertised BW, but that is not available */
                currentRelay->advertisedBandwidth = currentRelay->descriptorBandwidth;
                break;
            }
            case '.': //meaningless; squelch
                break;
            default:
                tfm->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfm->_base.id,
                    "don't know what to do with response '%s'", line);
                break;
        }

        g_free(line);
    }

    return g_hash_table_size(tfm->AllRelaysByFingerprint);
}

static GQueue* _torflowmanager_getMeasurableRelays(TorFlowManager* tfm) {
    GQueue* measurableRelays = g_queue_new();

    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, tfm->AllRelaysByFingerprint);
    while(g_hash_table_iter_next(&iter, &key, &value)) {
        TorFlowRelay* r = value;
        if(r && r->isFast && r->isRunning) {
            g_queue_insert_sorted(measurableRelays, r, torflowutil_compareRelaysData, NULL);
        }
    }

    return measurableRelays;
}

static guint _torflowmanager_updateSlices(TorFlowManager* tfm, GQueue* allMeasurableRelays) {
    /* free the old slices */
    while(tfm->currentSlices && g_queue_peek_head(tfm->currentSlices)) {
        TorFlowSlice* oldSlice = g_queue_pop_head(tfm->currentSlices);
        _torflowmanager_freeSlice(oldSlice);
    }

    g_assert(g_queue_get_length(tfm->currentSlices) == 0);

    /* create the new slices */
    gint numMeasurableRelays = g_queue_get_length(allMeasurableRelays);
    TorFlowSlice* newSlice = NULL;
    while(allMeasurableRelays && g_queue_peek_head(allMeasurableRelays)) {
        if(!newSlice) {
            newSlice = g_new0(TorFlowSlice, 1);
            newSlice->sliceNumber = g_queue_get_length(tfm->currentSlices);
            newSlice->filename = _torflowmanager_getSliceDownloadFileName(tfm, newSlice->sliceNumber, numMeasurableRelays);
        }

        TorFlowRelay* r = g_queue_pop_head(allMeasurableRelays);
        newSlice->allRelays = g_slist_append(newSlice->allRelays, r);
        newSlice->allRelaysLength++;

        if(r->isExit) {
            newSlice->exitRelays = g_slist_append(newSlice->exitRelays, r);
            newSlice->exitRelaysLength++;
        } else {
            newSlice->entryRelays = g_slist_append(newSlice->entryRelays, r);
            newSlice->entryRelaysLength++;
        }

        // finish building the slice if it is full or we have no relays left to add
        if(newSlice->allRelaysLength >= tfm->slicesize || g_queue_peek_head(allMeasurableRelays) == NULL) {
            // make sure all slices have at least one exit node, otherwise it cannot be measured
            if(newSlice->exitRelaysLength > 0 && newSlice->entryRelaysLength > 0) {
                g_queue_push_tail(tfm->currentSlices, newSlice);
            } else {
                tfm->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfm->_base.id,
                            "Slice %u is not measurable! Ignoring.", newSlice->sliceNumber);
                _torflowmanager_freeSlice(newSlice);
            }
            newSlice = NULL;
        }
    }

    return g_queue_get_length(tfm->currentSlices);
}

static void _torflowmanager_startProber(gpointer key, gpointer val, gpointer user) {
    torflowprober_start((TorFlowProber*)val);
}

static void _torflowmanager_continueProber(gpointer key, gpointer val, gpointer user) {
    torflowprober_continue((TorFlowProber*)val);
}

static void _torflowmanager_onDescriptorsReceived(TorFlowManager* tfm, GQueue* descriptorLines) {
    g_assert(tfm);

    /* first clear relay cache if we have any
     * this is needed so that new relays get created and measurement progress gets reset.
     * this is fine, all their previous measurements are stored by the aggregator. */
    g_hash_table_remove_all(tfm->AllRelaysByFingerprint);

    /* parse descriptor lines and store relays in table of all relays we know about */
    guint numRelays = _torflowmanager_parseAndStoreRelays(tfm, descriptorLines);

    /* now get the new set of all measurable relays */
    GQueue* allMeasurableRelays = _torflowmanager_getMeasurableRelays(tfm);
    guint numMeasurableRelays = g_queue_get_length(allMeasurableRelays);

    /* now break these up into relay slices */
    guint numSlices = _torflowmanager_updateSlices(tfm, allMeasurableRelays);

    tfm->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfm->_base.id,
            "New descriptors received. We have %u relays, %u measurable relays, and %u slices.",
            numRelays, numMeasurableRelays, numSlices);

    tfm->numMeasurableRelaysThisRound = numMeasurableRelays;
    tfm->numMeasurableSlicesThisRound = numSlices;
    tfm->numRemainingSlicesThisRound = numSlices;

    /* now our slices are ready, start the probers
     * when they finish bootstrapping, they will call torflowmanager_getNextSlice */
    if(!tfm->probersStarted) {
        g_hash_table_foreach(tfm->probers, _torflowmanager_startProber, NULL);
        tfm->probersStarted = TRUE;
    } else {
        g_hash_table_foreach(tfm->probers, _torflowmanager_continueProber, NULL);
    }

    // XXX rgj: i dont think we need to load anything anymore since the descriptors
    // will have our default starting values already
    // If not initialized, load initial advertised values
//    if(!tfp->internal->initialized) {
//        torflowaggregator_loadFromPresets(tfm->tfa, );
//    }
}

static const gchar* USAGE = "USAGE:\n"
	"  torflow filename pausetime workers slicesize node_cap ctlport socksport fileserver:fileport \n";

TorFlowManager* torflowmanager_new(gint argc, gchar* argv[], ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf) {
	g_assert(slogf);
	g_assert(scbf);

	if(argc != 9) {
		slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, USAGE);
		return NULL;
	}

	/* argv[0] is the 'program name' and should be ignored */
	gchar* v3bwPath = argv[1];
	gint pausetime = atoi(argv[2]);
	gint numWorkers = atoi(argv[3]);

	if(numWorkers < 1) {
		slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
            "Invalid number of torflow workers (%d). torflow will not operate.", numWorkers);
		return NULL;
	}

	gint slicesize = atoi(argv[4]);
	gdouble nodeCap = atof(argv[5]);

	gint hostControlPort = atoi(argv[6]);
	g_assert(hostControlPort <= G_MAXUINT16); // TODO log error instead
    in_port_t netControlPort = htons((in_port_t)hostControlPort);

    gint hostSocksPort = atoi(argv[7]);
    g_assert(hostSocksPort <= G_MAXUINT16); // TODO log error instead
    in_port_t netSocksPort = htons((in_port_t)hostSocksPort);

    /* get file server infos */
    GQueue* fileservers = g_queue_new();
    gchar** fsparts = g_strsplit(argv[8], ",", 0);
    gchar* fspart = NULL;
    for(gint i = 0; (fspart = fsparts[i]) != NULL; i++) {
        gchar** parts = g_strsplit(fspart, ":", 0);
        g_assert(parts[0] && parts[1]);

        /* the server domain name */
        gchar* name = parts[0];

        /* port in host order */
        gchar* hostFilePortStr = parts[1];
        gint hostFilePort = atoi(hostFilePortStr);
        g_assert(hostFilePort <= G_MAXUINT16); // TODO log error instead
        in_port_t netFilePort = htons((in_port_t)hostFilePort);

        TorFlowFileServer* fs = torflowfileserver_new(name, netFilePort);
        g_assert(fs);
        g_queue_push_tail(fileservers, fs);
        g_strfreev(parts);

        slogf(SHADOW_LOG_LEVEL_INFO, __FUNCTION__,
                "parsed file server %s at %s:%u",
                torflowfileserver_getName(fs),
                torflowfileserver_getHostIPStr(fs),
                ntohs(torflowfileserver_getNetPort(fs)));
    }
    g_strfreev(fsparts);

    g_assert(g_queue_get_length(fileservers) > 0); // TODO log error instead

    /* use epoll to asynchronously watch events for all of our sockets */
    gint mainEpollDescriptor = epoll_create(1);
    g_assert(mainEpollDescriptor > 0); // TODO log error instead

	TorFlowManager* tfm = g_new0(TorFlowManager, 1);
	tfm->slogf = slogf;
	tfm->scbf = scbf;
	tfm->workers = numWorkers;
	tfm->ed = mainEpollDescriptor;
	tfm->slicesize = slicesize;

	tfm->AllRelaysByFingerprint = g_hash_table_new_full(g_str_hash,
	        g_str_equal, NULL, (GDestroyNotify)_torflowmanager_freeRelay);
	tfm->currentSlices = g_queue_new();

    /* now start our controller to fetch descriptors */
	tfm->baseED = epoll_create(1);
	g_assert(tfm->baseED > 0); // TODO log error
	torflowutil_epoll(tfm->ed, tfm->baseED, EPOLL_CTL_ADD, EPOLLIN, tfm->slogf);
    TorFlowEventCallbacks handlers;
    memset(&handlers, 0, sizeof(TorFlowEventCallbacks));
    handlers.onBootstrapComplete = (BootstrapCompleteFunc) _torflowmanager_onBootstrapComplete;
    handlers.onDescriptorsReceived = (DescriptorsReceivedFunc) _torflowmanager_onDescriptorsReceived;
    torflowbase_init(&tfm->_base, &handlers, slogf, scbf, netControlPort, tfm->baseED, 0);
    torflowbase_start(&tfm->_base);

    /* helper to manage stat reports and create v3bw files */
    tfm->tfa = torflowaggregator_new(slogf, v3bwPath, nodeCap);

    /* workers that will probe the relays */
    tfm->probers = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) torflowbase_free);

    for(gint i = 1; i <= numWorkers; i++) {
        /* get the next fileserver */
        TorFlowFileServer* probeFileServer = g_queue_pop_head(fileservers);

        TorFlowProber* prober = torflowprober_new(slogf, scbf, tfm, i, numWorkers, pausetime,
                netControlPort, netSocksPort, probeFileServer);
        g_assert(prober); // TODO log error instead

        /* make sure we watch the prober events on our main epoll */
        gint proberED = torflow_getEpollDescriptor((TorFlow*)prober);
        torflowutil_epoll(tfm->ed, proberED, EPOLL_CTL_ADD, EPOLLIN, tfm->slogf);

        /* store the prober by its unique epoll descriptor */
        g_hash_table_replace(tfm->probers, GINT_TO_POINTER(proberED), prober);

        /* reuse the file server in round robin fashion */
        g_queue_push_tail(fileservers, probeFileServer);
    }

    /* the used file servers have been reffed by the probers;
     * the rest will be safely freed */
    g_queue_free_full(fileservers, (GDestroyNotify)torflowfileserver_unref);

    tfm->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
                    "started torflow with %i workers on control port %i and socks port %i",
                    numWorkers, hostControlPort, hostSocksPort);

    return tfm;
}

static void _torflowmanager_activateBase(TorFlowManager* tfm) {
    /* collect the events that are ready */
    struct epoll_event epevs[100];
    gint nfds = epoll_wait(tfm->baseED, epevs, 100, 0);
    if(nfds == -1) {
        tfm->slogf(SHADOW_LOG_LEVEL_CRITICAL, tfm->_base.id,
                "error in epoll_wait");
    } else {
        /* activate correct component for every socket thats ready */
        for(gint i = 0; i < nfds; i++) {
            gint d = epevs[i].data.fd;
            uint32_t e = epevs[i].events;
            if(d == torflowbase_getControlSD(&tfm->_base)) {
                torflowbase_activate(&tfm->_base, d, e);
            } else {
                tfm->slogf(SHADOW_LOG_LEVEL_WARNING, tfm->_base.id,
                        "got readiness on unknown descriptor: %i", d);
            }
        }
    }
}

void torflowmanager_ready(TorFlowManager* tfm) {
	g_assert(tfm);

	/* collect the events that are ready */
	struct epoll_event epevs[1000];
	memset(epevs, 0, sizeof(struct epoll_event)*1000);

	gint nfds = epoll_wait(tfm->ed, epevs, 1000, 0);
	if(nfds == -1) {
		tfm->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"epoll_wait error %i: %s", errno, g_strerror(errno));
		return;
	}

	/* activate correct component for every socket thats ready */
	for(gint i = 0; i < nfds; i++) {
		gint d = epevs[i].data.fd;
		uint32_t e = epevs[i].events;

		if(d == tfm->baseED) {
            tfm->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__,
                "handling events %i for fd %i", e, d);
            _torflowmanager_activateBase(tfm);
		} else {
            TorFlowProber* prober = g_hash_table_lookup(tfm->probers, GINT_TO_POINTER(d));
            if(prober) {
                tfm->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__,
                    "handling events %i for fd %i", e, d);
                torflow_ready((TorFlow*)prober);
            } else {
                tfm->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
                        "helper lookup failed for fd '%i'", d);
            }
		}
	}
}

void torflowmanager_free(TorFlowManager* tfm) {
	g_assert(tfm);

	if(tfm->tfa) {
		torflowaggregator_free(tfm->tfa);
	}

	if(tfm->probers) {
	    /* this calls the free function passed in on hash table creation for all elements */
	    g_hash_table_destroy(tfm->probers);
	}

	if(tfm->AllRelaysByFingerprint) {
	    g_hash_table_destroy(tfm->AllRelaysByFingerprint);
	}

	while(tfm->currentSlices && g_queue_get_length(tfm->currentSlices) > 0) {
	    TorFlowSlice* slice = g_queue_pop_head(tfm->currentSlices);
	    if(slice) {
	        _torflowmanager_freeSlice(slice);
	    }
	}

	if(tfm->ed) {
		close(tfm->ed);
	}

	g_free(tfm);
}
