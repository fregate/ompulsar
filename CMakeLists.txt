cmake_minimum_required(VERSION 3.14)

project(
    ompulsar
    VERSION 0.1.0
    DESCRIPTION "RSyslog output module for Apache Pulsar"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD 99)

include(cmake/common.cmake)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/thirdparty/cmake")

if ("${RSYSLOG_DIR}" STREQUAL "")
	message(FATAL_ERROR "Have to set rsyslog directory in RSYSLOG_DIR variable")
endif()

set(RSYSLOG_INCLUDE_DIRS
	${RSYSLOG_DIR}/runtime/
	${RSYSLOG_DIR}/grammar/
)

set(RSYSLOG_LIBS_DIR ${RSYSLOG_DIR}/runtime/.libs/)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FASTJSON REQUIRED libfastjson)

# ---- Build Pulsar C++ Client -----
find_package(thirdparty COMPONENTS hashmap pulsar-client)

add_library(ompulsar SHARED
	src/defines.h
	src/ompulsar.c
)

set_target_properties(ompulsar PROPERTIES PREFIX "")

# target_compile_options(ompulsar PRIVATE -fsanitize=address)
# target_link_options(ompulsar PRIVATE -fsanitize=address)

target_link_directories(ompulsar
PRIVATE
	${RSYSLOG_LIBS_DIR}
)

target_include_directories(ompulsar
PRIVATE
	${RSYSLOG_INCLUDE_DIRS}
	${FASTJSON_INCLUDE_DIRS}
)

target_link_libraries(ompulsar
PRIVATE
	rsyslog
	thrdpartyHashMap
	thirdparty::PulsarClient
	${FASTJSON_LIBRARIES}
)
