/*
 * The Shadow Simulator
 * Copyright (c) 2010-2011, Rob Jansen
 * See LICENSE for licensing information
 */

#include "shadowtor.h"

static void _shadowtor_secondCallback(ScallionTor* stor) {
	shadowtor_notify(stor);

	/* call Tor's second elapsed function */
	second_elapsed_callback(NULL, NULL);

	/* make sure we handle any event creations that happened in Tor */
	shadowtor_notify(stor);

	/* schedule the next callback */
	if(stor) {
		stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)_shadowtor_secondCallback,
				stor, 1000);
	}
}

#ifdef SCALLION_DOREFILLCALLBACKS
static void _shadowtor_refillCallback(ScallionTor* stor) {
	shadowtor_notify(stor);

	/* call Tor's refill function */
	refill_callback(NULL, NULL);

        /* notify stream BW events */
        control_event_stream_bandwidth_used();

	/* make sure we handle any event creations that happened in Tor */
	shadowtor_notify(stor);

	/* schedule the next callback */
	if(stor) {
		stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)_shadowtor_refillCallback,
				stor, stor->refillmsecs);
	}
}
#endif

ScallionTor* shadowtor_getPointer() {
	return shadowtor.stor;
}

static void shadowtor_logmsg_cb(int severity, uint32_t domain, const char *msg) {
	ShadowLogLevel level;
	switch (severity) {
		case LOG_DEBUG:
			level = SHADOW_LOG_LEVEL_DEBUG;
		break;
		case LOG_INFO:
			level = SHADOW_LOG_LEVEL_INFO;
		break;
		case LOG_NOTICE:
			level = SHADOW_LOG_LEVEL_MESSAGE;
		break;
		case LOG_WARN:
			level = SHADOW_LOG_LEVEL_WARNING;
		break;
		case LOG_ERR:
			level = SHADOW_LOG_LEVEL_ERROR;
		break;
		default:
			level = SHADOW_LOG_LEVEL_DEBUG;
		break;
	}
	gchar* msg_dup = g_strdup(msg);
	shadowtor_getPointer()->shadowlibFuncs->log(level, __FUNCTION__, "%s", g_strchomp(msg_dup));
	g_free(msg_dup);
}

void shadowtor_setLogging() {
	/* setup a callback so we can log into shadow */
    log_severity_list_t *severity = g_new0(log_severity_list_t, 1);
    /* we'll log everything according to Shadow's filter */
    set_log_severity_config(LOG_DEBUG, LOG_ERR, severity);
    add_callback_log(severity, shadowtor_logmsg_cb);
    g_free(severity);
}

void shadowtor_trackConsensusUpdate(const char* filepath, const char* contents) {
    /* check if filepath is a consenus file. store it in separate files
     * so we don't lose old consenus info on overwrites. */
    if(g_str_has_suffix(filepath, "cached-consensus")) {
    //if(g_strrstr(filepath, "cached-consensus") != NULL) {
        GString* newPath = g_string_new(filepath);
        GError* error = NULL;
        g_string_append_printf(newPath, ".%03i", shadowtor.consensusCounter++);
        if(!g_file_set_contents(newPath->str, contents, -1, &error)) {
            log_warn(LD_GENERAL,"Error writing file '%s' to track consensus update: error %i: %s",
                    newPath->str, error->code, error->message);
        }
        g_string_free(newPath, TRUE);
    }
}

int shadowtor_openSocket(int domain, int type, int protocol) {
    int s = socket(domain, type | SOCK_NONBLOCK, protocol);
    if (s >= 0) {
      socket_accounting_lock();
      ++n_sockets_open;
      // mark_socket_open(s);
      socket_accounting_unlock();
    }
    return s;
}

