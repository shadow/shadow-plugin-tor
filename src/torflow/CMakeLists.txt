# torflow - a program that mimicks the TorFlow software that is run by Tor bandwidth authorities
project(shadow-plugin-torflow)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

# dependencies
find_package(GLIB REQUIRED)
find_package(M REQUIRED)

include_directories(AFTER ${GLIB_INCLUDES} ${M_INCLUDES})

## plug-ins need to disable fortification to ensure syscalls are intercepted
add_cflags("-fPIC -fno-inline -fno-strict-aliasing -std=gnu11 -U_FORTIFY_SOURCE")

## Download smaller files than normal Torflow (improves running time)
#add_definitions( -DSMALLFILES )

## torflow source files
set(sources
    torflow-main.c
    torflow-manager.c
    torflow-base.c
    torflow.c
    torflow-prober.c
    torflow-aggregator.c
    torflow-fileserver.c
    torflow-timer.c
    torflow-utility.c
)

## create and install a dynamic library that can plug into shadow
add_shadow_plugin(shadow-plugin-torflow ${sources})
target_link_libraries(shadow-plugin-torflow ${GLIB_LIBRARIES} ${M_LIBRARIES})
install(TARGETS shadow-plugin-torflow DESTINATION lib)
