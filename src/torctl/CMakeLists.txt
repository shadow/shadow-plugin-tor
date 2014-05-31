find_package(GLIB REQUIRED)

include_directories(AFTER ${GLIB_INCLUDES})

## create and install a shared library that can plug into shadow
add_bitcode(shadow-plugin-torctl-bitcode torctl-plugin.c torctl.c)
add_plugin(shadow-plugin-torctl shadow-plugin-torctl-bitcode)
target_link_libraries(shadow-plugin-torctl ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-torctl DESTINATION plugins)

## create and install an executable that can run outside of shadow
add_executable(torctl torctl-main.c torctl.c)
target_link_libraries(torctl ${GLIB_LIBRARIES})
install(TARGETS torctl DESTINATION bin)