gint shadowtor_start(ScallionTor* stor, gint argc, gchar *argv[]) {
	time_t now = time(NULL);

	update_approx_time(now);
	tor_threads_init();

#ifdef STARTUP_Q_PARAM
	init_logging(0);
#else
	init_logging();
	shadowtor_setLogging();
#endif

	if (tor_init(argc, argv) < 0) {
		return -1;
	}

	  /* load the private keys, if we're supposed to have them, and set up the
	   * TLS context. */
	gpointer idkey;
#ifdef SCALLION_NEWIDKEYNAME
	idkey = client_identitykey;
#else
	idkey = identitykey;
#endif
    if (idkey == NULL) {
	  if (init_keys() < 0) {
	    log_err(LD_BUG,"Error initializing keys; exiting");
	    return -1;
	  }
    }

    /* Set up the packed_cell_t memory pool. */
#ifdef SCALLION_MEMPOOLOPT
#ifdef ENABLE_MEMPOOLS
	init_cell_pool();
#endif
#else
	init_cell_pool();
#endif

	/* Set up our buckets */
	connection_bucket_init();
	stats_prev_global_read_bucket = global_read_bucket;
	stats_prev_global_write_bucket = global_write_bucket;

	/* initialize the bootstrap status events to know we're starting up */
	control_event_bootstrap(BOOTSTRAP_STATUS_STARTING, 0);

	if (trusted_dirs_reload_certs()) {
		log_warn(LD_DIR,
			 "Couldn't load all cached v3 certificates. Starting anyway.");
	}
#ifndef SCALLION_NOV2DIR
	if (router_reload_v2_networkstatus()) {
		return -1;
	}
#endif
	if (router_reload_consensus_networkstatus()) {
		return -1;
	}

	/* load the routers file, or assign the defaults. */
	if (router_reload_router_list()) {
		return -1;
	}

	/* load the networkstatuses. (This launches a download for new routers as
	* appropriate.)
	*/
	directory_info_has_arrived(now, 1);

	/* !note that we intercept the cpuworker functionality (rob) */
	if (server_mode(get_options())) {
		/* launch cpuworkers. Need to do this *after* we've read the onion key. */
		cpu_init();
	}

	/* set up once-a-second callback. */
	if (! second_timer) {
//		struct timeval one_second;
//		one_second.tv_sec = 1;
//		one_second.tv_usec = 0;
//
//		second_timer = periodic_timer_new(tor_libevent_get_base(),
//										  &one_second,
//										  second_elapsed_callback,
//										  NULL);
//		tor_assert(second_timer);

		_shadowtor_secondCallback(stor);
	}


#ifdef SCALLION_DOREFILLCALLBACKS
#ifndef USE_BUFFEREVENTS
  if (!refill_timer) {
    int msecs = get_options()->TokenBucketRefillInterval;
//    struct timeval refill_interval;
//
//    refill_interval.tv_sec =  msecs/1000;
//    refill_interval.tv_usec = (msecs%1000)*1000;
//
//    refill_timer = periodic_timer_new(tor_libevent_get_base(),
//                                      &refill_interval,
//                                      refill_callback,
//                                      NULL);
//    tor_assert(refill_timer);
    stor->refillmsecs = msecs;
	_shadowtor_refillCallback(stor);
  }
#endif
#endif

    /* run the startup events */
    shadowtor_notify(stor);

	return 0;
}

static gchar* _shadowtor_getFormatedArg(gchar* argString, const gchar* home, const gchar* hostname) {
	gchar* found = NULL;
	GString* sbuffer = g_string_new(argString);

	/* replace all ~ with the home directory */
	while((found = g_strstr_len(sbuffer->str, sbuffer->len, "~"))) {
		gssize position = (gssize) (found - sbuffer->str);
		sbuffer = g_string_erase(sbuffer, position, (gssize) 1);
		sbuffer = g_string_insert(sbuffer, position, home);
	}

	/* replace all ${NODEID} with the hostname */
	while((found = g_strstr_len(sbuffer->str, sbuffer->len, "${NODEID}"))) {
		gssize position = (gssize) (found - sbuffer->str);
		sbuffer = g_string_erase(sbuffer, position, (gssize) 9);
		sbuffer = g_string_insert(sbuffer, position, hostname);
	}

	return g_string_free(sbuffer, FALSE);
}

