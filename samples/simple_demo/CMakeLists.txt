# ---[ Google-gflags
find_package(GFlags)
list(APPEND Samples_INCLUDE_DIRS ${GFLAGS_INCLUDE_DIRS})
list(APPEND Samples_LINK_LIBS ${GFLAGS_LIBRARIES})

# ---[ OpenCV
set(OpenCV_FIND_QUIETLY true)
find_package(OpenCV OPTIONAL_COMPONENTS imgcodecs)
set(imgcodecs_libs ${OpenCV_LIBS})
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if (OpenCV_FOUND)
  message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
  message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
else (OpenCV_FOUND)
  message(FATAL_ERROR "Can not find OpenCV libs!")
endif ()
if (imgcodecs_FOUND)
  list(APPEND OpenCV_LIBS ${imgcodecs_libs})
endif ()
list(APPEND Samples_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
list(APPEND Samples_LINK_LIBS ${OpenCV_LIBS})

# # ---[ FFmpeg
# find_package(FFmpeg REQUIRED)
# if (FFMPEG_FOUND)
#   message(STATUS "FFmpeg Found, include: ${FFMPEG_INCLUDE_DIR}")
#   message(STATUS "FFmpeg Found, libraries: ${FFMPEG_LIBRARIES}")
# else (FFMPEG_FOUND)
#   message(FATAL_ERROR "Can not find FFmpeg libs!")
# endif (FFMPEG_FOUND)
# list(APPEND Samples_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
# list(APPEND Samples_LINK_LIBS ${FFMPEG_LIBRARIES})


find_package(FFmpeg REQUIRED)
if (FFMPEG_FOUND)
  include_directories(${FFMPEG_INCLUDE_DIR})
  list(APPEND 3RDPARTY_LIBS ${FFMPEG_LIBRARIES})
  list(APPEND Samples_LINK_LIBS ${FFMPEG_LIBRARIES})
  set(HAVE_FFMPEG true)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DHAVE_FFMPEG")
  if (WITH_FFMPEG_AVDEVICE)
     add_definitions(-DHAVE_FFMPEG_AVDEVICE)
  endif ()
else ()
  message(FATAL_ERROR "ffmpeg not found!")
endif ()



if (PLATFORM MATCHES "MLU370" OR PLATFORM MATCHES "MLU590")
  find_package(MLU)
  include_directories(${NEUWARE_INCLUDE_DIR})
elseif (PLATFORM MATCHES "CE3226")
  find_package(MPS)
  include_directories(${MPS_INCLUDE_DIR})
else()
  message(FATAL_ERROR "Unsupported PLATFORM: ${PLATFORM}")
endif()

set(EASYDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)


# ---[ glog
include(${EASYDK_ROOT_DIR}/cmake/FindGlog.cmake)
include_directories(${GLOG_INCLUDE_DIRS})
list(APPEND Samples_LINK_LIBS ${GLOG_LIBRARIES})

# ---[ LibYuv
# list(APPEND Samples_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/3rdparty/libyuv/include)
# list(APPEND Samples_LINK_LIBS yuv)

# ---[ thread
list(APPEND Samples_LINK_LIBS pthread dl)


# workaround for gcc known issue
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  set(COMPILE_FLAGS -Wno-uninitialized)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  set(COMPILE_FLAGS -Wno-maybe-uninitialized)
else()
  message(FATAL_ERROR "Unsupported Compiler!")
endif()

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/common common_srcs)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/common/util common_util_srcs)

# ----- classification
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/classification classification_srcs)

message(STATUS "@@@@@@@@@@@ Target : classification")
add_executable(classification ${classification_srcs} ${common_srcs} ${common_util_srcs})
add_sanitizers(classification)

target_compile_options(classification PRIVATE ${COMPILE_FLAGS})

target_include_directories(classification PRIVATE
                           ${Samples_INCLUDE_DIRS}
                           ${EASYDK_ROOT_DIR}/include
                           ${EASYDK_ROOT_DIR}/include/infer_server
                           ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(classification easydk ${Samples_LINK_LIBS} ${CNRT_LIBS})

install(TARGETS classification RUNTIME DESTINATION bin)



message(STATUS "@@@@@@@@@@@ Target : detection")

# ----- stream_app
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/detection detection_srcs)

add_executable(detection ${detection_srcs} ${common_srcs} ${common_util_srcs})
add_sanitizers(detection)

target_compile_options(detection PRIVATE ${COMPILE_FLAGS})


target_include_directories(detection PRIVATE
                           ${Samples_INCLUDE_DIRS}
                           ${EASYDK_ROOT_DIR}/include
                           ${EASYDK_ROOT_DIR}/include/infer_server
                           ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(detection easydk ${Samples_LINK_LIBS} ${CNRT_LIBS})

install(TARGETS detection RUNTIME DESTINATION bin)

