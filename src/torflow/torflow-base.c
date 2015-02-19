/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

typedef enum {
	C_NONE, C_AUTH, C_BOOTSTRAP, C_MAIN
} TorFlowControlState;

struct _TorFlowBaseInternal {
	TorFlowEventCallbacks eventHandlers;

	/* epoll socket to watch our control socket */
	gint epolld;

	/* controlling the tor client */
	in_port_t netControlPort;
	gint controld;
	TorFlowControlState controlState;
	GQueue* commands;

	/* flags */
	gboolean waitingMeasurementCircuit;
	gint cachedCircId;
	gchar* cachedCircPath;
	gboolean isStatusEventSet;

	gboolean readyForDescriptors;
	gboolean gettingDescriptors;
	GQueue* descriptorLines;
	
	GString* receiveLineBuffer;
};

static gint _torflowbase_parseCode(gchar* line) {
	gchar** parts1 = g_strsplit(line, " ", 0);
	gchar** parts2 = g_strsplit_set(parts1[0], "-+", 0);
	gint code = atoi(parts2[0]);
	g_strfreev(parts1);
	g_strfreev(parts2);
	return code;
}

static gint _torflowbase_parseBootstrapProgress(gchar* line) {
	gint progress = -1;
	gchar** parts = g_strsplit(line, " ", 0);
	gchar* part = NULL;
	gboolean foundBootstrap = FALSE;
	for(gint j = 0; (part = parts[j]) != NULL; j++) {
		gchar** subparts = g_strsplit(part, "=", 0);
		if(!g_ascii_strncasecmp(subparts[0], "BOOTSTRAP", 9)) {
			foundBootstrap = TRUE;
		} else if(foundBootstrap && !g_ascii_strncasecmp(subparts[0], "PROGRESS", 8)) {
			progress = atoi(subparts[1]);
		}
		g_strfreev(subparts);
	}
	g_strfreev(parts);
	return progress;
}

static void _torflowbase_processLineSync(TorFlowBase* tfb, GString* linebuf) {
	/* '250 OK' msgs are fine. the only thing we want to extract info from
	 * is when we are building a new measurement circuit
	 * or when we are getting descriptor information and need to stop.*/
	if(g_strstr_len(linebuf->str, linebuf->len, " EXTENDED ")) {
		if(tfb->internal->waitingMeasurementCircuit && !tfb->internal->cachedCircId) {
			/* response to EXTENDCIRCUIT '250 EXTENDED circid' */
			gchar** parts = g_strsplit(linebuf->str, " ", 0);
			tfb->internal->cachedCircId = atoi(parts[2]);
			g_strfreev(parts);

			if(tfb->internal->cachedCircId <= 0) {
				tfb->slogf(SHADOW_LOG_LEVEL_WARNING, tfb->id,
						"measurement circuit build failure '%s'", tfb->internal->cachedCircPath);
			} else {
				tfb->slogf(SHADOW_LOG_LEVEL_INFO, tfb->id,
						"started building measurement circuit '%i'", tfb->internal->cachedCircId);
			}
		}
	} else if (tfb->internal->readyForDescriptors && g_strstr_len(linebuf->str, linebuf->len, "ns/all")) {
        tfb->internal->readyForDescriptors = FALSE;
        tfb->internal->gettingDescriptors = TRUE;
        tfb->internal->descriptorLines = g_queue_new();
    } else if (tfb->internal->gettingDescriptors && g_strstr_len(linebuf->str, linebuf->len, " OK")) {
		tfb->internal->gettingDescriptors = FALSE;
		if(tfb->internal->eventHandlers.onDescriptorsReceived) {
			tfb->internal->eventHandlers.onDescriptorsReceived(tfb, tfb->internal->descriptorLines);
		}
		while(g_queue_get_length(tfb->internal->descriptorLines) > 0) {
		    g_free(g_queue_pop_head(tfb->internal->descriptorLines));
		}
		g_queue_free(tfb->internal->descriptorLines);
	}
}