ScallionTor* shadowtor_new(ShadowFunctionTable* shadowlibFuncs, gchar* hostname,
		gint torargc, gchar* torargv[]) {
	ScallionTor* stor = g_new0(ScallionTor, 1);
	stor->shadowlibFuncs = shadowlibFuncs;

	/* get formatted argument vector by expanding '~' and '${NODEID}' */
	gchar* formattedArgs[torargc+1];
	/* tor ignores the first arg */
	formattedArgs[0] = g_strdup("tor");

	/* create the new strings */
	const gchar* homeDirectory = g_get_home_dir();
	for(gint i = 0; i < torargc; i++) {
		formattedArgs[i+1] = _shadowtor_getFormatedArg(torargv[i], homeDirectory, hostname);
	}

	/* initialize tor */
	shadowtor.stor = stor;
	shadowtor_start(stor, torargc+1, formattedArgs);

	/* free the new strings */
	for(gint i = 0; i < torargc+1; i++) {
		g_free(formattedArgs[i]);
	}

	return stor;
}

void shadowtor_notify(ScallionTor* stor) {
	update_approx_time(time(NULL));

	/* tell libevent to check epoll and activate the ready sockets without blocking */
	event_base_loop(tor_libevent_get_base(), EVLOOP_NONBLOCK);
}

/*
 * normally tor calls event_base_loopexit so control returns from the libevent
 * event loop back to the tor main loop. tor then activates "linked" socket
 * connections before returning back to the libevent event loop.
 *
 * we hijack and use the libevent loop in nonblock mode, so when tor calls
 * the loopexit, we basically just need to do the linked connection activation.
 * that is extracted to shadowtor_loopexitCallback, which we need to execute
 * as a callback so we don't invoke event_base_loop while it is currently being
 * executed. */
static void shadowtor_loopexitCallback(ScallionTor* stor) {
	update_approx_time(time(NULL));

	shadowtor_notify(stor);

	while(1) {
		/* All active linked conns should get their read events activated. */
		SMARTLIST_FOREACH(active_linked_connection_lst, connection_t *, conn,
				event_active(conn->read_event, EV_READ, 1));

		/* if linked conns are still active, enter libevent loop using EVLOOP_ONCE */
		called_loop_once = smartlist_len(active_linked_connection_lst) ? 1 : 0;
		if(called_loop_once) {
			event_base_loop(tor_libevent_get_base(), EVLOOP_ONCE|EVLOOP_NONBLOCK);
		} else {
			/* linked conns are done */
			break;
		}
	}

	/* make sure we handle any new events caused by the linked conns */
	shadowtor_notify(stor);
}
void shadowtor_loopexit() {
    ScallionTor* stor = shadowtor_getPointer();
    g_assert(stor);
	stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)shadowtor_loopexitCallback, (gpointer)stor, 1);
}

/* return -1 to kill, 0 for EAGAIN, bytes read/written for success */
static int shadowtor_checkIOResult(int fd, int ioResult) {
	if(ioResult < 0) {
		if(errno == EAGAIN) {
			/* dont block! and dont fail! */
			return 0;
		} else {
			/* true error from shadow network layer */
			log_info(LD_OR,
					 "CPU worker exiting because of error on connection to Tor "
					 "process.");
			log_info(LD_OR,"(Error on %d was %s)",
					fd, tor_socket_strerror(tor_socket_errno(fd)));
			return -1;
		}
	} else if (ioResult == 0) {
		log_info(LD_OR,
				 "CPU worker exiting because Tor process closed connection "
				 "(either rotated keys or died).");
		return -1;
	}

	return ioResult;
}

