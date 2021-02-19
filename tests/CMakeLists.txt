# ---[ OpenCV
if (MLU_PLATFORM STREQUAL MLU200EDGE)
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
  set(OpenCV_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/dist/include)
  set(OpenCV_LIBS ${OpenCV_CORE} ${OpenCV_HIGHGUI} ${OpenCV_IMGPROC} ${OpenCV_IMGCODEC})
  message(STATUS "OpenCV Found, headers: ${OpenCV_HEADER}")
  message(STATUS "OpenCV Found, libraries: ${OpenCV_LIBS}")
else()
  set(OpenCV_FIND_QUIETLY true)
  find_package(OpenCV OPTIONAL_COMPONENTS imgcodecs)
  set(imgcodecs_libs ${OpenCV_LIBS})
  find_package(OpenCV REQUIRED core imgproc highgui)
  if(OpenCV_FOUND)
    message(STATUS "OpenCV Found, libraries: ${OpenCV_LIBS}")
  else()
    message(FATAL_ERROR "Can not find OpenCV libs!")
  endif()
  if(imgcodecs_FOUND)
    list(APPEND OpenCV_LIBS ${imgcodecs_libs})
  endif()
endif()

file(GLOB test_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

if (MLU_PLATFORM STREQUAL MLU200CLOUD)
  list(APPEND test_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/resize/test_resize_yuv2yuv_invoke.cpp)
endif()
if (WITH_CNPLUGIN)
  file(GLOB test_plugin_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.cpp)
  list(APPEND test_srcs ${test_plugin_srcs})
endif()

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)
message(STATUS "@@@@@@@@@@@ Target : test_edk")
add_executable(tests_edk ${test_srcs})

add_sanitizers(tests_edk)

target_include_directories(tests_edk PRIVATE
                           ${NEUWARE_HOME}/include
                           ${OpenCV_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tests_edk PRIVATE pthread dl gtest gtest_main easydk ${OpenCV_LIBS})
target_compile_options(tests_edk PRIVATE "-Wno-deprecated-declarations")

install(TARGETS tests_edk RUNTIME DESTINATION bin)