static void _torflowbase_processLineASync(TorFlowBase* tfb, GString* linebuf) {
	gboolean isConsumed = FALSE;

	if(g_strstr_len(linebuf->str, linebuf->len, ".exit")) {
		tfb->slogf(SHADOW_LOG_LEVEL_INFO, tfb->id,
				"ignoring tor-internal response '%s'", linebuf->str);
		return;
	}

	if(g_strstr_len(linebuf->str, linebuf->len, " CIRC ")) {
		/* responses:
		 *   650 CIRC 21 BUILT ...
		 *   650 CIRC 3 CLOSED ...
		 */
		gchar** parts = g_strsplit(linebuf->str, " ", 0);
		gint circid = atoi(parts[2]);

		if(g_strstr_len(parts[3], 5, "BUILT")) {
			if(tfb->internal->waitingMeasurementCircuit && circid == tfb->internal->cachedCircId) {
				tfb->slogf(SHADOW_LOG_LEVEL_INFO, tfb->id,
						"finished building new measurement circuit '%i'", circid);

				tfb->internal->waitingMeasurementCircuit = FALSE;

				if(tfb->internal->eventHandlers.onMeasurementCircuitBuilt) {
					tfb->internal->eventHandlers.onMeasurementCircuitBuilt(tfb, circid);
					isConsumed = TRUE;
				}
			}
		} else if(g_strstr_len(parts[3], 6, "CLOSED")) {
		} else if(g_strstr_len(parts[3], 6, "FAILED")) {
			if(circid == tfb->internal->cachedCircId) {
				if(g_strstr_len(parts[7], 14, "REASON=TIMEOUT")) {
					tfb->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfb->id,
							"measurement circuit '%i' has timed out", circid);
				} else {
					tfb->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfb->id,
							"measurement circuit '%i' has failed for some reason", circid);
				}

				tfb->internal->waitingMeasurementCircuit = FALSE;

				if(tfb->internal->eventHandlers.onFileServerTimeout) {
					tfb->internal->eventHandlers.onFileServerTimeout(tfb);
					isConsumed = TRUE;
				}
			}
		}

		g_strfreev(parts);
	} else if(g_strstr_len(linebuf->str, linebuf->len, " STREAM ")) {
		/* responses:
		 *   650 STREAM 21 NEW 0 52.1.0.0:80 ...
		 *   650 STREAM 21 SUCCEEDED 22 52.1.0.0:80
		 *   650 STREAM 18 CLOSED 5 53.1.0.0:9111 ...
		 */
		gchar** parts = g_strsplit(linebuf->str, " ", 0);
		gchar** targetParts = g_strsplit(parts[5], ":", 0);

		gint streamid = atoi(parts[2]);
		gint circid = atoi(parts[4]);
		GString* targetAddress = g_string_new(targetParts[0]);
		gint targetPort = atoi(targetParts[1]);

		GString* sourceAddress = NULL;
		gint sourcePort = 0;
        for(gint i = 6; parts[i]; i++) {
            if(!g_ascii_strncasecmp(parts[i], "SOURCE_ADDR=", 12)) {
                gchar** sourceParts = g_strsplit(&parts[i][12], ":", 0);
                g_assert(sourceParts[0] && sourceParts[1]);
                sourceAddress = g_string_new(sourceParts[0]);
                sourcePort = atoi(sourceParts[1]);
                g_strfreev(sourceParts);
            }
		}

		if(g_strstr_len(parts[3], 3, "NEW") && tfb->internal->eventHandlers.onStreamNew) {
			tfb->internal->eventHandlers.onStreamNew(tfb, streamid, circid,
			        targetAddress->str, targetPort, sourceAddress->str, sourcePort);
			isConsumed = TRUE;
		} else if(g_strstr_len(parts[3], 9, "SUCCEEDED") && tfb->internal->eventHandlers.onStreamSucceeded) {
			tfb->internal->eventHandlers.onStreamSucceeded(tfb, streamid, circid, targetAddress->str, targetPort);
			isConsumed = TRUE;
		} else if(g_strstr_len(parts[3], 6, "CLOSED")) {
		}

		if(sourceAddress) {
		    g_string_free(sourceAddress, TRUE);
		}
		g_string_free(targetAddress, TRUE);
		g_strfreev(targetParts);
		g_strfreev(parts);
	}

	if(!isConsumed) {
		tfb->slogf(SHADOW_LOG_LEVEL_INFO, tfb->id,
				"ignoring asynch response '%s'", linebuf->str);
	}
}

