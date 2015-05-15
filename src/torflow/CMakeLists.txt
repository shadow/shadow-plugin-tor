find_package(GLIB REQUIRED)

include_directories(AFTER ${GLIB_INCLUDES})

## Download smaller files than normal Torflow (improves running time)
#add_definitions( -DSMALLFILES )

## torflow plug-in
set(sources
    torflow-plugin.c
    torflow-manager.c
    torflow-base.c
    torflow.c
    torflow-prober.c
    torflow-aggregator.c
    torflow-fileserver.c
    torflow-utility.c
)

## create and install a shared library that can plug into shadow
add_bitcode(shadow-plugin-torflow-bitcode ${sources})
add_plugin(shadow-plugin-torflow shadow-plugin-torflow-bitcode)
target_link_libraries(shadow-plugin-torflow ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-torflow DESTINATION plugins)
