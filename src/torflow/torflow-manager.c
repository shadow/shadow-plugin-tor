/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowManager {
	gint ed;
	ShadowLogFunc slogf;
	ShadowCreateCallbackFunc scbf;
	TorFlowProber* tfp;
	gint tfped;
};

static const gchar* USAGE = "USAGE:\n"
	"  torflow thinktime slicesize node_cap ctlport:socksport fileserver:fileport,... \n";

TorFlowManager* torflowmanager_new(gint argc, gchar* argv[], ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf) {
	g_assert(slogf);
	g_assert(scbf);

	if(argc < 6 || argc > 8) {
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

	TorFlowManager* tfm = g_new0(TorFlowManager, 1);
	tfm->ed = mainEpollDescriptor;
	tfm->slogf = slogf;
	tfm->scbf = scbf;

	/* get file server infos */
	GQueue* fileservers = g_queue_new();
	gchar** fsparts = g_strsplit(argv[5], ",", 0);
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
	slicesize = atoi(argv[2]);
	nodeCap = atof(argv[3]);
	gchar** portparts = g_strsplit(argv[4], ":", 0);
	probeControlPort = atoi(portparts[0]);
	probeSocksPort = atoi(portparts[1]);
	g_strfreev(portparts);

	TorFlowFileServer* probeFileServer = g_queue_pop_head(fileservers);

	tfm->tfp = torflowprober_new(slogf, scbf, thinktime, slicesize, nodeCap,
			probeControlPort, probeSocksPort, probeFileServer);
	tfm->tfped = torflow_getEpollDescriptor((TorFlow*)tfm->tfp);

	torflowutil_epoll(tfm->ed, tfm->tfped, EPOLL_CTL_ADD, EPOLLIN, tfm->slogf);

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

		if(d == tfm->tfped) {
			torflow_ready((TorFlow*)tfm->tfp);
		} else {
			tfm->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
					"helper lookup failed for fd '%i'", d);
		}
	}
}

void torflowmanager_free(TorFlowManager* tfm) {
	g_assert(tfm);

	if(tfm->tfp) {
		torflowbase_free((TorFlowBase*)(tfm->tfp));
	}

	if(tfm->ed) {
		close(tfm->ed);
	}

	g_free(tfm);
}
