cmake_minimum_required(VERSION 3.5)

# ---[ easydk project
project(easydk C CXX)

set(EDK_VERSION_MAJOR 4)
set(EDK_VERSION_MINOR 1)
set(EDK_VERSION_PATCH 0)
set(EDK_VERSION "${EDK_VERSION_MAJOR}.${EDK_VERSION_MINOR}.${EDK_VERSION_PATCH}")

#######################################################################
if (NOT PLATFORM)
  set(PLATFORM "MLU370" CACHE STRING "Build target platform")
endif()

option(BUILD_PYTHON_API "Build Python API" OFF)
option(BUILD_TESTS "Build all of modules' unit-tests" ON)
option(BUILD_SAMPLES "Build samples" ON)
option(CODE_COVERAGE_TEST "Build code coverage test" OFF)
option(CNIS_WITH_CURL "Build infer server with curl" ON)
option(CNIS_RECORD_PERF "Enable record performance" ON)

option(SANITIZE_MEMORY "Enable MemorySanitizer for sanitized targets." OFF)
option(SANITIZE_ADDRESS "Enable AddressSanitizer for sanitized targets." OFF)
option(SANITIZE_THREAD "Enable ThreadSanitizer for sanitized targets." OFF)
option(SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer for sanitized targets." OFF)

#######################################################################
set(CMAKE_SYSTEM_NAME Linux)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wall -Werror -fPIC -D_REENTRANT")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c11 -Wall -fPIC -D_REENTRANT")

set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O1")
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--no-as-needed")

if(CODE_COVERAGE_TEST)
  message(STATUS "Build with code coverage test")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
endif()

message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sanitizers)
find_package(Sanitizers)

if (PLATFORM MATCHES "MLU370")
  add_definitions(-DPLATFORM_MLU370)
  find_package(MLU)
elseif (PLATFORM MATCHES "MLU590")
  add_definitions(-DPLATFORM_MLU590)
  find_package(MLU)
elseif (PLATFORM MATCHES "CE3226")
  add_definitions(-DPLATFORM_CE3226)
  find_package(MPS)
else()
  message(FATAL_ERROR "Unsupported PLATFORM: ${PLATFORM}")
endif()

if(HAVE_CN_VB_HEADER)
  add_definitions(-DMPS_VERSION_MAJOR=1)
  add_definitions(-DMPS_VERSION_MINOR=1)
  add_definitions(-DMPS_VERSION_MICRO=0)
else()
  add_definitions(-DMPS_VERSION_MAJOR=1)
  add_definitions(-DMPS_VERSION_MINOR=0)
  add_definitions(-DMPS_VERSION_MICRO=0)
endif()

if(HAVE_MM_COMMON_HEADER)
  add_definitions(-DHAVE_MM_COMMON_HEADER)
endif()

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "!!!!! BUILD ${CMAKE_BUILD_TYPE} !!!!!")
message(STATUS "!!!!! PLATFORM ${PLATFORM} !!!!")

#######################################################################

# set runtime path
set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib)

if (PLATFORM MATCHES "MLU370" OR PLATFORM MATCHES "MLU590")
  set(CMAKE_BUILD_RPATH "${CMAKE_INSTALL_RPATH}/lib64")
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib64")
elseif (PLATFORM MATCHES "CE3226")
  set(CMAKE_BUILD_RPATH "${CMAKE_INSTALL_RPATH}/lib")
  set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
else()
  message(FATAL_ERROR "Unsupported PLATFORM: ${PLATFORM}")
endif()

# ------------------- Sources -----------------------

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mps_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/mps_config.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/edk_config.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/edk_config.h)
install(FILES edk_config.h DESTINATION include)
install(DIRECTORY include DESTINATION include)
install(DIRECTORY include/infer_server/cnis DESTINATION include)

