/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#include "torflow.h"

struct _TorFlowTimer {
    GFunc notifyTimerExpired;
    gpointer arg1;
    gpointer arg2;
    gint timerFD;
};

TorFlowTimer* torflowtimer_new(GFunc func, gpointer arg1, gpointer arg2) {
    /* store the data in the timer table */
    TorFlowTimer* timer = g_new0(TorFlowTimer, 1);
    timer->notifyTimerExpired = func;
    timer->arg1 = arg1;
    timer->arg2 = arg2;

    /* create new timerfd */
    timer->timerFD = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);

    return timer;
}

void torflowtimer_arm(TorFlowTimer* timer, guint timeoutSeconds) {
    g_assert(timer && timer->timerFD > 0);

    /* create the timer info */
    struct itimerspec arm;

    guint64 seconds = (guint64) timeoutSeconds;
    guint64 nanoseconds = (guint64) 0;

    /* a timer with 0 delay will cause timerfd to disarm, so we use a 1 nano
     * delay instead, in order to execute the event as close to now as possible */
    if(seconds == 0 && nanoseconds == 0) {
        nanoseconds = 1;
    }

    /* set the initial expiration */
    arm.it_value.tv_sec = seconds;
    arm.it_value.tv_nsec = nanoseconds;

    /* timer never repeats */
    arm.it_interval.tv_sec = 0;
    arm.it_interval.tv_nsec = 0;

    /* arm the timer, flags=0 -> relative time, NULL -> ignore previous setting */
    gint result = timerfd_settime(timer->timerFD, 0, &arm, NULL);
}

static gboolean _torflowtimer_didExpire(TorFlowTimer* timer) {
    g_assert(timer);

    /* clear the event from the descriptor */
    guint64 numExpirations = 0;
    ssize_t result = read(timer->timerFD, &numExpirations, sizeof(guint64));

    /* return TRUE if read succeeded and the timer expired at least once */
    if(result > 0 && numExpirations > 0) {
        return TRUE;
    } else {
        return FALSE;
    }
}

static void _torflowtimer_callNotify(TorFlowTimer* timer) {
    g_assert(timer);

    /* execute the callback function */
    if(timer->notifyTimerExpired != NULL) {
        timer->notifyTimerExpired(timer->arg1, timer->arg2);
    }
}

gboolean torflowtimer_check(TorFlowTimer* timer) {
    g_assert(timer);

    /* if the timer expired, execute the callback function; otherwise do nothing */
    if(_torflowtimer_didExpire(timer)) {
        _torflowtimer_callNotify(timer);
        return TRUE;
    } else {
        return FALSE;
    }
}

gint torflowtimer_getFD(TorFlowTimer* timer) {
    g_assert(timer);

    return timer->timerFD;
}

void torflowtimer_free(TorFlowTimer* timer) {
    g_assert(timer);

    close(timer->timerFD);
    g_free(timer);
}
