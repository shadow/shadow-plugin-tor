/*
 * See LICENSE for licensing information
 */

#ifndef TORFLOW_H_
#define TORFLOW_H_

/* How many times we need to measure each relay before a slice is done */
#define MEASUREMENTS_PER_SLICE 5
/* Time in seconds for a worker with no slices to wait before trying again */
#define WORKER_RETRY_TIME 300
/* Time in seconds to wait before calling a failed download a timeout */
#define DOWNLOAD_TIMEOUT 300

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <math.h>

#include <glib.h>
#include <glib/gstdio.h>

/* logging facility */
void torflowmain_log(GLogLevelFlags level, const gchar* functionName, const gchar* format, ...);

#define debug(...) torflowmain_log(G_LOG_LEVEL_DEBUG, __FUNCTION__, __VA_ARGS__)
#define info(...) torflowmain_log(G_LOG_LEVEL_INFO, __FUNCTION__, __VA_ARGS__)
#define message(...) torflowmain_log(G_LOG_LEVEL_MESSAGE, __FUNCTION__, __VA_ARGS__)
#define warning(...) torflowmain_log(G_LOG_LEVEL_WARNING, __FUNCTION__, __VA_ARGS__)
#define critical(...) torflowmain_log(G_LOG_LEVEL_CRITICAL, __FUNCTION__, __VA_ARGS__)
#define error(...) torflowmain_log(G_LOG_LEVEL_ERROR, __FUNCTION__, __VA_ARGS__)

typedef struct _TorFlowFileServer TorFlowFileServer;

typedef struct _TorFlowRelay {
	GString * nickname;
	GString * identity;
	gint descriptorBandwidth;
	gint advertisedBandwidth;
	gint newBandwidth;
	gboolean isExit;
	gboolean isRunning;
	gboolean isFast;
	gint measureCount;
	GSList* t_rtt;
	GSList* t_payload;
	GSList* t_total;
	GSList* bytesPushed;
} TorFlowRelay;

typedef struct _TorFlowSlice {
    guint sliceNumber;
    gchar* filename;

    /* all the relays assigned to this slice */
    GSList* allRelays;
    guint allRelaysLength;
    GSList* exitRelays;
    guint exitRelaysLength;
    GSList* entryRelays;
    guint entryRelaysLength;
    TorFlowRelay* currentExitRelay;
    TorFlowRelay* currentEntryRelay;
} TorFlowSlice;

typedef void (*ReadyFunc)(gpointer data);
typedef void (*BootstrapCompleteFunc)(gpointer data);
typedef void (*DescriptorsReceivedFunc)(gpointer data1, GQueue* descriptorLines);
typedef void (*MeasurementCircuitBuiltFunc)(gpointer data, gint circid);
typedef void (*StreamNewFunc)(gpointer data, gint streamid, gint circid,
        gchar* targetAddress, gint targetPort, gchar* sourceAddress, gint sourcePort);
typedef void (*StreamSucceededFunc)(gpointer data, gint streamid, gint circid, gchar* targetAddress, gint targetPort);
typedef void (*FileServerConnectedFunc)(gpointer data, gint socksd);
typedef void (*FileServerTimeoutFunc)(gpointer data);
typedef void (*FileDownloadCompleteFunc)(gpointer data, gint contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime);
typedef void (*FreeFunc)(gpointer data);
typedef struct _TorFlowEventCallbacks {
	BootstrapCompleteFunc onBootstrapComplete;
	DescriptorsReceivedFunc onDescriptorsReceived;
	MeasurementCircuitBuiltFunc onMeasurementCircuitBuilt;
	StreamNewFunc onStreamNew;
	StreamSucceededFunc onStreamSucceeded;
	FileServerConnectedFunc onFileServerConnected;
	FileServerTimeoutFunc onFileServerTimeout;
	FileDownloadCompleteFunc onFileDownloadComplete;
	FreeFunc onFree;
} TorFlowEventCallbacks;


/* timers */
typedef struct _TorFlowTimer TorFlowTimer;
TorFlowTimer* torflowtimer_new(GFunc func, gpointer arg1, gpointer arg2);
void torflowtimer_arm(TorFlowTimer* timer, guint timeoutSeconds);
gboolean torflowtimer_check(TorFlowTimer* timer);
gint torflowtimer_getFD(TorFlowTimer* timer);
void torflowtimer_free(TorFlowTimer* timer);


typedef struct _TorFlowManager TorFlowManager;

typedef struct _TorFlowBase TorFlowBase;
typedef struct _TorFlowBaseInternal TorFlowBaseInternal;
struct _TorFlowBase {
	TorFlowBaseInternal* internal;
	gchar* id;
};