file(GLOB edk_src ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB common_src ${CMAKE_CURRENT_SOURCE_DIR}/src/common/*.cpp)
file(GLOB_RECURSE infer_server_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/infer_server/*.cpp)
file(GLOB_RECURSE cncv_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/transform_cncv/*.cpp)
if (PLATFORM MATCHES "MLU370")
  file(GLOB_RECURSE platform_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/mlu370/*.cpp)
elseif (PLATFORM MATCHES "MLU590")
  file(GLOB_RECURSE platform_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/mlu590/*.cpp)
elseif (PLATFORM MATCHES "CE3226")
  file(GLOB_RECURSE platform_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/ce3226/*.cpp)
endif()


list(APPEND srcs ${edk_src} ${infer_server_srcs} ${platform_srcs} ${cncv_srcs} ${common_src})

message(STATUS "@@@@@@@@@@@ Target : easydk")

add_library (easydk SHARED ${srcs})
target_include_directories(easydk PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/include/infer_server
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/infer_server
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/transform_cncv/)

if (PLATFORM MATCHES "MLU370" OR PLATFORM MATCHES "MLU590")
  target_include_directories(easydk PRIVATE ${NEUWARE_INCLUDE_DIR})
  target_link_libraries(easydk PRIVATE
                        ${CNCODECV3_LIBS}
                        ${MAGICMIND_RUNTIME_LIBS}
                        ${CNCV_LIBS}
                        ${CNRT_LIBS}
                        ${CNDRV_LIBS})
elseif (PLATFORM MATCHES "CE3226")
  target_include_directories(easydk PRIVATE
                             ${MPS_INCLUDE_DIR})
  target_link_libraries(easydk PRIVATE
                        ${SENSOR_LIBS}
                        ${CNCODEC_LIBS}
                        ${CNVO_LIBS}
                        ${CNVPS_LIBS}
                        ${CNVGU_LIBS}
                        ${CNG2D_LIBS}
                        ${CNSYS_LIBS}
                        ${CNCV_LIBS}
                        ${MAGICMIND_RUNTIME_LIBS}
                        ${CNRT_LIBS}
                        ${CNDRV_LIBS})
  message(STATUS ${SENSOR_LIBS})
endif()

# ---[ glog
find_package(Glog REQUIRED)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindGlog.cmake)
include_directories(${GLOG_INCLUDE_DIRS})

target_link_libraries(easydk PRIVATE ${GLOG_LIBRARIES} -dl -pthread -lm)

set_target_properties(easydk PROPERTIES VERSION ${EDK_VERSION})
set_target_properties(easydk PROPERTIES SOVERSION ${EDK_VERSION_MAJOR})
install(TARGETS easydk LIBRARY DESTINATION lib64)


if (CNIS_WITH_CURL)
  find_package(CURL REQUIRED)
  add_definitions(-DCNIS_HAVE_CURL)
  target_include_directories(easydk PRIVATE ${CURL_INCLUDE_DIR})
  target_link_libraries(easydk PUBLIC ${CURL_LIBRARIES})
  if (PLATFORM MATCHES "CE3226")
    find_package(OpenSSL REQUIRED)
    target_include_directories(easydk PRIVATE ${OPENSSL_INCLUDE_DIR})
    target_link_libraries(easydk PUBLIC ${OPENSSL_LIBRARIES})
  endif()
endif()

if (CNIS_RECORD_PERF)
  add_definitions(-DCNIS_RECORD_PERF)
endif()

if (BUILD_PYTHON_API)
  add_subdirectory(python)
endif()

if (${CMAKE_PROJECT_NAME} MATCHES "easydk")
  if (BUILD_SAMPLES)
    message(STATUS "---------------- Build samples -----------------")
    add_subdirectory(3rdparty/libyuv)
    add_subdirectory(samples)
  endif()

  if (BUILD_TESTS)
    message(STATUS "----------------- Build tests ------------------")
    set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin/)
    add_subdirectory(3rdparty/googletest)
    add_subdirectory(unitest)
  endif()

endif()
