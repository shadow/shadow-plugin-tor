/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

#define BUFSIZE 16384

typedef enum {
	S_NONE, S_CONNECTING, S_SOCKSSENDCONNECT, S_SOCKSRECVCONNECT,
	S_SOCKSSENDINIT, S_SOCKSRECVINIT, S_HTTPSENDREQUEST, S_HTTPRECVREPLY,
	S_READY
} TorFlowSocksState;

typedef struct _CallbackData {
	TorFlow* tf;
	gint socksd;
} CallbackData;

typedef struct _TorFlowDownload {
	gint socksd;
	TorFlowSocksState socksState;

	TorFlowFileServer* fileserver;

	gchar recvbuf[BUFSIZE+1];
	gint recvOffset;
	gchar* filePath;
	gint contentLength;
	gint remaining;

	struct timespec start;
	struct timespec first;
	struct timespec end;
} TorFlowDownload;

struct _TorFlowInternal {
	TorFlowEventCallbacks eventHandlers;

	/* a copy of the epolld from torflowbase */
	gint epolld;

	/* the socks port that the tor client is listening on, in network order */
	in_port_t netSocksPort;
	/* the port our side of the socks connection assigned to us, in host order */
	in_port_t hostBoundSocksPort;
	GHashTable* downloads;
};

gint torflow_newDownload(TorFlow* tf, TorFlowFileServer* fileserver) {
	g_assert(tf);
	g_assert(fileserver);

	/* create the client socket and get a socket descriptor */
	gint socksd = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
	if(socksd == -1) {
		tf->_base.slogf(SHADOW_LOG_LEVEL_CRITICAL, tf->_base.id,
				"unable to start client socksd: error in socket");
		return -1;
	}

	/* our client socket address information for connecting to the server */
	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);;
	serverAddress.sin_port = tf->internal->netSocksPort;

	/* connect to server. since we are non-blocking, we expect this to return EINPROGRESS */
	gint res = connect(socksd, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
	if (res == -1 && errno != EINPROGRESS) {
		tf->_base.slogf(SHADOW_LOG_LEVEL_CRITICAL, tf->_base.id,
				"unable to start client socksd: error in connect");
		return -1;
	}

	struct sockaddr socksName;
	memset(&socksName, 0, sizeof(struct sockaddr));
	socklen_t socksNameLen = (socklen_t)sizeof(struct sockaddr);
	res = getsockname(socksd, &socksName, &socksNameLen);
	if(res == 0) {
	    tf->internal->hostBoundSocksPort = ntohs(((struct sockaddr_in*)&socksName)->sin_port);
	}

	/* specify the events to watch for on this socket.
	 * the client wants to know when it can send a message. */
	torflowutil_epoll(tf->internal->epolld, socksd, EPOLL_CTL_ADD, EPOLLOUT, tf->_base.slogf);

	TorFlowDownload* tfd = g_new0(TorFlowDownload, 1);
	tfd->socksd = socksd;
	tfd->socksState = S_CONNECTING;

    torflowfileserver_ref(fileserver);
    tfd->fileserver = fileserver;

	g_hash_table_insert(tf->internal->downloads, GINT_TO_POINTER(socksd), tfd);

	return socksd;
}

static void _torflow_freeDownload(TorFlowDownload* tfd) {
	g_assert(tfd);
	if(tfd->socksd) {
		close(tfd->socksd);
	}
	if(tfd->filePath) {
		g_free(tfd->filePath);
	}
	if(tfd->fileserver) {
	    torflowfileserver_unref(tfd->fileserver);
	}
	g_free(tfd);
}

void torflow_freeDownload(TorFlow* tf, gint socksd) {
	g_assert(tf);
	TorFlowDownload* tfd = g_hash_table_lookup(tf->internal->downloads, GINT_TO_POINTER(socksd));
	if(tfd) {
		_torflow_freeDownload(tfd);
	}
}

void torflow_startDownload(TorFlow* tf, gint socksd, gchar* filePath) {
	g_assert(tf);

	TorFlowDownload* tfd = g_hash_table_lookup(tf->internal->downloads, GINT_TO_POINTER(socksd));
	if(tfd) {
		torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, EPOLLOUT, tf->_base.slogf);
		tfd->socksState = S_HTTPSENDREQUEST;
		if(tfd->filePath) {
			g_free(tfd->filePath);
		}
		tfd->filePath = g_strdup(filePath);
	}
}

