cmake_minimum_required (VERSION 3.10)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/obj")
#SET(CMAKE_RANLIB "echo")

project (CameraMonitor)
INCLUDE(CheckIncludeFiles)
INCLUDE(CheckLibraryExists)
INCLUDE(CheckFunctionExists)
INCLUDE(${CMAKE_ROOT}/Modules/FindPkgConfig.cmake)

SET(LIBS pthread curl)

SET(DSL_DIRECTORY ${PROJECT_SOURCE_DIR}/DSL/dslsrc)
include_directories(${DSL_DIRECTORY})

add_definitions(-pthread -Wall -D_REENTRANT -D_THREAD_SAFE -D_POSIX_C_SOURCE=200112 -D_FORTIFY_SOURCE=2 -D_FILE_OFFSET_BITS=64 -DENABLE_LIBEVENT -DENABLE_SQLITE)

SET(DSL_SHARED 0)
SET(LIBS ${LIBS} dl)
add_definitions(-fPIC -DIB_PIC)

#optimizations
add_definitions(-O3)
SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -s")
SET(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -s")
SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -s")

#enable debugging
#add_definitions(-D_GLIBCXX_DEBUG -DDEBUG -D_DEBUG)
#add_definitions(-DDEBUG -D_DEBUG)
#set(DEBUG 1)
#add_definitions(-g -ggdb)

pkg_check_modules(LIBEVENT REQUIRED libevent libevent_pthreads)
include_directories(${LIBEVENT_INCLUDE_DIRS})
link_directories(${LIBEVENT_LIBRARY_DIRS})
SET(LIBS ${LIBS} ${LIBEVENT_LIBRARIES})

pkg_search_module(LIBUNIVALUE REQUIRED libunivalue univalue)
include_directories(${LIBUNIVALUE_INCLUDE_DIRS})
link_directories(${LIBUNIVALUE_LIBRARY_DIRS})
SET(LIBS ${LIBS} ${LIBUNIVALUE_LIBRARIES})

pkg_search_module(LIBMOSQUITTO REQUIRED libmosquitto)
include_directories(${LIBMOSQUITTO_INCLUDE_DIRS})
link_directories(${LIBMOSQUITTO_LIBRARY_DIRS})
SET(LIBS ${LIBS} ${LIBMOSQUITTO_LIBRARIES})

pkg_search_module(SQLITE REQUIRED sqlite3 sqlite)
include_directories(${SQLITE_INCLUDE_DIRS})
link_directories(${SQLITE_LIBRARY_DIRS})
SET(LIBS ${LIBS} ${SQLITE_LIBRARIES})

add_subdirectory (DSL)
SET(LIBS ibdsl ${LIBS})

add_subdirectory(meshcore-companion-mqtt)

add_subdirectory(./libmeshcoremqttclient)
SET(LIBS meshcoremqttclient ${LIBS})

add_subdirectory(meshcore-mqtt-irc)