#ifdef SCALLION_USEV2CPUWORKER
void shadowtor_readCPUWorkerCallback(int sockd, short ev_types, void * arg) {
	vtor_cpuworker_tp cpuw = arg;

enter:
	SCALLION_CPUWORKER_ASSERT(cpuw);
	if(cpuw->state == CPUW_NONE) {
		cpuw->state = CPUW_V2_READ;
	}

	switch (cpuw->state) {
	case CPUW_V2_READ: {
		size_t req_size = sizeof(cpuworker_request_t);
		char recvbuf[req_size];

		/* read until we have a full request */
		while(cpuw->num_partial_bytes < req_size) {
			memset(recvbuf, 0, req_size);
			size_t bytes_needed = req_size - cpuw->num_partial_bytes;

			int ioResult = recv(cpuw->fd, recvbuf, bytes_needed, 0);
//			int ioResult = recv(cpuw->fd, (&(cpuw->req))+cpuw->offset, bytesNeeded-cpuw->offset, 0);

			ioResult = shadowtor_checkIOResult(cpuw->fd, ioResult);
			if(ioResult < 0) goto end; // error, kill ourself
			else if(ioResult == 0) goto ret; // EAGAIN
			else g_assert(ioResult > 0); // yay

			/* we read some bytes */
			size_t bytes_read = (size_t)ioResult;
			g_assert(bytes_read <= bytes_needed);

			/* copy these bytes into our request buffer */
			gpointer req_loc = (gpointer) &(cpuw->req);
			gpointer req_w_loc = &req_loc[cpuw->num_partial_bytes];

			SCALLION_CPUWORKER_ASSERT(cpuw);
			memcpy(req_w_loc, recvbuf, bytes_read);
			SCALLION_CPUWORKER_ASSERT(cpuw);

			cpuw->num_partial_bytes += bytes_read;
			g_assert(cpuw->num_partial_bytes <= req_size);
		}

		/* we got what we needed, assert this */
		if(cpuw->num_partial_bytes == req_size) {
			/* got full request, process it */
			cpuw->state = CPUW_V2_PROCESS;
			cpuw->num_partial_bytes = 0;
			goto enter;
		} else {
		  log_err(LD_BUG,"read tag failed. Exiting.");
		  goto end;
		}
	}

	case CPUW_V2_PROCESS: {
		tor_assert(cpuw->req.magic == CPUWORKER_REQUEST_MAGIC);

		SCALLION_CPUWORKER_ASSERT(cpuw);
		memset(&(cpuw->rpl), 0, sizeof(cpuworker_reply_t));
		SCALLION_CPUWORKER_ASSERT(cpuw);

		if (cpuw->req.task == CPUWORKER_TASK_ONION) {
			const create_cell_t *cc = &cpuw->req.create_cell;
			created_cell_t *cell_out = &cpuw->rpl.created_cell;
			int n = 0;
#ifdef SCALLION_USEV2CPUWORKERTIMING
			struct timeval tv_start, tv_end;
			cpuw->rpl.timed = cpuw->req.timed;
			cpuw->rpl.started_at = cpuw->req.started_at;
			cpuw->rpl.handshake_type = cc->handshake_type;
			if (cpuw->req.timed)
			  tor_gettimeofday(&tv_start);
#endif
			n = onion_skin_server_handshake(cc->handshake_type, cc->onionskin,
					cc->handshake_len, &cpuw->onion_keys, cell_out->reply,
					cpuw->rpl.keys, CPATH_KEY_MATERIAL_LEN,
					cpuw->rpl.rend_auth_material);
			if (n < 0) {
				/* failure */
				log_debug(LD_OR, "onion_skin_server_handshake failed.");
				memset(&cpuw->rpl, 0, sizeof(cpuworker_reply_t));
				memcpy(cpuw->rpl.tag, cpuw->req.tag, TAG_LEN);
				cpuw->rpl.success = 0;
			} else {
				/* success */
				log_debug(LD_OR, "onion_skin_server_handshake succeeded.");
				memcpy(cpuw->rpl.tag, cpuw->req.tag, TAG_LEN);
				cell_out->handshake_len = n;
				switch (cc->cell_type) {
				case CELL_CREATE:
					cell_out->cell_type = CELL_CREATED;
					break;
				case CELL_CREATE2:
					cell_out->cell_type = CELL_CREATED2;
					break;
				case CELL_CREATE_FAST:
					cell_out->cell_type = CELL_CREATED_FAST;
					break;
				default:
					tor_assert(0);
					goto end;
				}
				cpuw->rpl.success = 1;
			}
			cpuw->rpl.magic = CPUWORKER_REPLY_MAGIC;
#ifdef SCALLION_USEV2CPUWORKERTIMING
			if (cpuw->req.timed) {
			  struct timeval tv_diff;
			  tor_gettimeofday(&tv_end);
			  timersub(&tv_end, &tv_start, &tv_diff);
			  int64_t usec = (int64_t)(((int64_t)tv_diff.tv_sec)*1000000 + tv_diff.tv_usec);
/** If any onionskin takes longer than this, we clip them to this
* time. (microseconds) */
#define MAX_BELIEVABLE_ONIONSKIN_DELAY (2*1000*1000)
			  if (usec < 0 || usec > MAX_BELIEVABLE_ONIONSKIN_DELAY)
				cpuw->rpl.n_usec = MAX_BELIEVABLE_ONIONSKIN_DELAY;
			  else
				cpuw->rpl.n_usec = (uint32_t) usec;
			  }
#endif
			/* write response after processing request */
			SCALLION_CPUWORKER_ASSERT(cpuw);
			cpuw->state = CPUW_V2_WRITE;
		} else if (cpuw->req.task == CPUWORKER_TASK_SHUTDOWN) {
			log_info(LD_OR, "Clean shutdown: exiting");
			cpuw->state = CPUW_NONE;
			goto end;
		} else {
			/* dont know the task, just ignore it and start over reading the next */
			cpuw->state = CPUW_V2_RESET;
		}

		goto enter;
	}

	case CPUW_V2_WRITE: {
		size_t rpl_size = sizeof(cpuworker_reply_t);
		char sendbuf[rpl_size];
		memset(sendbuf, 0, rpl_size);

		/* copy reply into send buffer */
		SCALLION_CPUWORKER_ASSERT(cpuw);
		memcpy(sendbuf, (gpointer) &(cpuw->rpl), rpl_size);
		SCALLION_CPUWORKER_ASSERT(cpuw);

		/* write until we wrote it all */
		while(cpuw->num_partial_bytes < rpl_size) {
			size_t bytes_needed = rpl_size - cpuw->num_partial_bytes;
			gpointer rpl_loc = (gpointer) sendbuf;
			gpointer rpl_r_loc = &rpl_loc[cpuw->num_partial_bytes];

			int ioResult = send(cpuw->fd, rpl_r_loc, bytes_needed, 0);

			ioResult = shadowtor_checkIOResult(cpuw->fd, ioResult);
			if(ioResult < 0) goto end; // error, kill ourself
			else if(ioResult == 0) goto ret; // EAGAIN
			else g_assert(ioResult > 0); // yay

			/* we wrote some bytes */
			size_t bytes_written = (size_t)ioResult;
			g_assert(bytes_written <= bytes_needed);

			cpuw->num_partial_bytes += bytes_written;
			g_assert(cpuw->num_partial_bytes <= rpl_size);
		}

		/* we sent what we needed, assert this */
		if(cpuw->num_partial_bytes == rpl_size) {
			/* sent full reply, start over */
			log_debug(LD_OR, "finished writing response.");
			cpuw->state = CPUW_V2_RESET;
			cpuw->num_partial_bytes = 0;
			goto enter;
		} else {
			log_err(LD_BUG,"writing response buf failed. Exiting.");
			goto end;
		}
	}

	case CPUW_V2_RESET: {
		memwipe(&cpuw->req, 0, sizeof(cpuworker_request_t));
		memwipe(&cpuw->rpl, 0, sizeof(cpuworker_reply_t));
		cpuw->state = CPUW_V2_READ;
		cpuw->num_partial_bytes = 0;
		goto enter;
	}
	}

ret:
	return;

end:
	if (cpuw != NULL) {
	    cpuw->state = CPUW_DEAD;
	}
}
#else
void shadowtor_readCPUWorkerCallback(int sockd, short ev_types, void * arg) {
	/* adapted from cpuworker_main.
	 *
	 * these are blocking calls in Tor. we need to cope, so the approach we
	 * take is that if the first read would block, its ok. after that, we
	 * continue through the state machine until we are able to read and write
	 * everything we need to, then reset and start with the next question.
	 *
	 * this is completely nonblocking with the state machine.
	 */
	vtor_cpuworker_tp cpuw = arg;
	g_assert(cpuw);

	if(cpuw->state == CPUW_NONE) {
		cpuw->state = CPUW_READTYPE;
	}

	int ioResult = 0;
	int action = 0;

enter:

	switch(cpuw->state) {
		case CPUW_READTYPE: {
			ioResult = 0;

			/* get the type of question */
			ioResult = recv(cpuw->fd, &(cpuw->question_type), 1, 0);

			action = shadowtor_checkIOResult(cpuw, ioResult);
			if(action == -1) goto kill;
			else if(action == 0) goto exit;

			/* we got our initial question type */
			tor_assert(cpuw->question_type == CPUWORKER_TASK_ONION);

			cpuw->state = CPUW_READTAG;
			goto enter;
		}

		case CPUW_READTAG: {
			ioResult = 0;
			action = 1;
			int bytesNeeded = TAG_LEN;

			while(action > 0 && cpuw->offset < bytesNeeded) {
				ioResult = recv(cpuw->fd, cpuw->tag+cpuw->offset, bytesNeeded-cpuw->offset, 0);

				action = shadowtor_checkIOResult(cpuw, ioResult);
				if(action == -1) goto kill;
				else if(action == 0) goto exit;

				/* read some bytes */
				cpuw->offset += action;
			}

			/* we got what we needed, assert this */
			if (cpuw->offset != TAG_LEN) {
			  log_err(LD_BUG,"read tag failed. Exiting.");
			  goto kill;
			}

			cpuw->state = CPUW_READCHALLENGE;
			cpuw->offset = 0;
			goto enter;
		}

		case CPUW_READCHALLENGE: {
			ioResult = 0;
			action = 1;
			int bytesNeeded = ONIONSKIN_CHALLENGE_LEN;

			while(action > 0 && cpuw->offset < bytesNeeded) {
				ioResult = recv(cpuw->fd, cpuw->question+cpuw->offset, bytesNeeded-cpuw->offset, 0);

				action = shadowtor_checkIOResult(cpuw, ioResult);
				if(action == -1) goto kill;
				else if(action == 0) goto exit;

				/* read some bytes */
				cpuw->offset += action;
			}

			/* we got what we needed, assert this */
			if (cpuw->offset != ONIONSKIN_CHALLENGE_LEN) {
			  log_err(LD_BUG,"read question failed. got %i bytes, expecting %i bytes. Exiting.", cpuw->offset, ONIONSKIN_CHALLENGE_LEN);
			  goto kill;
			}

			cpuw->state = CPUW_PROCESS;
			cpuw->offset = 0;
			goto enter;
		}

		case CPUW_PROCESS: {
			if (cpuw->question_type != CPUWORKER_TASK_ONION) {
				log_debug(LD_OR,"unknown CPU worker question type. ignoring...");
				cpuw->state = CPUW_READTYPE;
				cpuw->offset = 0;
				goto exit;
			}


			int r = onion_skin_server_handshake(cpuw->question, cpuw->onion_key, cpuw->last_onion_key,
					  cpuw->reply_to_proxy, cpuw->keys, CPATH_KEY_MATERIAL_LEN);

			if (r < 0) {
				/* failure */
				log_debug(LD_OR,"onion_skin_server_handshake failed.");
				*(cpuw->buf) = 0; /* indicate failure in first byte */
				memcpy(cpuw->buf+1,cpuw->tag,TAG_LEN);
				/* send all zeros as answer */
				memset(cpuw->buf+1+TAG_LEN, 0, LEN_ONION_RESPONSE-(1+TAG_LEN));
			} else {
				/* success */
				log_debug(LD_OR,"onion_skin_server_handshake succeeded.");
				cpuw->buf[0] = 1; /* 1 means success */
				memcpy(cpuw->buf+1,cpuw->tag,TAG_LEN);
				memcpy(cpuw->buf+1+TAG_LEN,cpuw->reply_to_proxy,ONIONSKIN_REPLY_LEN);
				memcpy(cpuw->buf+1+TAG_LEN+ONIONSKIN_REPLY_LEN,cpuw->keys,CPATH_KEY_MATERIAL_LEN);
			}

			cpuw->state = CPUW_WRITERESPONSE;
			cpuw->offset = 0;
			goto enter;
		}

		case CPUW_WRITERESPONSE: {
			ioResult = 0;
			action = 1;
			int bytesNeeded = LEN_ONION_RESPONSE;

			while(action > 0 && cpuw->offset < bytesNeeded) {
				ioResult = send(cpuw->fd, cpuw->buf+cpuw->offset, bytesNeeded-cpuw->offset, 0);

				action = shadowtor_checkIOResult(cpuw, ioResult);
				if(action == -1) goto kill;
				else if(action == 0) goto exit;

				/* wrote some bytes */
				cpuw->offset += action;
			}

			/* we wrote what we needed, assert this */
			if (cpuw->offset != LEN_ONION_RESPONSE) {
				log_err(LD_BUG,"writing response buf failed. Exiting.");
				goto kill;
			}

			log_debug(LD_OR,"finished writing response.");

			cpuw->state = CPUW_READTYPE;
			cpuw->offset = 0;
			goto enter;
		}

		default: {
			log_err(LD_BUG,"unknown CPU worker state. Exiting.");
			goto kill;
		}
	}

exit:
	return;

kill:
	if(cpuw != NULL) {
		cpuw->state = CPUW_DEAD;
	}
}
#endif

