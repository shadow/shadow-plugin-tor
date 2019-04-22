# torctl - a minimal program to log tor controller messages
project(shadow-plugin-torctl)
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

# dependencies
find_package(GLIB REQUIRED)
include_directories(AFTER ${GLIB_INCLUDES})

## plug-ins need to disable fortification to ensure syscalls are intercepted
# already done in parent CMake file
#add_cflags("-fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

## create and install a dynamic library that can plug into shadow
add_shadow_plugin(shadow-plugin-torctl torctl-main.c torctl.c)
target_link_libraries(shadow-plugin-torctl ${GLIB_LIBRARIES})
install(TARGETS shadow-plugin-torctl DESTINATION lib)
