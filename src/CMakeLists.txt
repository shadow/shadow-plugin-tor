add_cflags(-Wno-unknown-attributes)
add_cflags(-Wno-unused-command-line-argument)
add_cflags(-Wno-unknown-warning-option)

add_subdirectory(tor)
add_subdirectory(torctl)
#add_subdirectory(torflow)