static void _shadowtor_freeCPUWorker(vtor_cpuworker_tp cpuw) {
    if(!cpuw) return;

#ifdef SCALLION_USEV2CPUWORKER
    memwipe(&cpuw->req, 0, sizeof(cpuw->req));
    memwipe(&cpuw->rpl, 0, sizeof(cpuw->rpl));
    release_server_onion_keys(&cpuw->onion_keys);
    tor_close_socket(cpuw->fd);
    event_del(&(cpuw->read_event));
    memset(cpuw, 0, sizeof(vtor_cpuworker_t));
    free(cpuw);
#else
    if (cpuw->onion_key)
        crypto_pk_free(cpuw->onion_key);
    if (cpuw->last_onion_key)
        crypto_pk_free(cpuw->last_onion_key);
    tor_close_socket(cpuw->fd);
    event_del(&(cpuw->read_event));
    free(cpuw);
#endif
}

void shadowtor_free(ScallionTor* stor) {
    tor_cleanup();
    if(stor->cpuWorkers != NULL) {
        g_slist_free_full(stor->cpuWorkers, (GDestroyNotify)_shadowtor_freeCPUWorker);
    }
    g_free(stor);
}

static void _shadowtor_freeDeadCPUWorkers(ScallionTor* stor) {
	/* check for dead workers and free them */
	GSList* next = stor->cpuWorkers;
	while(next != NULL) {
	    vtor_cpuworker_tp cpuw = (vtor_cpuworker_tp)next->data;

	    /* free the worker */
	    if(cpuw != NULL && cpuw->state == CPUW_DEAD) {
	        stor->cpuWorkers = g_slist_delete_link(stor->cpuWorkers, next);
	        _shadowtor_freeCPUWorker(cpuw);
	    }

	    next = g_slist_next(next);
	}
}

