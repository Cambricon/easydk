if(MLU_PLATFORM STREQUAL MLU200EDGE)
  list(APPEND Samples_INCLUDE_DIRS ${PROJECT_SOURCE_DIR}/dist/include)

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
else()
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
endif()

# ---[ thread
list(APPEND Samples_LINK_LIBS pthread dl)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

message(STATUS "@@@@@@@@@@@ Target : cnis_demo")
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} sample_srcs)

add_executable(cnis_demo ${sample_srcs})

if (SANITIZE_MEMORY OR SANITIZE_ADDRESS OR SANITIZE_THREAD OR SANITIZE_UNDEFINED)
  add_sanitizers(cnis_demo)
endif()

target_include_directories(cnis_demo PRIVATE 
                           ${Samples_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../contrib
                           ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cnis_demo cnis ${Samples_LINK_LIBS})

install(TARGETS cnis_demo RUNTIME DESTINATION bin)
