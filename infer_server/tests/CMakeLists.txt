# ---[ OpenCV
set(OpenCV_FIND_QUIETLY true)
find_package(OpenCV OPTIONAL_COMPONENTS imgcodecs)
set(imgcodecs_libs ${OpenCV_LIBS})
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if(OpenCV_FOUND)
  message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
  message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
else()
  message(FATAL_ERROR "Can not find OpenCV libs!")
endif()
if(imgcodecs_FOUND)
  list(APPEND OpenCV_LIBS ${imgcodecs_libs})
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../samples/cmake)
# ---[ FFmpeg
find_package(FFmpeg REQUIRED)
if (FFMPEG_FOUND)
  message(STATUS "FFmpeg Found, include: ${FFMPEG_INCLUDE_DIR}")
  message(STATUS "FFmpeg Found, libraries: ${FFMPEG_LIBRARIES}")
else (FFMPEG_FOUND)
  message(FATAL_ERROR "Can not find FFmpeg libs!")
endif (FFMPEG_FOUND)

list(APPEND LINK_LIBS dl pthread gtest gtest_main ${GLOG_LIBRARIES} ${OpenCV_LIBS} ${FFMPEG_LIBRARIES} easydk)

# ---[ api tests
file(GLOB apitest_srcs ${CMAKE_CURRENT_SOURCE_DIR}/api/*.cpp)
add_executable(apitest_cnis ${apitest_srcs})
add_sanitizers(apitest_cnis)

if (CNIS_WITH_CONTRIB)
  target_compile_definitions(apitest_cnis PRIVATE CNIS_WITH_CONTRIB)
  if (CNCV_LIBS)
    target_compile_definitions(apitest_cnis PRIVATE HAVE_CNCV)
  endif()
endif()

target_include_directories(apitest_cnis PRIVATE
                           ${NEUWARE_INCLUDE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../src
                           ${CMAKE_CURRENT_SOURCE_DIR}/common
                           ${CMAKE_CURRENT_SOURCE_DIR}/api
                           ${GLOG_INCLUDE_DIRS}
                           ${OpenCV_INCLUDE_DIRS})
target_link_libraries(apitest_cnis ${LINK_LIBS})
target_compile_options(apitest_cnis PRIVATE "-Wno-deprecated-declarations")

# ---[ unit tests
file(GLOB unittest_srcs ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp)
add_executable(unittest_cnis ${unittest_srcs})
add_sanitizers(unittest_cnis)

if (CNIS_RECORD_PERF)
  target_compile_definitions(unittest_cnis PRIVATE CNIS_RECORD_PERF)
endif()

target_include_directories(unittest_cnis PRIVATE
                           ${NEUWARE_INCLUDE_DIR}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../src
                           ${CMAKE_CURRENT_SOURCE_DIR}/common
                           ${CMAKE_CURRENT_SOURCE_DIR}/unit
                           ${GLOG_INCLUDE_DIRS}
                           ${OpenCV_INCLUDE_DIRS})
target_link_libraries(unittest_cnis ${LINK_LIBS})
target_compile_options(unittest_cnis PRIVATE "-Wno-deprecated-declarations")