void torflowbase_init(TorFlowBase* tfb, TorFlowEventCallbacks* eventHandlers, in_port_t controlPort, gint epolld, gint workerID);
void torflowbase_free(TorFlowBase* tfb);
void torflowbase_start(TorFlowBase* tfb);
void torflowbase_activate(TorFlowBase* tfb, gint sd, uint32_t events);
void torflowbase_requestInfo(TorFlowBase* tfb);
gint torflowbase_getControlSD(TorFlowBase* tfb);
const gchar* torflowbase_getCurrentPath(TorFlowBase* tfb);
gboolean torflowbase_buildNewMeasurementCircuit(TorFlowBase* tfb, gchar* path);
void torflowbase_closeCircuit(TorFlowBase* tfb, gint circid);
void torflowbase_attachStreamToCircuit(TorFlowBase* tfb, gint streamid, gint circid);
void torflowbase_stopReading(TorFlowBase* tfb, gchar* addressString);
void torflowbase_enableCircuits(TorFlowBase* tfb);
void torflowbase_closeStreams(TorFlowBase* tfb, gchar* addressString);
void torflowbase_ignorePackageWindows(TorFlowBase* tfb, gint circid);

typedef struct _TorFlowAggregator TorFlowAggregator;

gint torflowaggregator_loadFromPresets(TorFlowAggregator* tfa, GSList* relays);
void torflowaggregator_reportMeasurements(TorFlowAggregator* tfa, TorFlowSlice* slice, gboolean printFile);
void torflowaggregator_free(TorFlowAggregator* tfa);
TorFlowAggregator* torflowaggregator_new(gchar* filename, gdouble nodeCap);

typedef struct _TorFlow TorFlow;
typedef struct _TorFlowInternal TorFlowInternal;
struct _TorFlow {
	TorFlowBase _base;
	TorFlowInternal* internal;
};

void torflow_init(TorFlow* tf, TorFlowEventCallbacks* eventHandlers,
		in_port_t controlPort, in_port_t socksPort, gint workerID);
void torflow_start(TorFlow* tf);
gint torflow_newDownload(TorFlow* tf, TorFlowFileServer* fileserver);
void torflow_freeDownload(TorFlow* tf, gint socksd);
void torflow_startDownload(TorFlow* tf, gint socksd, gchar* filePath);
gint torflow_getEpollDescriptor(TorFlow* tf);
in_port_t torflow_getHostBoundSocksPort(TorFlow* tf);
void torflow_ready(TorFlow* tf, TorFlowManager* tfm);

typedef struct _TorFlowProber TorFlowProber;
typedef struct _TorFlowProberInternal TorFlowProberInternal;
struct _TorFlowProber {
	TorFlow _tf;
	TorFlowProberInternal* internal;
};

TorFlowManager* torflowmanager_new(gint argc, gchar* argv[]);
void torflowmanager_ready(TorFlowManager* tfm);
gint torflowmanager_getEpollDescriptor(TorFlowManager* tfm);
void torflowmanager_free(TorFlowManager* tfm);
TorFlowSlice* torflowmanager_getNextSlice(TorFlowManager* tfm);
void torflowmanager_notifySliceMeasured(TorFlowManager* tfm, TorFlowSlice* slice);
void torflowmanager_registerTimer(TorFlowManager* tfm, TorFlowTimer* timer);

TorFlowProber* torflowprober_new(TorFlowManager* tfm, gint workerID, gint numWorkers,
		gint pausetime, in_port_t controlPort, in_port_t socksPort, TorFlowFileServer* fileserver);
void torflowprober_start(TorFlowProber* tfp);
void torflowprober_continue(TorFlowProber* tfp);


void torflowutil_epoll(gint ed, gint fd, gint operation, guint32 events);
gsize torflowutil_computeTime(struct timespec* start, struct timespec* end);
in_addr_t torflowutil_lookupAddress(const gchar* name);
gchar* torflowutil_ipToNewString(in_addr_t netIP);
void torflowutil_resetRelay(TorFlowRelay* relay, gpointer nothing);
gint torflowutil_meanBandwidth(TorFlowRelay* relay);
gint torflowutil_filteredBandwidth(TorFlowRelay* relay, gint meanBandwidth);
GString* torflowutil_base64ToBase16(GString* base64);
gint torflowutil_compareRelays(gconstpointer a, gconstpointer b);
gint torflowutil_compareRelaysData(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean torflowutil_relayEqualFunc(gconstpointer a, gconstpointer b);

TorFlowFileServer* torflowfileserver_new(const gchar* name, in_port_t networkPort);
void torflowfileserver_ref(TorFlowFileServer* tffs);
void torflowfileserver_unref(TorFlowFileServer* tffs);
in_addr_t torflowfileserver_getNetIP(TorFlowFileServer* tffs);
in_port_t torflowfileserver_getNetPort(TorFlowFileServer* tffs);
const gchar* torflowfileserver_getName(TorFlowFileServer* tffs);
const gchar*  torflowfileserver_getHostIPStr(TorFlowFileServer* tffs);


#endif /* TORFLOW_H_ */
