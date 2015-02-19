/*ni
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowAggregator {
	ShadowLogFunc slogf;
	gboolean loadedInitial;
	GString* filepath;
	GHashTable* relayStats;
	gdouble nodeCap;
	gint version;
};

typedef struct _TorFlowRelayStats {
	GString * nickname;
	GString * identity;
	gint descriptorBandwidth;
	gint advertisedBandwidth;
	gint newBandwidth;
	gint meanBandwidth;
	gint filteredBandwidth;
} TorFlowRelayStats;

static void _torflowaggregator_torFlowRelayStatsFree(gpointer toFree) {
	TorFlowRelayStats* tfrs = (TorFlowRelayStats*)toFree;
	g_assert(tfrs);
	
	if(tfrs->nickname) {
		g_string_free(tfrs->nickname, TRUE);
	}
	if(tfrs->identity) {
		g_string_free(tfrs->identity, TRUE);
	}
	g_free(tfrs);
}

static void _torflowaggregator_updateAuthoritativeLink(TorFlowAggregator* tfa, const gchar* newPath){
    const gchar* configuredPath = tfa->filepath->str;

    /* sanity check for directory */
    if(g_file_test(configuredPath, G_FILE_TEST_IS_DIR)) {
        tfa->slogf(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__,
            "configured path '%s' for v3bw file must not be a directory", configuredPath);
        return;
    }

    /* first lets make sure the path we want to write is clear */
    gint result = 0;
    if(g_file_test(configuredPath, G_FILE_TEST_IS_SYMLINK)) {
        /* it is a symlink, so its safe to remove */
        result = g_unlink(configuredPath);
        if(result < 0) {
            tfa->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
                    "g_unlink() returned %i error %i: %s",
                    result, errno, g_strerror(errno));
        }
    } else if(g_file_test(configuredPath, G_FILE_TEST_IS_REGULAR)) {
        /* not a symlink, but a regular file. dont overwrite it */
        GString* backupFilepathBuffer = g_string_new(configuredPath);
        g_string_append_printf(backupFilepathBuffer, ".init");

        result = g_rename(configuredPath, backupFilepathBuffer->str);
        if(result < 0) {
            tfa->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
                    "g_rename() returned %i error %i: %s",
                    result, errno, g_strerror(errno));
        } else {
            /* the file better not exist anymore */
            g_assert(!g_file_test(configuredPath, G_FILE_TEST_IS_REGULAR));
        }

        g_string_free(backupFilepathBuffer, TRUE);
    } else {
        /* its not a symlink, and not a file. so we are creating the first one */
    }

    if(result < 0) {
        return;
    }

    /* the target path for the link is now clear, now we need to set up
     * the reference (where the link is going to point).
     * the linkref should point in the same directory as the link itself.
     */
    const gchar* linkRef = NULL;
    /* get the base filename without directory components */
    gchar* baseFilename = g_strrstr(newPath, "/");
    if(baseFilename) {
        /* chop off the directories */
        linkRef = baseFilename+1;
    } else {
        /* newPath had no directories */
        linkRef = newPath;
    }

    /* now make the configured path exist, pointing to the new file */
    result = symlink(linkRef, configuredPath);
    if(result < 0) {
        tfa->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
                "Unable to create symlink at %s pointing to %s; symlink() returned %i error %i: %s",
                configuredPath, linkRef, result, errno, g_strerror(errno));
    } else {
        /* that better not be a dangling link */
        g_assert(g_file_test(configuredPath, G_FILE_TEST_IS_SYMLINK) &&
                g_file_test(configuredPath, G_FILE_TEST_IS_REGULAR));

        tfa->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
                "new v3bw file '%s' now linked at '%s'", linkRef, configuredPath);
    }
}

