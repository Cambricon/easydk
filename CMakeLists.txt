cmake_minimum_required(VERSION 2.8.7)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

# ---[ easydk project
project(easydk C CXX)

set(EDK_VERSION 2.3.0)
set(EDK_VERSION_MAJOR 2)

#######################################################################
# -- Build Flags

option(MLU "MLU platform" MLU270)

option(BUILD_TESTS "Build all of modules' unit-tests" OFF)
option(BUILD_SAMPLES "Build samples" OFF)
option(RELEASE "Build type" ON)
option(CODE_COVERAGE_TEST "Build code coverage test" OFF)

option(WITH_CODEC "Build easy codec" ON)
option(WITH_INFER "Build easy infer" ON)
option(WITH_TRACKER "Build easy tracker" ON)
option(WITH_BANG "Build easy Bang" ON)
option(WITH_CNPLUGIN "Build CNPlugin operator" OFF)
option(ENABLE_KCF "Build with KCF track" OFF)

option(WITH_TURBOJPEG "Build Turbo Jpeg" ON)

option(SANITIZE_MEMORY "Enable MemorySanitizer for sanitized targets." OFF)
option(SANITIZE_ADDRESS "Enable AddressSanitizer for sanitized targets." OFF)
option(SANITIZE_THREAD "Enable ThreadSanitizer for sanitized targets." OFF)
option(SANITIZE_UNDEFINED "Enable UndefinedBehaviorSanitizer for sanitized targets." OFF)

if (RELEASE)
  set(CMAKE_BUILD_TYPE RELEASE)
else()
  set(CMAKE_BUILD_TYPE DEBUG)
endif()

set(CMAKE_CXX_FLAGS "-fPIC -Wall -Werror -std=c++11 -D_REENTRANT")
if(CODE_COVERAGE_TEST)
  message(STATUS "Build with code coverage test")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
endif()
set(CMAKE_CXX_FLAGS_DEBUG "-DDEBUG -g")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3")
set(CMAKE_SHARED_LINKER_FLAGS "-Wl,--as-needed")

set(CMAKE_INSTALL_DEFAULT_PREFIX "${PROJECT_SOURCE_DIR}/dist")
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_DEFAULT_PREFIX}" CACHE PATH
    "Directory into which to install ${CMAKE_PROJECT_NAME} (default: ${CMAKE_INSTALL_DEFAULT_PREFIX})"
    FORCE)
endif()
message(STATUS "CMAKE_INSTALL_PREFIX = ${CMAKE_INSTALL_PREFIX}")

message(STATUS "!!!!! BUILD ${CMAKE_BUILD_TYPE} !!!!!")

if (SANITIZE_MEMORY OR SANITIZE_ADDRESS OR SANITIZE_THREAD OR SANITIZE_UNDEFINED) 
  set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
  find_package(Sanitizers)
endif()

#######################################################################

# judge platform according to MLU option and CMAKE_TOOLCHAIN_FILE
if (MLU STREQUAL MLU270 OR MLU STREQUAL MLU220)
  message(STATUS "generate MLU200 CLOUD Makefile")
  set(MLU_PLATFORM MLU200CLOUD)
elseif (MLU STREQUAL MLU220EDGE)
  if (CMAKE_TOOLCHAIN_FILE)
    message(STATUS "generate MLU200 EDGE Makefile")
    set(MLU_PLATFORM MLU200EDGE)
  else()
    message(FATAL_ERROR "compiling on MLU220 EDGE need a cross-compile toolchain file")
  endif()
else()
  message(STATUS "generate default (MLU200 CLOUD) Makefile")
  set(MLU_PLATFORM MLU200CLOUD)
endif()

