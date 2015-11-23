/*
 * See LICENSE for licensing information
 */

#include "torflow.h"

void torflowutil_epoll(gint ed, gint fd, gint operation, guint32 events, ShadowLogFunc slogf) {
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));
	ev.events = events;
	ev.data.fd = fd;

	/* start watching the client socket */
	gint res = epoll_ctl(ed, operation, fd, &ev);
	if(res == -1 && slogf) {
		slogf(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__,
				"unable to start client: error in epoll_ctl");
	}
}

gsize torflowutil_computeTime(struct timespec* start, struct timespec* end) {
	g_assert(start && end);
	struct timespec result;
	result.tv_sec = end->tv_sec - start->tv_sec;
	result.tv_nsec = end->tv_nsec - start->tv_nsec;
	while(result.tv_nsec < 0) {
		result.tv_sec--;
		result.tv_nsec += 1000000000;
	}
	gsize millis = (gsize)((result.tv_sec * 1000) + (result.tv_nsec / 1000000));
	return millis;
}

gchar* torflowutil_ipToNewString(in_addr_t netIP) {
	gchar* ipStringBuffer = g_malloc0(INET6_ADDRSTRLEN+1);
	const gchar* ipString = inet_ntop(AF_INET, &netIP, ipStringBuffer, INET6_ADDRSTRLEN);
	GString* result = ipString ? g_string_new(ipString) : g_string_new("NULL");
	g_free(ipStringBuffer);
	return g_string_free(result, FALSE);
}

in_addr_t torflowutil_lookupAddress(const gchar* name, ShadowLogFunc slogf) {
	struct addrinfo* info = NULL;
	gint ret = getaddrinfo((gchar*) name, NULL, NULL, &info);
	if(ret != 0 || !info) {
	    if(slogf) {
            slogf(SHADOW_LOG_LEVEL_ERROR, __FUNCTION__,
                    "hostname lookup failed '%s'", name);
	    }
		return 0;
	}
	in_addr_t netIP = ((struct sockaddr_in*)(info->ai_addr))->sin_addr.s_addr;
	freeaddrinfo(info);
	return netIP;
}

void torflowutil_resetRelay(TorFlowRelay* relay, gpointer nothing) {
	relay->measureCount = 0;
	relay->bytesPushed = relay->t_rtt = relay->t_payload = relay->t_total = NULL;
}

gint torflowutil_meanBandwidth(TorFlowRelay* relay) {
	gdouble bandwidth = 0.0;
	GSList * currentTiming = relay->t_total;
	GSList * currentBytes = relay->bytesPushed;
	gint i = 0;
	while (currentTiming && currentBytes) {
		gdouble currentBandwidth = (1000.0 * GPOINTER_TO_INT(currentBytes->data)) / GPOINTER_TO_INT(currentTiming->data);
		bandwidth += currentBandwidth;
		i++;
		currentTiming = g_slist_next(currentTiming);
		currentBytes = g_slist_next(currentBytes);
	}
	return (int)(bandwidth / i);
}

gint torflowutil_filteredBandwidth(TorFlowRelay* relay, gint meanBandwidth) {
	gdouble bandwidth = 0.0;
	GSList * currentTiming = relay->t_total;
	GSList * currentBytes = relay->bytesPushed;
	gint i = 0;
	while (currentTiming && currentBytes) {
		gdouble currentBandwidth = (1000.0 * GPOINTER_TO_INT(currentBytes->data)) / GPOINTER_TO_INT(currentTiming->data);
		if (currentBandwidth >= meanBandwidth) {
			bandwidth += currentBandwidth;
			i++;
		}
		currentTiming = g_slist_next(currentTiming);
		currentBytes = g_slist_next(currentBytes);
	}
	return (int)(bandwidth / i);
}

/* Takes in base 64 GString and returns new base 16 GString. 
 * Output string should be freed with g_string_free. */
GString* torflowutil_base64ToBase16(GString* base64) {
	gsize* bin_len = g_malloc(sizeof(gsize));
	guchar* base2 = g_base64_decode(base64->str, bin_len);
	GString* base16 = g_string_sized_new(2 * *bin_len + 1);
	for (gint i = 0; i < *bin_len; i++) {
		g_string_append_printf(base16, "%02x", base2[i]);
	}
	base16 = g_string_ascii_up(base16);
	g_free(base2);
	g_free(bin_len);
	return base16;
}


// Compare function to sort in descending order by bandwidth.
gint torflowutil_compareRelays(gconstpointer a, gconstpointer b){
	TorFlowRelay * aR = (TorFlowRelay *)a;
	TorFlowRelay * bR = (TorFlowRelay *)b;
	return bR->descriptorBandwidth - aR->descriptorBandwidth;
}

gint torflowutil_compareRelaysData(gconstpointer a, gconstpointer b, gpointer user_data){
    return torflowutil_compareRelays(a, b);
}

gboolean torflowutil_relayEqualFunc(gconstpointer a, gconstpointer b) {
    TorFlowRelay * aR = (TorFlowRelay *)a;
    TorFlowRelay * bR = (TorFlowRelay *)b;
    return g_str_equal(aR->identity->str, bR->identity->str);
}
