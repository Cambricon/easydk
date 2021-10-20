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
option(CNIS_WITH_CURL "Build infer server with curl" ON)
option(CNIS_USE_MAGICMIND "Build infer server with magicmind" OFF)

#######################################################################

set(CNIS_VERSION_MAJOR 1)
set(CNIS_VERSION_MINOR 0)
set(CNIS_VERSION_PATCH 0)
set(CNIS_VERSION "${CNIS_VERSION_MAJOR}.${CNIS_VERSION_MINOR}.${CNIS_VERSION_PATCH}")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
               ${CMAKE_CURRENT_SOURCE_DIR}/include/cnis/config.h)

find_package(MLU)

# set runtime path
set(CMAKE_BUILD_RPATH "$ORIGIN;${NEUWARE_HOME}/lib64;")

# ---[ Google-glog
find_package(Glog)
find_package(Gflags)

if (CNIS_WITH_CONTRIB)
  file(GLOB_RECURSE contrib_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/contrib/*.cpp)
endif()

file(GLOB_RECURSE infer_server_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
                                    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/*.cpp
                                    ${CMAKE_CURRENT_SOURCE_DIR}/src/processor/*.cpp
                                    ${CMAKE_CURRENT_SOURCE_DIR}/src/util/*.cpp)

message(STATUS "@@@@@@@@@@@ Target : cnis")
add_library(cnis SHARED ${infer_server_srcs} ${contrib_srcs})

if (CNIS_WITH_CURL)
  find_package(CURL REQUIRED)
  target_compile_definitions(cnis PRIVATE CNIS_HAVE_CURL)
  target_include_directories(cnis PRIVATE ${CURL_INCLUDE_DIR})
  target_link_libraries(cnis PUBLIC ${CURL_LIBRARIES})
endif()

if (CNIS_USE_MAGICMIND)
  target_link_libraries(cnis PUBLIC ${MAGICMIND_RUNTIME_LIBS})
endif()

if (CNIS_RECORD_PERF)
  target_compile_definitions(cnis PRIVATE CNIS_RECORD_PERF)
endif()

# ---[ cncv
if (CNIS_WITH_CONTRIB AND CNCV_LIBS)
  target_compile_definitions(cnis PRIVATE CNIS_HAVE_CNCV)
  target_link_libraries(cnis PUBLIC ${CNCV_LIBS})
endif()

target_include_directories(cnis PRIVATE
                           ${NEUWARE_INCLUDE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${GLOG_INCLUDE_DIRS}
                           ${GFLAGS_INCLUDE_DIRS})
target_link_libraries(cnis PUBLIC ${CNDRV_LIBS} ${CNRT_LIBS} ${GLOG_LIBRARIES} ${GFLAGS_LIBRARIES} easydk)

set_target_properties(cnis PROPERTIES VERSION ${CNIS_VERSION})
set_target_properties(cnis PROPERTIES SOVERSION ${CNIS_VERSION_MAJOR})

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_SAMPLES)
  add_subdirectory(samples)
endif()

if (CNIS_WITH_CONTRIB)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/cnis
          DESTINATION include)
else()
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/cnis
          DESTINATION include
          PATTERN "contrib" EXCLUDE)
endif()

install(TARGETS cnis LIBRARY DESTINATION lib)
