cmake_minimum_required(VERSION 2.8.7)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
CHECK_CXX_COMPILER_FLAG("-std=c++0x" COMPILER_SUPPORTS_CXX0X)
if(COMPILER_SUPPORTS_CXX11)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
elseif(COMPILER_SUPPORTS_CXX0X)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
else()
  message(STATUS "The compiler ${CMAKE_CXX_COMPILER} has no C++11 support. Please use a different C++ compiler.")
endif()

include(FindPackageHandleStandardArgs)

#######################################################################
# find GFlags
set(GFLAGS_ROOT_DIR "" CACHE PATH "Folder contains Gflags")

# We are testing only a couple of files in the include directories
if(WIN32)
    find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h
        PATHS ${GFLAGS_ROOT_DIR}/src/windows)
else()
    find_path(GFLAGS_INCLUDE_DIR gflags/gflags.h
        PATHS ${GFLAGS_ROOT_DIR})
endif()

if(MSVC)
    find_library(GFLAGS_LIBRARY_RELEASE
        NAMES libgflags
        PATHS ${GFLAGS_ROOT_DIR}
        PATH_SUFFIXES Release)

    find_library(GFLAGS_LIBRARY_DEBUG
        NAMES libgflags-debug
        PATHS ${GFLAGS_ROOT_DIR}
        PATH_SUFFIXES Debug)

    set(GFLAGS_LIBRARY optimized ${GFLAGS_LIBRARY_RELEASE} debug ${GFLAGS_LIBRARY_DEBUG})
else()
    find_library(GFLAGS_LIBRARY gflags)
endif()

find_package_handle_standard_args(GFlags DEFAULT_MSG GFLAGS_INCLUDE_DIR GFLAGS_LIBRARY)

if(GFLAGS_FOUND)
    set(GFLAGS_INCLUDE_DIRS ${GFLAGS_INCLUDE_DIR})
    set(GFLAGS_LIBRARIES ${GFLAGS_LIBRARY})
    message(STATUS "Found gflags  (include: ${GFLAGS_INCLUDE_DIR}, library: ${GFLAGS_LIBRARY})")
    mark_as_advanced(GFLAGS_LIBRARY_DEBUG GFLAGS_LIBRARY_RELEASE
                     GFLAGS_LIBRARY GFLAGS_INCLUDE_DIR GFLAGS_ROOT_DIR)
endif()


#######################################################################

set(Samples_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---[ Google-gflags
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
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(FFmpeg REQUIRED)
if (FFMPEG_FOUND)
  message(STATUS "ffmpeg include: ${FFMPEG_INCLUDE_DIR}")
  message(STATUS "ffmpeg libraries: ${FFMPEG_LIBRARIES}")
else (FFMPEG_FOUND)
  message(FATAL_ERROR "Can not find FFmpeg libs!")
endif (FFMPEG_FOUND)
list(APPEND Samples_INCLUDE_DIRS ${FFMPEG_INCLUDE_DIR})
list(APPEND Samples_LINK_LIBS ${FFMPEG_LIBRARIES})

if(NOT DEFINED ENV{NEUWARE_HOME})
  set(ENV{NEUWARE_HOME} /usr/local/neuware)
endif()
include_directories("$ENV{NEUWARE_HOME}/include")
link_directories("$ENV{NEUWARE_HOME}/lib64")

# ---[ thread
list(APPEND Samples_LINK_LIBS pthread dl)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} srcs)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

set(name stream-app)
message("target :  ${name}")
add_executable(${name} ${srcs})

if (SANITIZE_MEMORY OR SANITIZE_ADDRESS OR SANITIZE_THREAD OR SANITIZE_UNDEFINED)
    add_sanitizers(${name})
endif()

target_include_directories(${name} PRIVATE ${Samples_INCLUDE_DIRS})
target_link_libraries(${name} ${Samples_LINK_LIBS} easydk)
