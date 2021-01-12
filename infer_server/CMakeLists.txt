cmake_minimum_required(VERSION 2.8.7)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

# ---[ cnis project
project(cnis CXX)

#######################################################################
# -- Build Flags

option(CNIS_RECORD_PERF "Enable record performance" ON)
option(CNIS_WITH_CONTRIB "Build contrib content" ON)
option(CNIS_WITH_CURL "Build infer server with curl" OFF)

#######################################################################

# neuware include/libs
if(NOT DEFINED ENV{NEUWARE_HOME})
  set(ENV{NEUWARE_HOME} /usr/local/neuware)
endif()

set(NEUWARE_HOME $ENV{NEUWARE_HOME})

# set runtime path
set(CMAKE_BUILD_RPATH "$ORIGIN;${NEUWARE_HOME}/lib64;")

# ---[ cnrt
set(CNRT_LIBS "${NEUWARE_HOME}/lib64/libcnrt.so")

# ---[ Google-glog
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindGlog.cmake")

if (CNIS_WITH_CONTRIB)
  set(CONTRIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/contrib)
  file(GLOB_RECURSE contrib_srcs ${CMAKE_CURRENT_SOURCE_DIR}/contrib/video/*.cpp)
endif()

file(GLOB_RECURSE infer_server_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

message(STATUS "@@@@@@@@@@@ Target : cnis")
add_library(cnis SHARED ${infer_server_srcs} ${contrib_srcs})

if (CNIS_WITH_CURL)
  find_package(CURL REQUIRED)
  target_compile_definitions(cnis PRIVATE CNIS_HAVE_CURL)
  target_include_directories(cnis PRIVATE ${CURL_INCLUDE_DIR})
  target_link_libraries(cnis ${CURL_LIBRARIES})
endif()

if (CNIS_RECORD_PERF)
  target_compile_definitions(cnis PRIVATE CNIS_RECORD_PERF)
endif()

target_include_directories(cnis PRIVATE
                           ${NEUWARE_HOME}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${CONTRIB_INCLUDE_DIR})
target_link_libraries(cnis ${CNRT_LIBS} ${GLOG_LIBRARIES} easydk)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