# platform involved dependencies
if (MLU_PLATFORM STREQUAL MLU200EDGE)
  # ---------------------- MLU200 EDGE --------------------------
  # neuware include/libs
  if(NOT DEFINED ENV{NEUWARE_HOME})
    message(FATAL_ERROR "compiling on MLU220 EDGE need environment variable `NEUWARE_HOME`")
  endif()
  set(NEUWARE_HOME $ENV{NEUWARE_HOME})

  set(CODE_DEFINITIONS "EDK_MLU200_EDGE")

  # disable KCF and CNPLUGIN
  if (WITH_TRACKER AND ENABLE_KCF)
    message(WARNING "disable KCF since it's not supported on MLU220EDGE for now")
    set(ENABLE_KCF OFF)
  endif()
  if (WITH_CNPLUGIN)
    message(WARNING "disable cnplugin since it's not supported on MLU220EDGE for now")
    set(WITH_CNPLUGIN OFF)
  endif()

  # ---[ Google-glog
  find_path(GLOG_HEADER NAMES logging.h
            PATHS ${PROJECT_SOURCE_DIR}/dist/include/glog
            REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(GLOG_LIBRARIES NAMES glog
               PATHS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  message(STATUS "Found Glog library: ${GLOG_LIBRARIES}")
  message(STATUS "Found Glog headers: ${GLOG_HEADER}")
  set(GLOG_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/dist/include)
else()
  # ---------------------- MLU200 CLOUD --------------------------
  # neuware include/libs
  if(NOT DEFINED ENV{NEUWARE_HOME})
    set(ENV{NEUWARE_HOME} /usr/local/neuware)
  endif()
  set(NEUWARE_HOME $ENV{NEUWARE_HOME})

  set(CODE_DEFINITIONS "EDK_MLU200_CLOUD")

  # cnplugin
  if(WITH_CNPLUGIN)
    # ---[ cnml
    set(CNML_LIBS ${NEUWARE_HOME}/lib64/libcnml.so)
    # ---[ cnplugin
    set(CNPLUGIN_LIBS ${NEUWARE_HOME}/lib64/libcnplugin.so)
  endif()

  # ---[ Google-glog
  include("${PROJECT_SOURCE_DIR}/cmake/FindGlog.cmake")
endif()

# set runtime path
set(CMAKE_BUILD_RPATH "$ORIGIN;${NEUWARE_HOME}/lib64;")
set(CMAKE_INSTALL_RPATH "$ORIGIN;${CMAKE_INSTALL_PREFIX}/lib;${NEUWARE_HOME}/lib64;")

# ---[ cnrt
set(CNRT_LIBS "${NEUWARE_HOME}/lib64/libcnrt.so")

# ---[ cncodec
if(WITH_CODEC)
  set(CNCODEC_LIBS "${NEUWARE_HOME}/lib64/libcncodec.so")
  if (MLU_PLATFORM STREQUAL MLU200EDGE)
    list(APPEND CNCODEC_LIBS "${NEUWARE_HOME}/lib64/libion.so")
  endif()
endif()

if(WITH_TURBOJPEG)
  message("------------------- Turbo-JPEG and libyuv -----------------------")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fvisibility=hidden")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
  list(APPEND CODE_DEFINITIONS "ENABLE_TURBOJPEG")
  add_subdirectory(3rdparty/libjpeg-turbo)
  add_subdirectory(3rdparty/libyuv)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fvisibility=default")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=default")
  set(TURBOJPEG_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/3rdparty/libyuv/include"
                             "${PROJECT_SOURCE_DIR}/3rdparty/libjpeg-turbo")
  set(TURBOJPEG_LIBRARIES turbojpeg-static yuv)
  message("------------------- Turbo-JPEG and libyuv END -----------------------")
endif()


# ------------------- Sources -----------------------

file(GLOB cxxutil_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/cxxutil/*.cpp)
file(GLOB device_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/device/*.cpp)
install(DIRECTORY include/cxxutil DESTINATION include)
install(DIRECTORY include/device DESTINATION include)

if(WITH_INFER)
  message(STATUS "Build with EasyInfer")
  file(GLOB infer_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easyinfer/*.cpp)
  install(DIRECTORY include/easyinfer DESTINATION include)
endif()

if(WITH_CODEC)
  message(STATUS "Build with EasyCodec")
  file(GLOB codec_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easycodec/*.cpp)
  install(DIRECTORY include/easycodec DESTINATION include)
endif()

if(WITH_TRACKER)
  message(STATUS "Build with EasyTrack")
  file(GLOB_RECURSE track_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easytrack/*.cpp)
  if(ENABLE_KCF)
    set(KCF_LIBS "${CMAKE_CURRENT_SOURCE_DIR}/src/easytrack/kcf/libkcf_mlu270.a")
    list(APPEND CODE_DEFINITIONS "ENABLE_KCF")
  endif()
  install(DIRECTORY include/easytrack DESTINATION include)
endif()

if(WITH_BANG)
  message(STATUS "Build with EasyBang")
  if ((CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64) OR (CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64))
    file(GLOB_RECURSE resize_and_convert_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize_and_convert/kernel_${CMAKE_SYSTEM_PROCESSOR}.o ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize_and_convert/*.cpp)
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
      file(GLOB_RECURSE resize_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize/kernel_x86_64_mlu270.o ${CMAKE_CURRENT_SOURCE_DIR}/src/easybang/resize/*.cpp)
    endif()
    install(DIRECTORY include/easybang DESTINATION include)
  else()
    message(FATAL_ERROR "error system processor !!!")
  endif()
endif()

if(WITH_CNPLUGIN)
  file(GLOB_RECURSE plugin_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/easyplugin/*.cpp)
  install(DIRECTORY include/easyplugin DESTINATION include)
endif()

message(STATUS "@@@@@@@@@@@ Target : easydk")
add_library(easydk SHARED
            ${cxxutil_srcs}
            ${device_srcs}
            ${infer_srcs}
            ${codec_srcs}
            ${resize_srcs}
            ${resize_and_convert_srcs}
            ${track_srcs}
            ${plugin_srcs})

target_include_directories(easydk PRIVATE
                           ${NEUWARE_HOME}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/include
                           ${GLOG_INCLUDE_DIRS}
                           ${TURBOJPEG_INCLUDE_DIRS})

target_link_libraries(easydk PUBLIC
                      ${CNRT_LIBS}
                      ${CNCODEC_LIBS}
                      ${CNML_LIBS}
                      ${CNPLUGIN_LIBS}
                      ${KCF_LIBS}
                      ${GLOG_LIBRARIES})

if(WITH_TURBOJPEG)
  target_link_libraries(easydk PRIVATE ${TURBOJPEG_LIBRARIES})
endif()

target_compile_definitions(easydk PRIVATE ${CODE_DEFINITIONS})

set_target_properties(easydk PROPERTIES VERSION ${EDK_VERSION})
set_target_properties(easydk PROPERTIES SOVERSION ${EDK_VERSION_MAJOR})

install(TARGETS easydk LIBRARY DESTINATION lib)

if(BUILD_SAMPLES AND WITH_INFER AND WITH_CODEC AND WITH_TRACKER AND WITH_BANG)
  message(STATUS "---------------- Build samples -----------------")
  add_subdirectory(samples/stream-app)
endif()

if(BUILD_TESTS)
  message(STATUS "----------------- Build tests ------------------")
  add_subdirectory(3rdparty/googletest)
  add_subdirectory(tests)
  # This must be set in the root directory for the tests to be run by
  # 'make test' or ctest.
  enable_testing()
endif()

