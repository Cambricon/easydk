cmake_minimum_required(VERSION 3.5)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

set(EASYDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(3RDPARTY_LIBS "")

# ---[ Google-gflags
find_package(GFlags)
include_directories(${GFLAGS_INCLUDE_DIRS})
list(APPEND 3RDPARTY_LIBS ${GFLAGS_LIBRARIES})

# ---[ glog
include(${EASYDK_ROOT_DIR}/cmake/FindGlog.cmake)
include_directories(${GLOG_INCLUDE_DIRS})
list(APPEND 3RDPARTY_LIBS ${GLOG_LIBRARIES})

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

# ---[ opencv
set(OpenCV_FIND_QUIETLY true)
#find_package(OpenCV REQUIRED) //this will include opencv_ts which result in crash on centos
find_package(OpenCV OPTIONAL_COMPONENTS videoio)
set(optional_opencv_libs ${OpenCV_LIBS})
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if(OpenCV_FOUND)
  if(imgcodecs_FOUND)
    list(APPEND OpenCV_LIBS optional_opencv_libs)
  endif()
  include_directories(${OpenCV_INCLUDE_DIRS})
  list(APPEND 3RDPARTY_LIBS ${OpenCV_LIBS})
  message(STATUS "opencv include: ${OpenCV_INCLUDE_DIRS}")
  message(STATUS "opencv libraries: ${OpenCV_LIBS}")
else()
  message(FATAL_ERROR "opencv not found!")
endif()

# ---[ libyuv
list(APPEND ADDITIONAL_MAKE_CLEAN_FILES_LIST "${EASYDK_ROOT_DIR}/3rdparty/libyuv")
add_custom_target(cnstream_libyuv sh ${EASYDK_ROOT_DIR}/tools/build_libyuv.sh ${CMAKE_C_COMPILER} ${CMAKE_CXX_COMPILER})
list(APPEND DEPENDENCIES cnstream_libyuv)
include_directories(${EASYDK_ROOT_DIR}/3rdparty/libyuv/include)
link_directories(${EASYDK_ROOT_DIR}/3rdparty/libyuv/lib)
list(APPEND 3RDPARTY_LIBS ${EASYDK_ROOT_DIR}/lib/libyuv.a)

if (PLATFORM MATCHES "MLU370" OR PLATFORM MATCHES "MLU590")
  find_package(MLU)
  include_directories(${NEUWARE_INCLUDE_DIR})
elseif (PLATFORM MATCHES "CE3226")
  find_package(MPS)
  include_directories(${MPS_INCLUDE_DIR})
else()
  message(FATAL_ERROR "Unsupported PLATFORM: ${PLATFORM}")
endif()

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/decode
  ${CMAKE_CURRENT_SOURCE_DIR}/encode
  ${CMAKE_CURRENT_SOURCE_DIR}/inference
  ${CMAKE_CURRENT_SOURCE_DIR}/osd
  ${EASYDK_ROOT_DIR}/include
  ${EASYDK_ROOT_DIR}/include/infer_server
  ${CMAKE_CURRENT_SOURCE_DIR}/common
  ${CMAKE_CURRENT_SOURCE_DIR}/framework
)

file(GLOB_RECURSE srcs
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/encode/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/decode/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/inference/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/osd/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/framework/*.cpp
)

add_executable(stream_app ${srcs})
target_link_libraries(stream_app PUBLIC ${3RDPARTY_LIBS} easydk ${CNRT_LIBS} gtest glog -dl -pthread -lm)