static void _torflow_callFileServerConnected(CallbackData* cd) {
	g_assert(cd && cd->tf);
	cd->tf->internal->eventHandlers.onFileServerConnected(cd->tf, cd->socksd);
	g_free(cd);
}

static void _torflow_callFileServerTimeout(CallbackData* cd) {
	g_assert(cd && cd->tf);
	cd->tf->internal->eventHandlers.onFileServerTimeout(cd->tf);
	g_free(cd);
}

static void _torflow_activateSocksDownload(TorFlow* tf, TorFlowDownload* tfd, uint32_t events) {
	g_assert(tf);
	g_assert(tfd);

	if(events & EPOLLOUT) {
		tf->_base.slogf(SHADOW_LOG_LEVEL_DEBUG, tf->_base.id, "EPOLLOUT is set");
	}
	if(events & EPOLLIN) {
		tf->_base.slogf(SHADOW_LOG_LEVEL_DEBUG, tf->_base.id, "EPOLLIN is set");
	}

beginsocks:
	switch(tfd->socksState) {
	case S_CONNECTING: {
		if(events & EPOLLOUT) {
			/* we are now connected */
			tfd->socksState = S_SOCKSSENDINIT;
			goto beginsocks;
		}
		break;
	}

	case S_SOCKSSENDINIT: {
		g_assert(events & EPOLLOUT);

		gchar sendbuf[16];
		sendbuf[0] = 0x05;
		sendbuf[1] = 0x01;
		sendbuf[2] = 0x00;
		gint bytes = send(tfd->socksd, sendbuf, 3, 0);

		g_assert(bytes == 3);

		tf->_base.slogf(SHADOW_LOG_LEVEL_INFO, tf->_base.id, "socks sent init");

		torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, EPOLLIN, tf->_base.slogf);
		tfd->socksState = S_SOCKSRECVINIT;
		break;
	}

	case S_SOCKSRECVINIT: {
		g_assert(events & EPOLLIN);

		gint bytes = recv(tfd->socksd, tfd->recvbuf, BUFSIZE, 0);

		if(bytes != 2 || tfd->recvbuf[0] != 0x05 || tfd->recvbuf[1] != 0x00) {
			tf->_base.slogf(SHADOW_LOG_LEVEL_CRITICAL, tf->_base.id,
				"socks init error (read %i bytes)", bytes);
		} else {
			tf->_base.slogf(SHADOW_LOG_LEVEL_INFO, tf->_base.id, "socks init success");
			torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, EPOLLOUT, tf->_base.slogf);
			tfd->socksState = S_SOCKSSENDCONNECT;
		}

		break;
	}

	case S_SOCKSSENDCONNECT: {
		g_assert(events & EPOLLOUT);

		in_addr_t netIP = torflowfileserver_getNetIP(tfd->fileserver);
		in_port_t netPort = torflowfileserver_getNetPort(tfd->fileserver);

		gchar sendbuf[64];
		memset(sendbuf, 0, sizeof(gchar)*64);
		sendbuf[0] = 0x05;
		sendbuf[1] = 0x01;
		sendbuf[2] = 0x00;
		sendbuf[3] = 0x01;
		memcpy(&sendbuf[4], &(netIP), 4);
		memcpy(&sendbuf[8], &(netPort), 2);

		gint bytes = send(tfd->socksd, sendbuf, 10, 0);

		g_assert(bytes == 10);

		tf->_base.slogf(SHADOW_LOG_LEVEL_INFO, tf->_base.id,
		        "sent socks server connect to %s at %s:%u",
		        torflowfileserver_getName(tfd->fileserver),
		        torflowfileserver_getHostIPStr(tfd->fileserver), ntohs(netPort));

		torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, EPOLLIN, tf->_base.slogf);
		tfd->socksState = S_SOCKSRECVCONNECT;
		break;
	}

	case S_SOCKSRECVCONNECT: {
		g_assert(events & EPOLLIN);

		gint bytes = recv(tfd->socksd, tfd->recvbuf+tfd->recvOffset, BUFSIZE-tfd->recvOffset, 0);
		tfd->recvOffset += bytes;

		if(tfd->recvOffset < 10) {
			/* wait until they send us more */
			break;
		}

		if(tfd->recvbuf[0] == 0x05 && tfd->recvbuf[1] == 0x00 && tfd->recvbuf[3] == 0x01) {
			/* socks server may tell us to connect somewhere else ... */
			in_addr_t serverAddress;
			in_port_t serverPort;
			memcpy(&serverAddress, &(tfd->recvbuf[4]), 4);
			memcpy(&serverPort, &(tfd->recvbuf[8]), 2);

			/* ... but we dont support it */
			g_assert(serverAddress == 0 && serverPort == 0);

			tf->_base.slogf(SHADOW_LOG_LEVEL_INFO, tf->_base.id, "socks connect success");

			torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, 0, tf->_base.slogf);
			tfd->socksState = S_READY;

			if(tf->internal->eventHandlers.onFileServerConnected) {
				/* wait a second to give the stream success msg a chance to get delivered */
				CallbackData* cd = g_new0(CallbackData, 1);
				cd->tf = tf;
				cd->socksd = tfd->socksd;
				tf->_base.scbf((ShadowPluginCallbackFunc)_torflow_callFileServerConnected, cd, 1000);
			}
		} else if(tfd->recvbuf[0] == 0x05 && tfd->recvbuf[1] == 0x06 && tfd->recvbuf[2] == 0x00 && tfd->recvbuf[3] == 0x01) {
			tf->_base.slogf(SHADOW_LOG_LEVEL_MESSAGE, tf->_base.id,
				"socks connect timed out");
			if(tf->internal->eventHandlers.onFileServerTimeout) {
				CallbackData* cd = g_new0(CallbackData, 1);
				cd->tf = tf;
				cd->socksd = 0;
				tf->_base.scbf((ShadowPluginCallbackFunc)_torflow_callFileServerTimeout, cd, 1000);
			}	
		} else {
			tf->_base.slogf(SHADOW_LOG_LEVEL_CRITICAL, tf->_base.id,
				"socks connect error (read %i bytes, code %x%x%x%x)", bytes,
				tfd->recvbuf[0], tfd->recvbuf[1], tfd->recvbuf[2], tfd->recvbuf[3]);
			//tf->_base.slogf(SHADOW_LOG_LEVEL_CRITICAL, tf->_base.id,
			//	"socks connect error (read %i bytes)", bytes);
		}

		/* reset */
		tfd->recvOffset = 0;
		memset(tfd->recvbuf, 0, BUFSIZE);

		break;
	}

	case S_HTTPSENDREQUEST: {
		g_assert(events & EPOLLOUT);

		GString* request = g_string_new(NULL);
		g_string_printf(request, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", tfd->filePath,
		        torflowfileserver_getName(tfd->fileserver));
		gint bytes = send(tfd->socksd, request->str, request->len, 0);

		g_assert(bytes == request->len);
		g_string_free(request, TRUE);

		clock_gettime(CLOCK_REALTIME, &(tfd->start));

		torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, EPOLLIN, tf->_base.slogf);
		tfd->socksState = S_HTTPRECVREPLY;
		break;
	}

	case S_HTTPRECVREPLY: {
		g_assert(events & EPOLLIN);

		gint bytes = recv(tfd->socksd, tfd->recvbuf+tfd->recvOffset, BUFSIZE-tfd->recvOffset, 0);
		tfd->recvOffset += bytes;
		tfd->recvbuf[tfd->recvOffset] = 0x0;

		if(bytes == 0) {
			tf->_base.slogf(SHADOW_LOG_LEVEL_WARNING, tf->_base.id, "socks socket %i was closed", tfd->socksd);
			torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_DEL, 0, tf->_base.slogf);
			close(tfd->socksd);
			break;
		}

		if(!tfd->contentLength) {
			gchar* error404 = g_strstr_len(tfd->recvbuf, bytes, "HTTP/1.1 404 NOT FOUND\r\n");
			g_assert(!error404);

			gchar* ok200 = g_strstr_len(tfd->recvbuf, bytes, "HTTP/1.1 200 OK\r\n");
			g_assert(ok200);

			gchar* clen = g_strstr_len(tfd->recvbuf, bytes, "Content-Length: ");
			if(clen) {
				clen += 16;
			}

			gchar* payload = g_strstr_len(tfd->recvbuf, bytes, "\r\n\r\n");
			if(payload) {
				clock_gettime(CLOCK_REALTIME, &(tfd->first));
				payload[0] = 0x0; // so we can get content length
				payload += 4;
				tfd->contentLength = (gsize) atoi(clen);
				tfd->remaining = tfd->contentLength - (bytes - (payload - tfd->recvbuf));
				tfd->recvOffset = 0;
				tf->_base.slogf(SHADOW_LOG_LEVEL_INFO, tf->_base.id, "http payload will be %i bytes", tfd->contentLength);
			}
		} else {
			tfd->remaining -= bytes;
			tfd->recvOffset = 0;
			tf->_base.slogf(SHADOW_LOG_LEVEL_INFO, tf->_base.id, "got %i bytes", bytes);
		}

		/* finished a download probe - only the prober will get here bc senders stop reading */
		if(!tfd->remaining) {
			clock_gettime(CLOCK_REALTIME, &(tfd->end));

			gsize roundTripTime = torflowutil_computeTime(&tfd->start, &tfd->first);
			gsize payloadTime = torflowutil_computeTime(&tfd->first, &tfd->end);
			gsize totalTime = torflowutil_computeTime(&tfd->start, &tfd->end);
			gint contentLength = tfd->contentLength;

			tfd->contentLength = 0;
			tfd->recvOffset = 0;
			memset(tfd->recvbuf, 0, BUFSIZE);

			torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, 0, tf->_base.slogf);
			tfd->socksState = S_READY;

			if(tf->internal->eventHandlers.onFileDownloadComplete) {
				tf->internal->eventHandlers.onFileDownloadComplete(tf, contentLength, roundTripTime, payloadTime, totalTime);
			}
		}

		break;
	}

	case S_READY: {
		torflowutil_epoll(tf->internal->epolld, tfd->socksd, EPOLL_CTL_MOD, 0, tf->_base.slogf);
		break;
	}

	default:
		break;
	}
}