static void _torflowbase_processLine(TorFlowBase* tfb, GString* linebuf) {
	switch(tfb->internal->controlState) {
		case C_AUTH: {
			gint code = _torflowbase_parseCode(linebuf->str);
			if(code == 250) {
				tfb->slogf(SHADOW_LOG_LEVEL_INFO, tfb->id,
							"successfully received auth response '%s'", linebuf->str);
				g_queue_push_tail(tfb->internal->commands, g_string_new("GETINFO status/bootstrap-phase\r\n"));
				tfb->internal->controlState = C_BOOTSTRAP;
			} else {
				tfb->slogf(SHADOW_LOG_LEVEL_CRITICAL, tfb->id,
							"received failed auth response '%s'", linebuf->str);
			}
			break;
		}

		case C_BOOTSTRAP: {
			/* we will be getting all client status events, not all of them have bootstrap status */
			gint progress = _torflowbase_parseBootstrapProgress(linebuf->str);
			if(progress >= 0) {
				tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id,
							"successfully received bootstrap phase response '%s'", linebuf->str);
				if(progress >= 100) {
					tfb->slogf(SHADOW_LOG_LEVEL_MESSAGE, tfb->id,
							"torflow client is now ready (Bootstrapped 100)");

					g_queue_push_tail(tfb->internal->commands, g_string_new("SETCONF __LeaveStreamsUnattached=1 __DisablePredictedCircuits=1 MaxCircuitDirtiness=36000 CircuitStreamTimeout=3600\r\n"));
					g_queue_push_tail(tfb->internal->commands, g_string_new("SETEVENTS CIRC STREAM\r\n"));
					g_queue_push_tail(tfb->internal->commands, g_string_new("SIGNAL NEWNYM\r\n"));

					tfb->internal->isStatusEventSet = FALSE;
					tfb->internal->controlState = C_MAIN;
					if(tfb->internal->eventHandlers.onBootstrapComplete) {
						tfb->internal->eventHandlers.onBootstrapComplete(tfb);
					}
				} else if(!(tfb->internal->isStatusEventSet)) {
					/* not yet at 100%, register the async status event to wait for it */
					g_queue_push_tail(tfb->internal->commands, g_string_new("SETEVENTS EXTENDED STATUS_CLIENT\r\n"));
					tfb->internal->isStatusEventSet = TRUE;
				}
			}
			break;
		}

		case C_MAIN: {
			gint code = _torflowbase_parseCode(linebuf->str);

			if(code == 250) {/* synchronous responses */
				_torflowbase_processLineSync(tfb, linebuf);
			} else if(code == 650) {/* asynchronous responses */
				_torflowbase_processLineASync(tfb, linebuf);
			} else if(tfb->internal->gettingDescriptors) {/* descriptor info */
			    g_queue_push_tail(tfb->internal->descriptorLines, g_strdup(linebuf->str));
			} else {
				tfb->slogf(SHADOW_LOG_LEVEL_WARNING, tfb->id,
					"received unhandled response '%s'", linebuf->str);
			}
			break;
		}

		case C_NONE:
		default:
			/* this should never happen */
			g_assert(FALSE);
			break;
	}
}

