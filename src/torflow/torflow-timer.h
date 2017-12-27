/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_TIMER_H_
#define SRC_TORFLOW_TORFLOW_TIMER_H_

#include <glib.h>

typedef struct _TorFlowTimer TorFlowTimer;

TorFlowTimer* torflowtimer_new(GFunc func, gpointer arg1, gpointer arg2);
void torflowtimer_arm(TorFlowTimer* timer, guint timeoutSeconds);
gboolean torflowtimer_check(TorFlowTimer* timer);
gint torflowtimer_getFD(TorFlowTimer* timer);
void torflowtimer_free(TorFlowTimer* timer);

#endif /* SRC_TORFLOW_TORFLOW_TIMER_H_ */