static void _torflow_onFree(TorFlow* tf) {
	g_assert(tf);
	g_assert(tf->internal);

	tf->internal->eventHandlers.onFree(tf);

	if(tf->internal->epolld) {
		close(tf->internal->epolld);
	}

	g_free(tf->internal);
}

void torflow_init(TorFlow* tf, TorFlowEventCallbacks* eventHandlers,
		ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf,
		in_port_t controlPort, in_port_t socksPort, gint workerID) {
	g_assert(tf);
	g_assert(eventHandlers);

	tf->internal = g_new0(TorFlowInternal, 1);

	/* use epoll to asynchronously watch events for all of our sockets */
	tf->internal->epolld = epoll_create(1);
	if(tf->internal->epolld == -1) {
		tf->_base.slogf(SHADOW_LOG_LEVEL_ERROR, tf->_base.id,
				"Error in main epoll_create");
		close(tf->internal->epolld);
		return;
	}

	tf->internal->netSocksPort = socksPort;
	tf->internal->downloads = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify) _torflow_freeDownload);

	/* store the child events */
	TorFlowEventCallbacks events = *eventHandlers;
	tf->internal->eventHandlers = events;
	/* intercept onFree so we can properly free our data */
	events.onFree = (FreeFunc) _torflow_onFree;

	torflowbase_init((TorFlowBase*)tf, &events, slogf, scbf, controlPort, tf->internal->epolld, workerID);
}

