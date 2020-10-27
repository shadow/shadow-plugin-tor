set(TOR_VERSION "${TOR_VERSION_A}.${TOR_VERSION_B}.${TOR_VERSION_C}.${TOR_VERSION_D}")
MESSAGE(STATUS "Found Tor path = ${TOR_ROOT_DIR}")
MESSAGE(STATUS "Found Tor version = ${TOR_VERSION}")

macro(aux_src_subdirs curdir result)
    file(GLOB children RELATIVE ${curdir} ${curdir}/*)
    unset(srclist)
    foreach(child ${children})
        if(IS_DIRECTORY ${curdir}/${child})
            aux_source_directory(${curdir}/${child} srclist)
        endif()
    endforeach()
    list(APPEND ${result} ${srclist})
endmacro()

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

check_tor_version(version_check_passed 0 3 5 1)
if(version_check_passed)
	include(CMakeLists.txt.post035)
else()
	include(CMakeLists.txt.pre035)
endif()