static void _torflowaggregator_printToFile(TorFlowAggregator* tfa) {

	// loop through measured nodes and aggregate stats
	gint totalMeanBW = 0;
	gint totalFiltBW = 0;
	gint measuredNodes = 0;

	GHashTableIter iter;
	gpointer key;
	gpointer value;
	g_hash_table_iter_init(&iter, tfa->relayStats);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		TorFlowRelayStats* current = (TorFlowRelayStats*)value;
		totalMeanBW += current->meanBandwidth;
		totalFiltBW += current->filteredBandwidth;
	}

	// calculate averages
	gdouble avgMeanBW = (gdouble)totalMeanBW/g_hash_table_size(tfa->relayStats);
	gdouble avgFiltBW = (gdouble)totalFiltBW/g_hash_table_size(tfa->relayStats);
	gint totalBW = 0;

	//loop through nodes and calculate new bandwidths
	g_hash_table_iter_init(&iter, tfa->relayStats);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		TorFlowRelayStats* current = (TorFlowRelayStats*)value;
		//use the better of the mean and filtered ratios, because that's what torflow does
		current->newBandwidth = (gint)(current->advertisedBandwidth * fmax(current->meanBandwidth/avgMeanBW, current->filteredBandwidth/avgFiltBW));
		totalBW += current->newBandwidth;
	}

	//create new file to print to, and increment version
	GString* newFilename = g_string_new(tfa->filepath->str);
	g_string_append_printf(newFilename, ".%d", tfa->version++);

	struct timespec now_ts;
	clock_gettime(CLOCK_REALTIME, &now_ts);

	FILE * fp;
	fp = fopen(newFilename->str, "w");
	fprintf(fp, "%li\n", now_ts.tv_sec);

	//loop through nodes and cap bandwidths that are too large, then print to file
	/*
	 * format is, where first line value is unix timestamp:
	 * ```
	 * {}\n
	 * node_id=${}\tbw={}\tnick={}\n
	 * [...]
	 * node_id=${}\tbw={}\tnick={}\n
	 * ```
	 * notice there is a newline on the last line.
	 */
	g_hash_table_iter_init(&iter, tfa->relayStats);
	while(g_hash_table_iter_next(&iter, &key, &value)) {
		TorFlowRelayStats* current = (TorFlowRelayStats*)value;
		if (current->newBandwidth > (gint)(totalBW * tfa->nodeCap)){
			tfa->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
					"Capping bandwidth for extremely fast relay %s\n",
					current->nickname->str);
			current->newBandwidth = (gint)(totalBW * tfa->nodeCap);
		}

		fprintf(fp, "node_id=$%s\tbw=%i\tnick=%s\n",
				current->identity->str,
				current->newBandwidth,
				current->nickname->str);
	}

	fclose(fp);

	/* update symlink */
	_torflowaggregator_updateAuthoritativeLink(tfa, newFilename->str);
	g_string_free(newFilename, TRUE);
}

static void _torflowaggregator_readInitialAdvertisements(TorFlowAggregator* tfa) {
	g_assert(tfa);

	if(tfa->loadedInitial) {
		tfa->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__,
				"Already loaded initial advertisements");
		return;
	}

	//Open file for reading
	FILE * fp;
	fp = fopen(tfa->filepath->str, "r");
	if (fp == NULL) {
		tfa->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"Could not open v3bw file %s for reading", tfa->filepath);
		return;
	}
	gchar* line = NULL;
	gsize len = 0;
	gssize read;
	//Attempt to read first line, which must be timestamp and is therefore useless to us
	if (getline(&line, &len, fp) == -1) {
		tfa->slogf(SHADOW_LOG_LEVEL_CRITICAL, __FUNCTION__,
				"Error reading from v3bw file %s", tfa->filepath);
		return;
	}

	//read information for each relay
	while((read = getline(&line, &len, fp)) != -1) {
		gchar** rparts = g_strsplit(line, "\t", 0);
		gchar* rpart = NULL;

		TorFlowRelayStats* tfrs = g_new(TorFlowRelayStats, 1);
		tfrs->nickname = tfrs->identity = NULL;
		tfrs->descriptorBandwidth = tfrs->advertisedBandwidth = 0;

		for(gint i = 0; (rpart = rparts[i]) != NULL; i++) {

			gchar** iparts = g_strsplit(rpart,"=", 2);
			if(!(iparts[0] && iparts[1])) {
				tfa->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
						"Error parsing token %s from v3bw file %s", rpart, tfa->filepath);
				continue;
			} else if(g_strcmp0(iparts[0], "node_id") == 0) {
				tfrs->identity = g_string_new(iparts[1]+1); //excluding dollar sign
			} else if(g_strcmp0(iparts[0], "nick") == 0) {
				tfrs->nickname = g_string_new(iparts[1]);
			} else if(g_strcmp0(iparts[0], "bw") == 0) {
				tfrs->descriptorBandwidth = tfrs->advertisedBandwidth = tfrs->meanBandwidth = tfrs->filteredBandwidth = atoi(iparts[1]);
			} else if(g_strcmp0(iparts[0], "measured_at") == 0) {
				//ignore useless but recognized data
			} else {
				tfa->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
						"Unrecognized field %s in v3bw file", iparts[0]);
			}
		}
		
		if(!tfrs->identity) {
			tfa->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
					"No node_id found in line %s in v3bw file", line);
			_torflowaggregator_torFlowRelayStatsFree(tfrs);
		} else {
			g_hash_table_replace(tfa->relayStats, tfrs->identity->str, tfrs);
		}
	}
	free(line);
	fclose(fp);

	tfa->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "Read %u nodes from file",
	        g_hash_table_size(tfa->relayStats));

	tfa->loadedInitial = TRUE;
}

