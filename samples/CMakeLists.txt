list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
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
find_package(FFmpeg REQUIRED)
if (FFMPEG_FOUND)
  message(STATUS "FFmpeg Found, include: ${FFMPEG_INCLUDE_DIR}")
  message(STATUS "FFmpeg Found, libraries: ${FFMPEG_LIBRARIES}")
else (FFMPEG_FOUND)
  message(FATAL_ERROR "Can not find FFmpeg libs!")
endif (FFMPEG_FOUND)
list(APPEND Samples_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
list(APPEND Samples_LINK_LIBS ${FFMPEG_LIBRARIES})

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
# ----- stream_app
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/stream-app stream_app_srcs)

message(STATUS "@@@@@@@@@@@ Target : stream-app")
add_executable(stream-app ${stream_app_srcs} ${common_srcs})
add_sanitizers(stream-app)

target_compile_options(stream-app PRIVATE ${COMPILE_FLAGS})
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
add_sanitizers(classification)

target_compile_options(classification PRIVATE ${COMPILE_FLAGS})
target_include_directories(classification PRIVATE 
                           ${Samples_INCLUDE_DIRS}
                           ${PROJECT_SOURCE_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_link_libraries(classification easydk ${Samples_LINK_LIBS})

install(TARGETS classification RUNTIME DESTINATION bin)
