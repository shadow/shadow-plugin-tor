/*
 * See LICENSE for licensing information
 */

#ifndef TORFLOW_H_
#define TORFLOW_H_

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "torflow-peer.h"
#include "torflow-config.h"
#include "torflow-event-manager.h"
#include "torflow-timer.h"
#include "torflow-relay.h"
#include "torflow-slice.h"
#include "torflow-database.h"
#include "torflow-torctl-client.h"
#include "torflow-probe.h"
#include "torflow-authority.h"
#include "torflow-file-server.h"
#include "torflow-file-listener.h"
#include "torflow-file-client.h"

/* logging facility */
void torflow_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...);

#define debug(...) torflow_log(G_LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__)
#define info(...) torflow_log(G_LOG_LEVEL_INFO, __FUNCTION__, __VA_ARGS__)
#define message(...) torflow_log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, __VA_ARGS__)
#define warning(...) torflow_log(G_LOG_LEVEL_WARNING, __FUNCTION__, __VA_ARGS__)
#define critical(...) torflow_log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, __VA_ARGS__)
#define error(...) torflow_log(G_LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__)

#endif /* TORFLOW_H_ */