void torflowbase_activate(TorFlowBase* tfb, gint sd, uint32_t events) {
	g_assert(tfb->internal->controld == sd);

	/* bootstrap */
	if(tfb->internal->controlState == C_NONE && (events & EPOLLOUT)) {
		/* our control socket is connected */
		g_queue_push_tail(tfb->internal->commands, g_string_new("AUTHENTICATE \"password\"\r\n"));
		tfb->internal->controlState = C_AUTH;
	}

	/* send all queued commands */
	if(events & EPOLLOUT) {
		tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "EPOLLOUT is set");

		while(!g_queue_is_empty(tfb->internal->commands)) {
			GString* command = g_queue_pop_head(tfb->internal->commands);

			gssize bytes = send(tfb->internal->controld, command->str, command->len, 0);

			if(bytes > 0) {
				/* at least some parts of the command were sent successfully */
				GString* sent = g_string_new(command->str);
				sent = g_string_truncate(sent, bytes);
				tfb->slogf(SHADOW_LOG_LEVEL_INFO, tfb->id, "torflow-sent '%s'", g_strchomp(sent->str));
				g_string_free(sent, TRUE);
			}

			if(bytes == command->len) {
				g_string_free(command, TRUE);
			} else {
				/* partial or no send */
				command = g_string_erase(command, (gssize)0, (gssize)bytes);
				g_queue_push_head(tfb->internal->commands, command);
				break;
			}
		}
		guint32 event = g_queue_is_empty(tfb->internal->commands) ? EPOLLIN : EPOLLOUT;
		torflowutil_epoll(tfb->internal->epolld, sd, EPOLL_CTL_MOD, event, tfb->slogf);
	}

	/* recv and process all incoming lines */
	if(events & EPOLLIN) {
		tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "EPOLLIN is set");

		gchar recvbuf[10240];
		memset(recvbuf, 0, 10240);
		gssize bytes = 0;

		while((bytes = recv(tfb->internal->controld, recvbuf, 10000, 0)) > 0) {
			recvbuf[bytes] = '\0';
			tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id,
					"recvbuf:%s",
					recvbuf);

			gboolean isLastLineIncomplete = FALSE;
			if(bytes < 2 || recvbuf[bytes-2] != '\r' || recvbuf[bytes-1] != '\n') {
				isLastLineIncomplete = TRUE;
			}

			//Check for corner case where first element is \r\n
			gboolean isStartCRLF = FALSE;
			if(recvbuf[0] == '\r' && recvbuf[1] == '\n') {
				isStartCRLF = TRUE;
			}

			gchar** lines = g_strsplit(recvbuf, "\r\n", 0);
			gchar* line = NULL;
			for(gint i = 0; (line = lines[i]) != NULL; i++) {
				if(!tfb->internal->receiveLineBuffer) {
					tfb->internal->receiveLineBuffer = g_string_new(line);
				} else if (isStartCRLF && i == 0 && 
						!g_ascii_strcasecmp(line, "")) {
					/* do nothing; we want to process the line in buffer already */
				} else {
					g_string_append_printf(tfb->internal->receiveLineBuffer, "%s", line);
				}

				if(!(isStartCRLF && i == 0) && (!g_ascii_strcasecmp(line, "") ||
						(isLastLineIncomplete && lines[i+1] == NULL))) {
					/* this is '', or the last line, and its not all here yet */
					continue;
				} else {
					/* we have a full line in our buffer */
					tfb->slogf(SHADOW_LOG_LEVEL_INFO, tfb->id, "torflow-recv '%s'", tfb->internal->receiveLineBuffer->str);
					_torflowbase_processLine(tfb, tfb->internal->receiveLineBuffer);
					g_string_free(tfb->internal->receiveLineBuffer, TRUE);
					tfb->internal->receiveLineBuffer = NULL;
				}
			}
			g_strfreev(lines);
		}
	}

	/* if we have commands to send, lets register for output event */
	if(!g_queue_is_empty(tfb->internal->commands)) {
		torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);
	}
}

void torflowbase_start(TorFlowBase* tfb) {
	g_assert(tfb);

	/* create the client socket and get a socket descriptor */
	tfb->internal->controld = socket(AF_INET, (SOCK_STREAM | SOCK_NONBLOCK), 0);
	if(tfb->internal->controld == -1) {
		tfb->slogf(SHADOW_LOG_LEVEL_ERROR, tfb->id,
				"unable to start client controld: error in socket");
		return;
	}

	/* our client socket address information for connecting to the server */
	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);;
	serverAddress.sin_port = tfb->internal->netControlPort;

	/* connect to server. since we are non-blocking, we expect this to return EINPROGRESS */
	gint res = connect(tfb->internal->controld, (struct sockaddr *) &serverAddress, sizeof(serverAddress));
	if (res == -1 && errno != EINPROGRESS) {
		tfb->slogf(SHADOW_LOG_LEVEL_ERROR, tfb->id,
				"unable to start client controld: error in connect");
		return;
	}

	/* specify the events to watch for on this socket.
	 * to start out, the client wants to know when it can send a message. */
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_ADD, EPOLLOUT, tfb->slogf);
	tfb->internal->controlState = C_NONE;
}

void torflowbase_init(TorFlowBase* tfb, TorFlowEventCallbacks* eventHandlers,
		ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf, in_port_t controlPort, gint epolld, gint workerID) {
	g_assert(tfb);
	g_assert(eventHandlers);
	g_assert(slogf && scbf);

	GString* idbuf = g_string_new(NULL);
	g_string_printf(idbuf, "TORFLOW-%u-%i", ntohs(controlPort), workerID);
	tfb->id = g_string_free(idbuf, FALSE);
	tfb->slogf = slogf;
	tfb->scbf = scbf;

	tfb->internal = g_new0(TorFlowBaseInternal, 1);
	tfb->internal->netControlPort = controlPort;
	tfb->internal->eventHandlers = *eventHandlers;
	tfb->internal->commands = g_queue_new();
	tfb->internal->epolld = epolld;
}

