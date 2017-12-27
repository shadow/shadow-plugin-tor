/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */


#ifndef SRC_TORFLOW_TORFLOW_EVENT_MANAGER_H_
#define SRC_TORFLOW_TORFLOW_EVENT_MANAGER_H_

#include <glib.h>

typedef enum _TorFlowEventFlag TorFlowEventFlag;
enum _TorFlowEventFlag {
    TORFLOW_EV_NONE = 0,
    TORFLOW_EV_READ = 1 << 0,
    TORFLOW_EV_WRITE = 1 << 1,
};

/* function signature for the callback function that will get called by the
 * event manager when I/O occurs on registered descriptors. */
typedef void (*TorFlowOnEventFunc)(gpointer onEventArg, TorFlowEventFlag eventType);

/* Opaque internal struct for the manager object */
typedef struct _TorFlowEventManager TorFlowEventManager;

/* returns a new instance of an event manager that will watch file descriptors
 * for read/write events and notify components when the specified I/O occurs. */
TorFlowEventManager* torfloweventmanager_new();

/* deallocates all memory associated with an event manager previously created
 * with the torfloweventmanager_new function. */
void torfloweventmanager_free(TorFlowEventManager* manager);

/* monitor a new file descriptor for I/O events of the given type, and register
 * a callback function and arguments to execute when I/O occurs.
 * returns TRUE if the registration was successful, false otherwise. */
gboolean torfloweventmanager_register(TorFlowEventManager* manager,
        gint descriptor, TorFlowEventFlag eventType,
        TorFlowOnEventFunc onEvent, gpointer onEventArg);

/* stops monitoring I/O for the given file descriptor and deregisters previously
 * registered callback functions.
 * returns TRUE if the descriptor was previously registered, FALSE otherwise. */
gboolean torfloweventmanager_deregister(TorFlowEventManager* manager, gint descriptor);

/* instructs the event manager to start waiting for events from all registered descriptors.
 * when events occur, the registered callback functions will be executed. */
gboolean torfloweventmanager_runMainLoop(TorFlowEventManager* manager);

/* instructs the event manager to break out of the main loop asap.
 * this will probably stop torflow. */
void torfloweventmanager_stopMainLoop(TorFlowEventManager* manager);

#endif /* SRC_TORFLOW_TORFLOW_EVENT_MANAGER_H_ */
