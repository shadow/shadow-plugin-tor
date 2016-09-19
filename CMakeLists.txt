project(shadow-plugin-tor)

## ensure cmake version
cmake_minimum_required(VERSION 2.8.8 FATAL_ERROR)

## ensure unix environment (CMAKE_SYSTEM_NAME == "Linux")
if((NOT UNIX) OR (NOT ("${CMAKE_SYSTEM_NAME}" STREQUAL "Linux")))
    message(FATAL_ERROR "Unix/Linux environment is required.")
endif()

## ensure out-of-source build
if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
    message(FATAL_ERROR "An out-of-source build is required. Please see the README.")
endif()

## make it easier for sub-projects to add CFLAGS
macro(add_cflags)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${ARGN}")
endmacro(add_cflags)

## now handle the options, set up our own flags
set(CMAKE_C_FLAGS_DEBUG "")
set(CMAKE_C_FLAGS_RELEASE "")
if("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    message(STATUS "Release enabled. Setting '-DNDEBUG' and '-O3'.")
    add_definitions(-DNDEBUG)
    add_cflags(-O3)
else()
    set(CMAKE_BUILD_TYPE "Debug")
    message(STATUS "Debug enabled. Setting '-DDEBUG' and '-g'.")
    add_definitions(-DDEBUG)
    add_cflags(-g)
endif()

## get general includes
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(TestBigEndian)

## general tests and configurations
TEST_BIG_ENDIAN(IS_BIG_ENDIAN)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

## we need to know where shadow is installed
if("${SHADOW_ROOT}" STREQUAL "")
    set(SHADOW_ROOT "$ENV{HOME}/.shadow")
endif()
if(EXISTS "${SHADOW_ROOT}")
    message(STATUS "SHADOW_ROOT = ${SHADOW_ROOT}")
else()
    message(FATAL_ERROR "SHADOW_ROOT path does not exist: '${SHADOW_ROOT}'")
endif()
include_directories(${SHADOW_ROOT}/include)

## by default, install to SHADOW_ROOT
if("${CMAKE_INSTALL_PREFIX}" STREQUAL "")
    set(CMAKE_INSTALL_PREFIX "${SHADOW_ROOT}")
endif()
## by default, search SHADOW_ROOT first for custom libs
if("${CMAKE_PREFIX_PATH}" STREQUAL "")
    set(CMAKE_PREFIX_PATH "${SHADOW_ROOT}")
endif()
## make sure the paths exist
if(NOT EXISTS "${CMAKE_INSTALL_PREFIX}")
    message(FATAL_ERROR "CMAKE_INSTALL_PREFIX path does not exist: '${CMAKE_INSTALL_PREFIX}'")
elseif(NOT EXISTS "${CMAKE_PREFIX_PATH}")
    message(FATAL_ERROR "CMAKE_PREFIX_PATH path does not exist: '${CMAKE_PREFIX_PATH}'")
else()
    message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")
endif()

## we use cmake files from shadow
if(EXISTS "${SHADOW_ROOT}/share/cmake/Modules")
    set(CMAKE_MODULE_PATH "${SHADOW_ROOT}/share/cmake/Modules" ${CMAKE_MODULE_PATH})
endif()
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
message(STATUS "CMAKE_MODULE_PATH = ${CMAKE_MODULE_PATH}")
include_directories(${CMAKE_MODULE_PATH})

## we need to use LLVM to build the plug-ins
set(LLVMHoistGlobalsPATH "${SHADOW_ROOT}/lib/LLVMHoistGlobals.so")
if(EXISTS "${LLVMHoistGlobalsPATH}")
    message(STATUS "LLVMHoistGlobalsPATH = ${LLVMHoistGlobalsPATH}")
else()
    message(FATAL_ERROR "LLVMHoistGlobalsPATH path does not exist: '${LLVMHoistGlobalsPATH}'")
endif()
include(LLVMTools)
add_custom_target(LLVMHoistGlobals)

## the plug-ins need to include shd-library.h and cmake files from shadow
include_directories(${SHADOW_ROOT}/include)

## plug-ins need to disable fortification to ensure syscalls are intercepted
#add_definitions(-D_GNU_SOURCE)
add_cflags("-fno-inline -fno-strict-aliasing")

## CFLAGS status update
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

add_subdirectory(src)
