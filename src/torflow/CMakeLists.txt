# torflow - a program that mimicks the TorFlow software that is run by Tor bandwidth authorities
project(shadow-plugin-torflow)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

# dependencies
find_package(GLIB REQUIRED)
find_package(M REQUIRED)

include_directories(AFTER ${GLIB_INCLUDES} ${M_INCLUDES})

## plug-ins need to disable fortification to ensure syscalls are intercepted
#already done in parent CMake file
#add_cflags("-fPIC -fno-inline -fno-strict-aliasing -std=gnu11 -U_FORTIFY_SOURCE")
add_cflags(-std=gnu11)

## Download smaller files than normal Torflow (improves running time)
#add_definitions( -DSMALLFILES )

## torflow source files
set(sources
    torflow.c
    torflow-authority.c
    torflow-config.c
    torflow-database.c
    torflow-event-manager.c
    torflow-file-client.c
    torflow-file-listener.c
    torflow-file-server.c
    torflow-peer.c
    torflow-probe.c
    torflow-relay.c
    torflow-slice.c
    torflow-timer.c
    torflow-torctl-client.c
)

## create and install a dynamic library that can plug into shadow
add_shadow_plugin(shadow-plugin-torflow ${sources})
target_link_libraries(shadow-plugin-torflow ${GLIB_LIBRARIES} ${M_LIBRARIES})
install(TARGETS shadow-plugin-torflow DESTINATION lib)
