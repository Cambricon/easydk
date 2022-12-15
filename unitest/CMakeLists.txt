cmake_minimum_required(VERSION 3.5)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

# ---[ 3rdparty
set(3RDPARTY_LIBS "")

# ---[ opencv
set(OpenCV_FIND_QUIETLY true)
# find_package(OpenCV REQUIRED) //this will include opencv_ts which result in crash on centos
find_package(OpenCV OPTIONAL_COMPONENTS imgcodecs)
find_package(OpenCV OPTIONAL_COMPONENTS videoio)
set(optional_opencv_libs ${OpenCV_LIBS})
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if(OpenCV_FOUND)
  if(imgcodecs_FOUND)
    list(APPEND OpenCV_LIBS optional_opencv_libs)
  endif()
  include_directories(${OpenCV_INCLUDE_DIRS})
  message(STATUS "opencv include: ${OpenCV_INCLUDE_DIRS}")
  message(STATUS "opencv libraries: ${OpenCV_LIBS}")
else()
  message(FATAL_ERROR "opencv not found!")
endif()
list(APPEND 3RDPARTY_LIBS ${OpenCV_LIBS})

# ---[ gflags
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindGFlags.cmake)
include_directories(${GFLAGS_INCLUDE_DIRS})
list(APPEND 3RDPARTY_LIBS ${GFLAGS_LIBRARIES})

# ---[ glog
include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/FindGlog.cmake)
include_directories(${GLOG_INCLUDE_DIRS})
list(APPEND 3RDPARTY_LIBS ${GLOG_LIBRARIES})

if (PLATFORM MATCHES "MLU370" OR PLATFORM MATCHES "MLU590")
  find_package(MLU)
  include_directories(${NEUWARE_INCLUDE_DIR})
elseif (PLATFORM MATCHES "CE3226")
  find_package(MPS)
  include_directories(${MPS_INCLUDE_DIR})
else()
  message(FATAL_ERROR "Unsupported PLATFORM: ${PLATFORM}")
endif()

set(EASYDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin/)

# list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../samples/cmake)
# ---[ FFmpeg
find_package(FFmpeg REQUIRED)
if (FFMPEG_FOUND)
  include_directories(${FFMPEG_INCLUDE_DIR})
  list(APPEND 3RDPARTY_LIBS ${FFMPEG_LIBRARIES})
  set(HAVE_FFMPEG true)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_FFMPEG")
  if (WITH_FFMPEG_AVDEVICE)
     add_definitions(-DHAVE_FFMPEG_AVDEVICE)
  endif ()
else ()
  message(FATAL_ERROR "ffmpeg not found!")
endif ()

file(GLOB test_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/infer_server/*.cpp
                    ${CMAKE_CURRENT_SOURCE_DIR}/util/*.cpp)

message(STATUS "@@@@@@@@@@@ Target : test_edk")
add_executable(tests_edk ${test_srcs})
add_sanitizers(tests_edk)

target_include_directories(tests_edk PRIVATE
                           ${OpenCV_INCLUDE_DIRS}
                           ${EASYDK_ROOT_DIR}/include
                           ${EASYDK_ROOT_DIR}/include/infer_server
                           ${EASYDK_ROOT_DIR}/src/infer_server
                           ${CMAKE_CURRENT_SOURCE_DIR}/util
                           ${EASYDK_ROOT_DIR}/src/common)

target_link_libraries(tests_edk PRIVATE gtest gtest_main easydk ${CNRT_LIBS} ${3RDPARTY_LIBS} ${MAGICMIND_RUNTIME_LIBS} pthread dl)
target_compile_options(tests_edk PRIVATE "-Wno-deprecated-declarations")
target_compile_definitions(tests_edk PRIVATE ${EDK_DEFINITIONS})

install(TARGETS tests_edk RUNTIME DESTINATION bin)
