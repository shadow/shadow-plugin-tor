## check for dependencies, using built-in cmake files to help us find the libs
## these are in /usr/share/cmake/Modules/Findxxx.cmake
find_package(ZLIB REQUIRED)

## other libs dont have a Findxxx.cmake, so we check on our own (./cmake)
find_package(RT REQUIRED)
find_package(DL REQUIRED)
find_package(M REQUIRED)
find_package(OPENSSL REQUIRED)
find_package(EVENT2 REQUIRED)
find_package(GLIB REQUIRED)

#find_package(TOR REQUIRED)
get_filename_component(TOR_ROOT_DIR "${CMAKE_BINARY_DIR}/../tor" ABSOLUTE)
#set(TOR_ROOT_DIR ${CMAKE_BINARY_DIR}/tor)
#set(TOR_ROOT ${TORPATH})
include_directories(${TOR_ROOT_DIR})
include_directories(${TOR_ROOT_DIR}/src)
include_directories(${TOR_ROOT_DIR}/src/common)
include_directories(${TOR_ROOT_DIR}/src/or)
include_directories(${TOR_ROOT_DIR}/src/ext)
include_directories(${TOR_ROOT_DIR}/src/ext/trunnel)
include_directories(${TOR_ROOT_DIR}/src/trunnel)

