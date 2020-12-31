if(MLU_PLATFORM STREQUAL MLU200EDGE)
  list(APPEND Samples_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/dist/include)

  # ---[ Google-gflags
  find_path(GFLAGS_HEADER NAMES gflags.h
            PATHS ${PROJECT_SOURCE_DIR}/dist/include/gflags
            REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(GFLAGS_LIBRARIES NAMES gflags
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  message(STATUS "Gflags Found, headers: ${GFLAGS_HEADER}")
  message(STATUS "Gflags Found, libraries: ${GFLAGS_LIBRARIES}")
  list(APPEND Samples_LINK_LIBS ${GFLAGS_LIBRARIES})

  # ---[ OpenCV
  find_path(OpenCV_HEADER NAMES opencv.hpp
            PATHS ${PROJECT_SOURCE_DIR}/dist/include/opencv2
            REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(OpenCV_CORE NAMES opencv_core
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(OpenCV_IMGPROC NAMES opencv_imgproc
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(OpenCV_HIGHGUI NAMES opencv_highgui
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(OpenCV_IMGCODEC NAMES opencv_imgcodecs
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               NO_CMAKE_FIND_ROOT_PATH)
  find_library(OpenCV_FEATURE NAMES opencv_features2d
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(OpenCV_VIDEOIO NAMES opencv_videoio
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  set(OpenCV_LIBS
      ${OpenCV_CORE}
      ${OpenCV_IMGPROC}
      ${OpenCV_IMGCODEC}
      ${OpenCV_HIGHGUI}
      ${OpenCV_FEATURE}
      ${OpenCV_VIDEOIO})
  message(STATUS "OpenCV Found, headers: ${OpenCV_HEADER}")
  message(STATUS "OpenCV Found, libraries: ${OpenCV_LIBS}")
  list(APPEND Samples_LINK_LIBS ${OpenCV_LIBS})

  # ---[ FFmpeg
  find_path(FFMPEG_HEADER NAMES libavcodec/avcodec.h libavformat/avformat.h libavutil/avutil.h
            PATHS ${PROJECT_SOURCE_DIR}/dist/include
            REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(FFMPEG_FORMAT NAMES avformat
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(FFMPEG_CODEC NAMES avcodec
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  find_library(FFMPEG_UTIL NAMES avutil
               HINTS ${PROJECT_SOURCE_DIR}/dist/lib
               REQUIRED NO_CMAKE_FIND_ROOT_PATH)
  set(FFMPEG_LIBRARIES ${FFMPEG_FORMAT} ${FFMPEG_UTIL} ${FFMPEG_CODEC})
  message(STATUS "FFMpeg Found, headers: ${FFMPEG_HEADER}")
  message(STATUS "FFMpeg Found, libraries: ${FFMPEG_LIBRARIES}")
  list(APPEND Samples_LINK_LIBS ${FFMPEG_LIBRARIES})
else()
  # ---[ Google-gflags
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindGFlags.cmake)
  list(APPEND Samples_INCLUDE_DIRS ${GFLAGS_INCLUDE_DIRS})
  list(APPEND Samples_LINK_LIBS ${GFLAGS_LIBRARIES})

  # ---[ OpenCV
  set(OpenCV_FIND_QUIETLY true)
  find_package(OpenCV OPTIONAL_COMPONENTS imgcodecs)
  set(imgcodecs_libs ${OpenCV_LIBS})
  find_package(OpenCV REQUIRED core imgproc highgui features2d)
  if (OpenCV_FOUND)
    message(STATUS "OpenCV Found")
  else (OpenCV_FOUND)
    message(FATAL_ERROR "Can not find OpenCV libs!")
  endif ()
  if (imgcodecs_FOUND)
    list(APPEND OpenCV_LIBS ${imgcodecs_libs})
  endif ()
  list(APPEND Samples_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS})
  list(APPEND Samples_LINK_LIBS ${OpenCV_LIBS})

  # ---[ FFmpeg
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
  find_package(FFmpeg REQUIRED)
  if (FFMPEG_FOUND)
    message(STATUS "FFmpeg Found, include: ${FFMPEG_INCLUDE_DIR}")
    message(STATUS "FFmpeg Found, libraries: ${FFMPEG_LIBRARIES}")
  else (FFMPEG_FOUND)
    message(FATAL_ERROR "Can not find FFmpeg libs!")
  endif (FFMPEG_FOUND)
  list(APPEND Samples_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
  list(APPEND Samples_LINK_LIBS ${FFMPEG_LIBRARIES})
endif()

# ---[ thread
list(APPEND Samples_LINK_LIBS pthread dl)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/common common_srcs)
# ----- stream_app
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/stream-app stream_app_srcs)

message(STATUS "@@@@@@@@@@@ Target : stream-app")
add_executable(stream-app ${stream_app_srcs} ${common_srcs})

if (SANITIZE_MEMORY OR SANITIZE_ADDRESS OR SANITIZE_THREAD OR SANITIZE_UNDEFINED)
  add_sanitizers(stream-app)
endif()

# workaround for gcc known issue
target_compile_options(stream-app PRIVATE -Wno-maybe-uninitialized)

target_include_directories(stream-app PRIVATE 
                           ${Samples_INCLUDE_DIRS}
                           ${PROJECT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(stream-app easydk ${Samples_LINK_LIBS})

install(TARGETS stream-app RUNTIME DESTINATION bin)

# ----- classification
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/classification classification_srcs)

message(STATUS "@@@@@@@@@@@ Target : classification")
add_executable(classification ${classification_srcs} ${common_srcs})

if (SANITIZE_MEMORY OR SANITIZE_ADDRESS OR SANITIZE_THREAD OR SANITIZE_UNDEFINED)
  add_sanitizers(classification)
endif()

# workaround for gcc known issue
target_compile_options(classification PRIVATE -Wno-maybe-uninitialized)

target_include_directories(classification PRIVATE 
                           ${Samples_INCLUDE_DIRS}
                           ${PROJECT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(classification easydk ${Samples_LINK_LIBS})

install(TARGETS classification RUNTIME DESTINATION bin)
