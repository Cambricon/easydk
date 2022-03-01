if (NOT CNIS_WITH_CONTRIB)
  message(FATAL_ERROR "CNIS sample depend on cnis/contrib content, enable CNIS_WITH_CONTRIB to build samples!")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---[ FFmpeg
find_package(FFmpeg REQUIRED)
if (FFMPEG_FOUND)
  message(STATUS "FFmpeg Found, include: ${FFMPEG_INCLUDE_DIR}")
  message(STATUS "FFmpeg Found, libraries: ${FFMPEG_LIBRARIES}")
else (FFMPEG_FOUND)
  message(FATAL_ERROR "Can not find FFmpeg libs!")
endif (FFMPEG_FOUND)
list(APPEND Samples_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
list(APPEND Samples_LINK_LIBS ${FFMPEG_LIBRARIES})

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

list(APPEND Samples_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/3rdparty/libyuv/include)
list(APPEND Samples_LINK_LIBS yuv)

# ---[ thread
list(APPEND Samples_LINK_LIBS pthread dl)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

message(STATUS "@@@@@@@@@@@ Target : cnis_demo")

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/postprocess postprocess_srcs)
add_executable(cnis_demo ${CMAKE_CURRENT_SOURCE_DIR}/demo.cpp ${postprocess_srcs})
add_sanitizers(cnis_demo)

target_include_directories(cnis_demo PRIVATE
                           ${Samples_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/postprocess
                           ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cnis_demo easydk ${Samples_LINK_LIBS})

install(TARGETS cnis_demo RUNTIME DESTINATION bin)

message(STATUS "@@@@@@@@@@@ Target : cnis_multi_stream_demo")

add_executable(cnis_multi_stream_demo ${CMAKE_CURRENT_SOURCE_DIR}/multi_stream_demo.cpp ${postprocess_srcs})
add_sanitizers(cnis_multi_stream_demo)

target_include_directories(cnis_multi_stream_demo PRIVATE
                           ${Samples_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/postprocess
                           ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cnis_multi_stream_demo easydk ${Samples_LINK_LIBS})

install(TARGETS cnis_multi_stream_demo RUNTIME DESTINATION bin)
