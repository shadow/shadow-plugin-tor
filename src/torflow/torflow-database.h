/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_DATABASE_H_
#define SRC_TORFLOW_TORFLOW_DATABASE_H_

#include <glib.h>

typedef struct _TorFlowDatabase TorFlowDatabase;

TorFlowDatabase* torflowdatabase_new(TorFlowConfig* config);
void torflowdatabase_free(TorFlowDatabase* database);

guint torflowdatabase_storeNewDescriptors(TorFlowDatabase* database, GQueue* descriptorLines);

GQueue* torflowdatabase_getMeasureableRelays(TorFlowDatabase* database);
void torflowdatabase_storeMeasurementResult(TorFlowDatabase* database,
        gchar* entryIdentity, gchar* exitIdentity, gboolean isSuccess,
        gsize contentLength, gsize roundTripTime, gsize payloadTime, gsize totalTime);

void torflowdatabase_writeBandwidthFile(TorFlowDatabase* database);

#endif /* SRC_TORFLOW_TORFLOW_DATABASE_H_ */