gint torflowaggregator_loadFromPresets(TorFlowAggregator* tfa, GSList* relays) {
	g_assert(tfa);

	gint changes = 0;
	//Preload advertisements from file; do this only once
	if(!tfa->loadedInitial) {
		_torflowaggregator_readInitialAdvertisements(tfa);
	}

	//Go through relays - update them with preset stats
	for(GSList* currentNode = relays; currentNode; currentNode = g_slist_next(currentNode)) {
		TorFlowRelay* current = currentNode->data;
		TorFlowRelayStats* stats = g_hash_table_lookup(tfa->relayStats, current->identity->str);
		if(!stats) {
			tfa->slogf(SHADOW_LOG_LEVEL_WARNING, __FUNCTION__,
					"Relay %s read in descriptor from torctl port, but not found in initialization file",
					current->identity->str);
			continue;
		}
		tfa->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__,
		        "for $%s, descriptorBandwidth was %i, advertisedBandwidth was %i",
				current->identity->str, current->descriptorBandwidth, current->advertisedBandwidth);
		if (current->descriptorBandwidth == 0) {
			current->descriptorBandwidth = stats->descriptorBandwidth;
			current->advertisedBandwidth = stats->advertisedBandwidth;
			changes++;
		}
	}

	//return the number of relays that changed as a result of this
	return changes;
}

/* This function reports all measurements taken in the current slice.
 * It prints the measurements to the log, and would also print them to disk if this were
 * real torflow. */
