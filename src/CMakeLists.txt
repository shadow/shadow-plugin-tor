## plug-ins need to disable fortification to ensure syscalls are intercepted
#add_definitions(-D_GNU_SOURCE)
add_definitions(-U_FORTIFY_SOURCE)

add_cflags(-fno-inline)
add_cflags(-fno-strict-aliasing)

add_cflags(-Wno-unknown-attributes)
add_cflags(-Wno-unused-command-line-argument)
add_cflags(-Wno-unknown-warning-option)

message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

add_subdirectory(tor)
add_subdirectory(torctl)
add_subdirectory(torflow)
