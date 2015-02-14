/*
 * See LICENSE for licensing information
 */

#ifndef TORFLOW_H_
#define TORFLOW_H_

#define MEASUREMENTS_PER_SLICE 5
#define WORKER_RETRY_TIME 300

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
#include <shd-library.h>

typedef struct _TorFlowFileServer TorFlowFileServer;

typedef struct _TorFlowRelay {
	GString * nickname;
	GString * identity;
	gint descriptorBandwidth;
	gint advertisedBandwidth;
	gint newBandwidth;
	gboolean exit;
	gboolean running;
	gboolean fast;
	gint measureCount;
	GSList* t_rtt;
	GSList* t_payload;
	GSList* t_total;
	GSList* bytesPushed;
} TorFlowRelay;

typedef void (*ReadyFunc)(gpointer data);
typedef void (*BootstrapCompleteFunc)(gpointer data);
typedef void (*DescriptorsReceivedFunc)(gpointer data1, gpointer data2);
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

typedef struct _TorFlowBase TorFlowBase;
typedef struct _TorFlowBaseInternal TorFlowBaseInternal;
struct _TorFlowBase {
	TorFlowBaseInternal* internal;
	ShadowLogFunc slogf;
	ShadowCreateCallbackFunc scbf;
	gchar* id;
};

void torflowbase_init(TorFlowBase* tfb, TorFlowEventCallbacks* eventHandlers,
		ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf, in_port_t controlPort, gint epolld, gint workerID);
void torflowbase_free(TorFlowBase* tfb);
void torflowbase_start(TorFlowBase* tfb);
void torflowbase_activate(TorFlowBase* tfb, gint sd, uint32_t events);
void torflowbase_requestInfo(TorFlowBase* tfb);
void torflowbase_reportMeasurements(TorFlowBase* tfb, gint sliceSize, gint currSlice);
gint torflowbase_getControlSD(TorFlowBase* tfb);
const gchar* torflowbase_getCurrentPath(TorFlowBase* tfb);
gboolean torflowbase_buildNewMeasurementCircuit(TorFlowBase* tfb, gint sliceSize, gint currSlice);
void torflowbase_closeCircuit(TorFlowBase* tfb, gint circid);
void torflowbase_attachStreamToCircuit(TorFlowBase* tfb, gint streamid, gint circid);
void torflowbase_stopReading(TorFlowBase* tfb, gchar* addressString);
void torflowbase_recordMeasurement(TorFlowBase* tfb, gint contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime);
void torflowbase_recordTimeout(TorFlowBase* tfb);
void torflowbase_updateRelays(TorFlowBase* tfb, GSList* relays);
void torflowbase_enableCircuits(TorFlowBase* tfb);
void torflowbase_closeStreams(TorFlowBase* tfb, gchar* addressString);
void torflowbase_ignorePackageWindows(TorFlowBase* tfb, gint circid);

typedef struct _TorFlowAggregator TorFlowAggregator;

gint torflowaggregator_loadFromPresets(TorFlowAggregator* tfa, GSList* relays);
void torflowaggregator_reportMeasurements(TorFlowAggregator* tfa, GSList* measuredRelays, gint sliceSize, gint currSlice);
void torflowaggregator_free(TorFlowAggregator* tfa);
TorFlowAggregator* torflowaggregator_new(ShadowLogFunc slogf, gchar* filename, gint sliceSize, gdouble nodeCap);

typedef struct _TorFlow TorFlow;
typedef struct _TorFlowInternal TorFlowInternal;
struct _TorFlow {
	TorFlowBase _base;
	TorFlowAggregator* tfa;
	TorFlowInternal* internal;
};

void torflow_init(TorFlow* tf, TorFlowEventCallbacks* eventHandlers,
		ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf,
		TorFlowAggregator* tfa, in_port_t controlPort, in_port_t socksPort, gint workerID);
gint torflow_newDownload(TorFlow* tf, TorFlowFileServer* fileserver);
void torflow_freeDownload(TorFlow* tf, gint socksd);
void torflow_startDownload(TorFlow* tf, gint socksd, gchar* filePath);
gint torflow_getEpollDescriptor(TorFlow* tf);
in_port_t torflow_getHostBoundSocksPort(TorFlow* tf);
void torflow_ready(TorFlow* tf);

typedef struct _TorFlowProber TorFlowProber;
typedef struct _TorFlowProberInternal TorFlowProberInternal;
struct _TorFlowProber {
	TorFlow _tf;
	TorFlowProberInternal* internal;
};

TorFlowProber* torflowprober_new(ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf,
		TorFlowAggregator* tfa, gint workerID, gint numWorkers,
		gint pausetime, gint sliceSize,
		in_port_t controlPort, in_port_t socksPort, TorFlowFileServer* fileserver);

typedef struct _TorFlowManager TorFlowManager;
TorFlowManager* torflowmanager_new(gint argc, gchar* argv[], ShadowLogFunc slogf, ShadowCreateCallbackFunc scbf);
void torflowmanager_ready(TorFlowManager* tfm);
void torflowmanager_free(TorFlowManager* tfm);

void torflowutil_epoll(gint ed, gint fd, gint operation, guint32 events, ShadowLogFunc slogf);
gsize torflowutil_computeTime(struct timespec* start, struct timespec* end);
in_addr_t torflowutil_lookupAddress(const gchar* name, ShadowLogFunc slogf);
gchar* torflowutil_ipToNewString(in_addr_t netIP);
void torflowutil_resetRelay(TorFlowRelay* relay, gpointer nothing);
gint torflowutil_meanBandwidth(TorFlowRelay* relay);
gint torflowutil_filteredBandwidth(TorFlowRelay* relay, gint meanBandwidth);
GString* torflowutil_base64ToBase16(GString* base64);
gint torflowutil_compareRelays(gconstpointer a, gconstpointer b);

TorFlowFileServer* torflowfileserver_new(const gchar* name, in_port_t networkPort);
void torflowfileserver_ref(TorFlowFileServer* tffs);
void torflowfileserver_unref(TorFlowFileServer* tffs);
in_addr_t torflowfileserver_getNetIP(TorFlowFileServer* tffs);
in_port_t torflowfileserver_getNetPort(TorFlowFileServer* tffs);
const gchar* torflowfileserver_getName(TorFlowFileServer* tffs);
const gchar*  torflowfileserver_getHostIPStr(TorFlowFileServer* tffs);

#endif /* TORFLOW_H_ */