static void _torflowaggregator_printMeasurements(TorFlowAggregator* tfa, TorFlowSlice* slice) {

/*
    // Calculate the name of the file on disc
    gdouble startPct, stopPct;
    startPct = 100.0 * sliceSize * currSlice / (gdouble)(tfb->internal->numRelays);
    if (sliceSize * (currSlice + 1) >= tfb->internal->numRelays) {
        stopPct = 100.0;
    } else {
        stopPct = 100.0 * sliceSize * (currSlice + 1) / (gdouble)(tfb->internal->numRelays);
    }
    struct timespec now_ts;
    clock_gettime(CLOCK_REALTIME, &now_ts);
    struct tm * now = gmtime(&(now_ts.tv_sec));
    gchar* fileName = g_malloc(80);
    sprintf(fileName, "data/bws-%03.1f:%03.1f-done-%04i-%02i-%02i-%02i:%02i:%02i",
                startPct,
                stopPct,
                now->tm_year+1900,
                now->tm_mon,
                now->tm_mday,
                now->tm_hour,
                now->tm_min,
                now->tm_sec);
    FILE * fp;
    fp = fopen(fileName, "w");

    // print file header
    fprintf(fp, "slicenum=%i\n", currSlice);
    fprintf(fp, "%li\n", now_ts.tv_sec);
*/
    tfa->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "Slice %u complete. Measurements:", slice->sliceNumber);

    // loop through measurements and print data
    GSList* currentNode = slice->allRelays;
    while (currentNode) {
        TorFlowRelay* current = currentNode->data;
        if (current && current->measureCount > 0) {
            tfa->slogf(SHADOW_LOG_LEVEL_DEBUG, __FUNCTION__, "Current[0]: Bytes %i Total %i",
                    current->bytesPushed ? GPOINTER_TO_INT(current->bytesPushed->data) : 0,
                    current->t_total ? GPOINTER_TO_INT(current->t_total->data) : 0);
            gint meanBW = torflowutil_meanBandwidth(current);
/*
            fprintf(fp, "node_id=%s nick=%s strm_bw=%i filt_bw=%i desc_bw=%i ns_bw=%i\n",
                        current->identity->str,
                        current->nickname->str,
                        meanBW,
                        torflowutil_filteredBandwidth(current, meanBW),
                        current->advertisedBandwidth,
                        current->descriptorBandwidth);
*/
            tfa->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__,
                        "node_id=%s nick=%s strm_bw=%i filt_bw=%i desc_bw=%i ns_bw=%i\n",
                        current->identity ? current->identity->str : "unknown",
                        current->nickname ? current->nickname->str : "unknown",
                        meanBW,
                        torflowutil_filteredBandwidth(current, meanBW),
                        current->advertisedBandwidth,
                        current->descriptorBandwidth);
        } else {
            tfa->slogf(SHADOW_LOG_LEVEL_MESSAGE, __FUNCTION__, "%s: unmeasured", current && current->nickname ? current->nickname->str : "unknown");
        }
        currentNode = g_slist_next(currentNode);
    }
/*
    fclose(fp);
    g_free(fileName);
*/
}

void torflowaggregator_reportMeasurements(TorFlowAggregator* tfa, TorFlowSlice* slice, gboolean printFile) {
	g_assert(tfa);
	
	//add all relays that the worker measured to our stats list
	GSList* currentNode = slice->allRelays;
	while (currentNode) {
		TorFlowRelay* current = currentNode->data;
		if (current->measureCount >= MEASUREMENTS_PER_SLICE) {
			TorFlowRelayStats* tfrs = g_new0(TorFlowRelayStats, 1);
			tfrs->nickname = g_string_new(current->nickname->str);
			tfrs->identity = g_string_new(current->identity->str);
			tfrs->descriptorBandwidth = current->descriptorBandwidth;
			tfrs->advertisedBandwidth = current->advertisedBandwidth;
			tfrs->meanBandwidth = torflowutil_meanBandwidth(current);
			tfrs->filteredBandwidth = torflowutil_filteredBandwidth(current, tfrs->meanBandwidth);
			g_hash_table_replace(tfa->relayStats, tfrs->identity->str, tfrs);

			tfa->slogf(SHADOW_LOG_LEVEL_INFO, __FUNCTION__,
                "stored new measurements for %s (%s) desc=%i adv=%i mean=%i filtered=%i",
                tfrs->nickname->str, tfrs->identity->str, tfrs->descriptorBandwidth,
                tfrs->advertisedBandwidth, tfrs->meanBandwidth, tfrs->filteredBandwidth);
		}
		currentNode = g_slist_next(currentNode);
	}

	if(printFile) {
        //print results to file
        _torflowaggregator_printToFile(tfa);
	}
}

void torflowaggregator_free(TorFlowAggregator* tfa) {
	g_assert(tfa);

	g_hash_table_destroy(tfa->relayStats);

	g_string_free(tfa->filepath, TRUE);
	g_free(tfa);
}

TorFlowAggregator* torflowaggregator_new(ShadowLogFunc slogf,
		gchar* filename, gdouble nodeCap) {

	TorFlowAggregator* tfa = g_new0(TorFlowAggregator, 1);
	tfa = g_new0(TorFlowAggregator, 1);
	tfa->slogf = slogf;
	tfa->filepath = g_string_new(filename);
	tfa->nodeCap = nodeCap;
	tfa->version = 0;
	tfa->relayStats = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, _torflowaggregator_torFlowRelayStatsFree);
	tfa->loadedInitial = FALSE;

	return tfa;
}