void torflowbase_free(TorFlowBase* tfb) {
	g_assert(tfb);

	if(tfb->internal->eventHandlers.onFree) {
        tfb->internal->eventHandlers.onFree(tfb);
	}

	if(tfb->internal->controld) {
		close(tfb->internal->controld);
	}

	if(tfb->internal->receiveLineBuffer) {
		g_string_free(tfb->internal->receiveLineBuffer, TRUE);
	}

	while(!g_queue_is_empty(tfb->internal->commands)) {
		g_string_free(g_queue_pop_head(tfb->internal->commands), TRUE);
	}
	g_queue_free(tfb->internal->commands);

	if(tfb->internal->cachedCircPath) {
		g_free(tfb->internal->cachedCircPath);
	}

	g_free(tfb->internal);

	if(tfb->id) {
		g_free(tfb->id);
	}

	g_free(tfb);
}

gint torflowbase_getControlSD(TorFlowBase* tfb) {
	g_assert(tfb);
	return tfb->internal->controld;
}

const gchar* torflowbase_getCurrentPath(TorFlowBase* tfb) {
	g_assert(tfb);
	return tfb->internal->cachedCircPath;
}

void torflowbase_requestInfo(TorFlowBase* tfb) {
	g_assert(tfb);

	GString* command = g_string_new(NULL);
	//g_string_printf(command, "GETINFO dir/status-vote/current/consensus\r\n");
	g_string_printf(command, "GETINFO ns/all\r\n");
	g_queue_push_tail(tfb->internal->commands, command);
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);
	tfb->internal->readyForDescriptors = TRUE;

	tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "queued a GETINFO command");
}

gboolean torflowbase_buildNewMeasurementCircuit(TorFlowBase* tfb, gchar* path) {
	g_assert(tfb);
	if (!path) {
		return FALSE;
	}

	GString* command = g_string_new(NULL);
	g_string_printf(command, "EXTENDCIRCUIT 0 %s\r\n", path);
	g_queue_push_tail(tfb->internal->commands, command);
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);

	tfb->internal->waitingMeasurementCircuit = TRUE;

	if(tfb->internal->cachedCircPath) {
        g_free(tfb->internal->cachedCircPath);
        tfb->internal->cachedCircId = 0;
    }
	tfb->internal->cachedCircPath = path;

	tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "queued a EXTENDCIRCUIT command for %s", path);
	return TRUE;
}

void torflowbase_closeCircuit(TorFlowBase* tfb, gint circid) {
	g_assert(tfb);

	GString* command = g_string_new(NULL);
	g_string_printf(command, "CLOSECIRCUIT %i\r\n", circid);
	g_queue_push_tail(tfb->internal->commands, command);
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);

	tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "queued a CLOSECIRCUIT command for %i", circid);
}

void torflowbase_attachStreamToCircuit(TorFlowBase* tfb, gint streamid, gint circid) {
	g_assert(tfb);

	GString* command = g_string_new(NULL);
	g_string_printf(command, "ATTACHSTREAM %i %i\r\n", streamid, circid);
	g_queue_push_tail(tfb->internal->commands, command);
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);

	tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "queued a ATTACHSTREAM command for stream %i to circuit %i", streamid, circid);
}

void torflowbase_enableCircuits(TorFlowBase* tfb) {
	g_assert(tfb);

	GString* command = g_string_new("SETCONF __DisablePredictedCircuits=0\r\n");
	g_queue_push_tail(tfb->internal->commands, command);
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);

	tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "now allowing predicted circuits");
}

void torflowbase_closeStreams(TorFlowBase* tfb, gchar* addressString) {
	g_assert(tfb);

	GString* command = g_string_new(NULL);
	g_string_printf(command, "CLOSEALLSTREAMS %s\r\n", addressString);
	g_queue_push_tail(tfb->internal->commands, command);
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);

	tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "closing streams to %s", addressString);
}

void torflowbase_ignorePackageWindows(TorFlowBase* tfb, gint circid) {
	g_assert(tfb);

	GString* command = g_string_new(NULL);
	g_string_printf(command, "IGNOREPACKAGEWINDOW %i\r\n", circid);
	g_queue_push_tail(tfb->internal->commands, command);
	torflowutil_epoll(tfb->internal->epolld, tfb->internal->controld, EPOLL_CTL_MOD, EPOLLOUT, tfb->slogf);

	tfb->slogf(SHADOW_LOG_LEVEL_DEBUG, tfb->id, "ignoring package windows for circ %i and its streams", circid);
}