void torflow_start(TorFlow* tf) {
	torflowbase_start((TorFlowBase*) tf);
}

gint torflow_getEpollDescriptor(TorFlow* tf) {
	g_assert(tf);
	return tf->internal->epolld;
}

in_port_t torflow_getHostBoundSocksPort(TorFlow* tf) {
    g_assert(tf);
    return tf->internal->hostBoundSocksPort;
}

void torflow_ready(TorFlow* tf) {
	g_assert(tf);

	/* collect the events that are ready */
	struct epoll_event epevs[100];
	gint nfds = epoll_wait(tf->internal->epolld, epevs, 100, 0);
	if(nfds == -1) {
		tf->_base.slogf(SHADOW_LOG_LEVEL_CRITICAL, tf->_base.id,
				"error in epoll_wait");
	} else {
		/* activate correct component for every socket thats ready */
		for(gint i = 0; i < nfds; i++) {
			gint d = epevs[i].data.fd;
			uint32_t e = epevs[i].events;
			if(d == torflowbase_getControlSD((TorFlowBase*)tf)) {
				torflowbase_activate((TorFlowBase*)tf, d, e);
			} else {
				/* check if we have a socks connection */
				TorFlowDownload* tfd = g_hash_table_lookup(tf->internal->downloads, GINT_TO_POINTER(d));
				if(tfd && d == tfd->socksd) {
					_torflow_activateSocksDownload(tf, tfd, e);
				}
			}
		}
	}
}