include_directories(AFTER ${RT_INCLUDES} ${DL_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${EVENT2_INCLUDES} ${OPENSSL_INCLUDES} ${TOR_INCLUDES})

## import shadow libs
#INCLUDE(${CMAKE_INSTALL_PREFIX}/share/shadow/shadow-externals.cmake)

set(TOR_VERSION "${TOR_VERSION_A}.${TOR_VERSION_B}.${TOR_VERSION_C}.${TOR_VERSION_D}")
MESSAGE(STATUS "Found Tor path = ${TOR_ROOT_DIR}")
MESSAGE(STATUS "Found Tor version = ${TOR_VERSION}")

macro(check_tor_version retval major minor micro patch)
    if((${TOR_VERSION_A} EQUAL ${major} AND ${TOR_VERSION_B} EQUAL ${minor} AND ${TOR_VERSION_C} EQUAL ${micro} AND ${TOR_VERSION_D} GREATER ${patch}) OR
    (${TOR_VERSION_A} EQUAL ${major} AND ${TOR_VERSION_B} EQUAL ${minor} AND ${TOR_VERSION_C} GREATER ${micro}) OR
    (${TOR_VERSION_A} EQUAL ${major} AND ${TOR_VERSION_B} GREATER ${minor}) OR
    (${TOR_VERSION_A} GREATER ${major}))
        set(${retval} true)
    else()
        set(${retval} false)
    endif()
endmacro(check_tor_version)

## check env to see if we should allow interposition ion EVP_Cipher
if(SHADOW_ENABLE_EVPCIPHER STREQUAL ON)
    add_definitions(-DSHADOW_ENABLE_EVPCIPHER)
    message(STATUS "Added definition SHADOW_ENABLE_EVPCIPHER")
endif(SHADOW_ENABLE_EVPCIPHER STREQUAL ON)

## Version-specific defines to handle changes to Tor's functionality
# version >= 0.2.2.17, commit a2bb0bfdd5731ef3cebf6e14fa7bebb98d5f2d8f
check_tor_version(version_check_passed 0 2 2 17)
if(version_check_passed)
    add_definitions(-DSCALLION_NEWIDKEYNAME)
    message(STATUS "Added definition SCALLION_NEWIDKEYNAME")
endif()
# version >= 0.2.3.5
check_tor_version(version_check_passed 0 2 3 5)
if(version_check_passed)
    add_definitions(-DSCALLION_DOREFILLCALLBACKS)
	message(STATUS "Added definition SCALLION_DOREFILLCALLBACKS")
endif()
# version >= 0.2.3.10, commit 26e789fbfd4e5a7e2789c1577e8eb967de3eba86
check_tor_version(version_check_passed 0 2 3 10)
if(version_check_passed)
    add_definitions(-DSCALLION_NEWCRYPTODEFS)
    message(STATUS "Added definition SCALLION_NEWCRYPTODEFS")
endif()
# version >= 0.2.4.8
check_tor_version(version_check_passed 0 2 4 8)
if(version_check_passed)
    add_definitions(-DSCALLION_USEV2CPUWORKER)
	message(STATUS "Added definition SCALLION_USEV2CPUWORKER")
endif()
# version >= 0.2.4.10
check_tor_version(version_check_passed 0 2 4 10)
if(version_check_passed)
    add_definitions(-DSCALLION_USEV2CPUWORKERTIMING)
    message(STATUS "Added definition SCALLION_USEV2CPUWORKERTIMING")
endif()
# version >= 0.2.4.11
check_tor_version(version_check_passed 0 2 4 11)
if(version_check_passed)
    add_definitions(-DSCALLION_LOGVWITHSUFFIX)
    message(STATUS "Added definition SCALLION_LOGVWITHSUFFIX")
endif()
# version >= 0.2.5.2
check_tor_version(version_check_passed 0 2 5 2)
if(version_check_passed)
    add_definitions(-DSCALLION_NOV2DIR)
    message(STATUS "Added definition SCALLION_NOV2DIR")
endif()
# version >= 0.2.5.5, commit 39d4e67be8283b2a7141a7aa8342d30e27f47e6f
check_tor_version(version_check_passed 0 2 5 5)
if(version_check_passed)
    add_definitions(-DSCALLION_MEMPOOLOPT)
    message(STATUS "Added definition SCALLION_MEMPOOLOPT")
endif()
# version >= 0.2.6.1, commit 39d4e67be8283b2a7141a7aa8342d30e27f47e6f
check_tor_version(version_check_passed 0 2 6 1)
if(version_check_passed)
    add_definitions(-DSTARTUP_Q_PARAM)
    message(STATUS "Added definition STARTUP_Q_PARAM")
endif()

## get tor source files, but ignore some of them
aux_source_directory(${TOR_ROOT_DIR}/src/common torcommon_sources)
list(REMOVE_ITEM torcommon_sources 
    ${TOR_ROOT_DIR}/src/common/sha256.c
    ${TOR_ROOT_DIR}/src/common/strlcat.c
    ${TOR_ROOT_DIR}/src/common/strlcpy.c
    ${TOR_ROOT_DIR}/src/common/compat_winthreads.c
)

aux_source_directory(${TOR_ROOT_DIR}/src/ext torext_sources)
list(REMOVE_ITEM torext_sources 
    ${TOR_ROOT_DIR}/src/ext/OpenBSD_malloc_Linux.c
    ${TOR_ROOT_DIR}/src/ext/strlcat.c
    ${TOR_ROOT_DIR}/src/ext/strlcpy.c
    ${TOR_ROOT_DIR}/src/ext/tinytest.c
    ${TOR_ROOT_DIR}/src/ext/tinytest_demo.c
)

aux_source_directory(${TOR_ROOT_DIR}/src/ext/curve25519_donna torextcurve_sources)
if(${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    list(REMOVE_ITEM torextcurve_sources
	    ${TOR_ROOT_DIR}/src/ext/curve25519_donna/curve25519-donna.c
    )
else()
    list(REMOVE_ITEM torextcurve_sources
        ${TOR_ROOT_DIR}/src/ext/curve25519_donna/curve25519-donna-c64.c
    )
endif()

aux_source_directory(${TOR_ROOT_DIR}/src/ext/ed25519/ref10 torextedref_sources)

aux_source_directory(${TOR_ROOT_DIR}/src/ext/ed25519/donna torexteddon_sources)
list(REMOVE_ITEM torexteddon_sources 
     ${TOR_ROOT_DIR}/src/ext/ed25519/donna/ed25519.c
    ${TOR_ROOT_DIR}/src/ext/ed25519/donna/test.c
    ${TOR_ROOT_DIR}/src/ext/ed25519/donna/test-internals.c
)

aux_source_directory(${TOR_ROOT_DIR}/src/ext/trunnel torexttrunnel_sources)

# force use of c compiler instead of cxx for keccak
check_tor_version(version_check_passed 0 2 9 1)
if(version_check_passed AND (EXISTS ${TOR_ROOT_DIR}/src/ext/keccak-tiny/keccak-tiny-unrolled.c))
    # the following doesn't stick, i think because the src file is not in the current dir
    #set(torkeccak ${TOR_ROOT_DIR}/src/ext/keccak-tiny/keccak-tiny-unrolled.c)
    #set_source_file_properties(${torkeccak} PROPERTIES LANGUAGE C)

    # work around the above problem by creating a target and setting the language on that
    # the only reason this works is because there is no state in keccak 
    # and so it doesnt need to be built with llvm
    include_directories(${TOR_ROOT_DIR}/src/ext/keccak-tiny)
    add_library(torkeccak STATIC ${TOR_ROOT_DIR}/src/ext/keccak-tiny/keccak-tiny-unrolled.c)
    set_target_properties(torkeccak PROPERTIES LANGUAGE C)
endif()

aux_source_directory(${TOR_ROOT_DIR}/src/or toror_sources)
list(REMOVE_ITEM toror_sources 
    ${TOR_ROOT_DIR}/src/or/ntmain.c
    ${TOR_ROOT_DIR}/src/or/tor_main.c
)

aux_source_directory(${TOR_ROOT_DIR}/src/trunnel tortrunnel_sources)

## get tor header files -- we dont need this for now
#file(GLOB tor_headers ${TOR_ROOT_DIR}/*.h)
#file(GLOB toror_headers ${TOR_ROOT_DIR}/src/or/*.h)
#file(GLOB torcommon_headers ${TOR_ROOT_DIR}/src/common/*.h)
#list(APPEND tor_headers ${toror_headers} ${torcommon_headers})
#foreach(headerpath ${tor_headers})
#    get_filename_component(headername ${headerpath} NAME)
#    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${headername}")
#endforeach(headerpath)

## tor needs these defined
add_definitions(-DLOCALSTATEDIR="/usr/local/var" -DSHARE_DATADIR="/usr/local/var" -DBINDIR="/usr/local/bin")
add_definitions(-DED25519_CUSTOMRANDOM -DED25519_SUFFIX=_donna)
remove_definitions(-DNDEBUG)

## disable warnings from tor code, add other options
add_cflags("-w -fPIC -fno-inline -fno-strict-aliasing -U_FORTIFY_SOURCE")

## create and install a shared library that can plug into shadow
add_shadow_plugin(shadow-plugin-tor 
    shadowtor-main.c ${toror_sources} ${torcommon_sources} 
    ${torext_sources} ${torextcurve_sources} ${torextedref_sources} ${torexteddon_sources}
    ${tortrunnel_sources} ${torexttrunnel_sources})
add_dependencies(shadow-plugin-tor shadow-preload-tor)

if(TARGET torkeccak)
    add_dependencies(shadow-plugin-tor torkeccak)
endif()

target_link_libraries(shadow-plugin-tor shadow-preload-tor
    ${TOR_LIBRARIES} ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES} 
    ${ZLIB_LIBRARIES} ${M_LIBRARIES} ${RT_LIBRARIES} ${DL_LIBRARIES} -lpthread)

if(TARGET torkeccak)
    target_link_libraries(shadow-plugin-tor torkeccak)
endif()

foreach(lib ${CMAKE_TOR_LIBRARIES})
    target_link_libraries(shadow-plugin-tor ${lib})
endforeach(lib ${CMAKE_TOR_LIBRARIES})

install(TARGETS shadow-plugin-tor DESTINATION plugins)
set_target_properties(shadow-plugin-tor PROPERTIES INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib INSTALL_RPATH_USE_LINK_PATH TRUE)

## preload library for intercepting functions
add_library(shadow-preload-tor SHARED shadowtor-preload.c)
target_link_libraries(shadow-preload-tor ${GLIB_LIBRARIES} ${EVENT2_LIBRARIES} ${OPENSSL_LIBRARIES} ${DL_LIBRARIES})
install(TARGETS shadow-preload-tor DESTINATION lib)

## install run script
install(PROGRAMS shadow-tor DESTINATION bin)

install(FILES ${TOR_ROOT_DIR}/src/config/geoip DESTINATION share/)
#install(DIRECTORY resource/ DESTINATION share PATTERN "config/*")
