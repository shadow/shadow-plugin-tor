/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowManager {
	gint ed;
	ShadowLogFunc slogf;
	ShadowCreateCallbackFunc scbf;
	TorFlowAggregator* tfa;
	GHashTable* probers;
	gint workers;
};

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

    tfm->tfa = torflowaggregator_new(slogf, v3bwPath, slicesize, nodeCap);

    tfm->probers = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) torflowbase_free);

    for(gint i = 0; i < numWorkers; i++) {
        /* get the next fileserver */
        TorFlowFileServer* probeFileServer = g_queue_pop_head(fileservers);

        TorFlowProber* prober = torflowprober_new(slogf, scbf, tfm->tfa, i, numWorkers, pausetime,
                slicesize, netControlPort, netSocksPort, probeFileServer);
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

void torflowmanager_free(TorFlowManager* tfm) {
	g_assert(tfm);

	if(tfm->tfa) {
		torflowaggregator_free(tfm->tfa);
	}

	if(tfm->probers) {
	    /* this calls the free function passed in on hash table creation for all elements */
	    g_hash_table_destroy(tfm->probers);
	}

	if(tfm->ed) {
		close(tfm->ed);
	}

	g_free(tfm);
}