void shadowtor_newCPUWorker(int fd) {
    ScallionTor* stor = shadowtor_getPointer();
	g_assert(stor);

	_shadowtor_freeDeadCPUWorkers(stor);

	vtor_cpuworker_tp cpuw = calloc(1, sizeof(vtor_cpuworker_t));
	g_slist_append(stor->cpuWorkers, cpuw);

	cpuw->fd = fd;
	cpuw->state = CPUW_NONE;
#ifdef SCALLION_USEV2CPUWORKER
	cpuw->magic1 = SCALLION_CPUWORKER_MAGIC1;
	cpuw->magic2 = SCALLION_CPUWORKER_MAGIC2;
	cpuw->magic3 = SCALLION_CPUWORKER_MAGIC3;

	setup_server_onion_keys(&(cpuw->onion_keys));
#else
	dup_onion_keys(&(cpuw->onion_key), &(cpuw->last_onion_key));
#endif

	/* setup event so we will get a callback */
	event_assign(&(cpuw->read_event), tor_libevent_get_base(), cpuw->fd, EV_READ|EV_PERSIST, shadowtor_readCPUWorkerCallback, cpuw);
	event_add(&(cpuw->read_event), NULL);

	log_notice(LD_GENERAL, "cpuworker launched!");
}

static void shadowtor_runSockMgrWorker(ScallionTor* stor) {
    int64_t pausetime = 0;
    int keep_running = sockmgr_thread_loop_once(&pausetime);
    if(keep_running) {
        stor->shadowlibFuncs->createCallback((ShadowPluginCallbackFunc)shadowtor_runSockMgrWorker,
                        stor, pausetime);
    }
}

void shadowtor_newSockMgrWorker() {
    ScallionTor* stor = shadowtor_getPointer();
    g_assert(stor);
    log_notice(LD_GENERAL, "sockmgr launched!");
    shadowtor_runSockMgrWorker(stor);
}
