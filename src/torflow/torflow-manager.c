/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowManager {
	gint ed;
	ShadowLogFunc slogf;
	ShadowCreateCallbackFunc scbf;
	TorFlowProber** tfps;
	gint* tfpeds;
	gint workers;
};

static const gchar* USAGE = "USAGE:\n"
	"  torflow thinktime workers slicesize node_cap ctlport:socksport fileserver:fileport,... \n";

TorFlowManager* torflowmanager_new(gint argc, gchar* argv[], ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf) {
	g_assert(slogf);
	g_assert(scbf);

	if(argc < 7 || argc > 9) {
		slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__, USAGE);
		return NULL;
	}

	/* use epoll to asynchronously watch events for all of our sockets */
	gint mainEpollDescriptor = epoll_create(1);
	if(mainEpollDescriptor == -1) {
		slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"Error in main epoll_create");
		close(mainEpollDescriptor);
		return NULL;
	}

	/* get file server infos */
	GQueue* fileservers = g_queue_new();
	gchar** fsparts = g_strsplit(argv[6], ",", 0);
	gchar* fspart = NULL;
	for(gint i = 0; (fspart = fsparts[i]) != NULL; i++) {
		TorFlowFileServer* fs = g_new0(TorFlowFileServer, 1);
		gchar** parts = g_strsplit(fspart, ":", 0);

		fs->portString = g_strdup(parts[1]);
		fs->port = htons((in_port_t) atoi(fs->portString));

		fs->name = g_strdup(parts[0]);
		fs->address = torflowutil_lookupAddress(fs->name, slogf);
		fs->addressString = torflowutil_ipToNewString(fs->address);

		g_strfreev(parts);

		g_queue_push_tail(fileservers, fs);
	}
	g_strfreev(fsparts);

	gint probeControlPort = 0, probeSocksPort = 0, thinktime = 0, slicesize = 0;
	gdouble nodeCap = 0.0;

	thinktime = atoi(argv[1]);
	slicesize = atoi(argv[3]);
	nodeCap = atof(argv[4]);
	gchar** portparts = g_strsplit(argv[5], ":", 0);
	probeControlPort = atoi(portparts[0]);
	probeSocksPort = atoi(portparts[1]);
	g_strfreev(portparts);

	TorFlowFileServer* probeFileServer = g_queue_pop_head(fileservers);

	TorFlowManager* tfm = g_new0(TorFlowManager, 1);
	tfm->ed = mainEpollDescriptor;
	tfm->slogf = slogf;
	tfm->scbf = scbf;
	tfm->workers = atoi(argv[2]);
	if(tfm->workers < 1) {
		slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
			"Invalid number of torflow workers (%d). torflow will not operate.",
			tfm->workers);
		tfm->tfps = NULL;
		tfm->tfpeds = NULL;
		return tfm;
	}
	
	tfm->tfps = g_new0(TorFlowProber*, tfm->workers);
	tfm->tfpeds = g_new0(int, tfm->workers);
	for(gint i = 0; i < tfm->workers; i++) {
		gdouble minPct = ((gdouble)i)/tfm->workers;
		gdouble maxPct = ((gdouble)i+1.0)/tfm->workers;
		tfm->tfps[i] = torflowprober_new(slogf, scbf,
				minPct, maxPct,
				thinktime, slicesize, nodeCap,
				probeControlPort, probeSocksPort, probeFileServer);
		tfm->tfpeds[i] = torflow_getEpollDescriptor((TorFlow*)tfm->tfps[i]);
		torflowutil_epoll(tfm->ed, tfm->tfpeds[i], EPOLL_CTL_ADD,
				EPOLLIN, tfm->slogf);
	}

	return tfm;
}

void torflowmanager_ready(TorFlowManager* tfm) {
	g_assert(tfm);

	/* collect the events that are ready */
	struct epoll_event epevs[1000];
	gint nfds = epoll_wait(tfm->ed, epevs, 1000, 0);
	if(nfds == -1) {
		tfm->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"error in epoll_wait");
		return;
	}

	/* activate correct component for every socket thats ready */
	for(gint i = 0; i < nfds; i++) {
		gint d = epevs[i].data.fd;
		uint32_t e = epevs[i].events;

		gboolean found = FALSE;
		for(gint j = 0; j < tfm->workers; j++) {
			if(d == tfm->tfpeds[j]) {
				torflow_ready((TorFlow*)tfm->tfps[j]);
				found = TRUE;
			}
		}
		if(!found) {
			tfm->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
					"helper lookup failed for fd '%i'", d);
		}
	}
}

void torflowmanager_free(TorFlowManager* tfm) {
	g_assert(tfm);

	if(tfm->tfps) {
		for(gint i = 0; i < tfm->workers; i++) {
			if(tfm->tfps[i]) {
				torflowbase_free((TorFlowBase*)(tfm->tfps[i]));
			}
		}
		g_free(tfm->tfps);
	}

	if(tfm->tfpeds) {
		g_free(tfm->tfpeds);
	}

	if(tfm->ed) {
		close(tfm->ed);
	}

	g_free(tfm);
}
