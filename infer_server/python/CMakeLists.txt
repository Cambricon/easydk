cmake_minimum_required(VERSION 2.8.7)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

set(CNIS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# compile flags
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -D_REENTRANT -fPIC -Wno-deprecated-declarations -Wall -Werror")

# ---[ neuware
find_package(MLU)

# ---[ pybind11
set(PYBIND11_PYTHON_VERSION 3)
add_subdirectory(${CNIS_ROOT_DIR}/../3rdparty/pybind11 ${CMAKE_CURRENT_BINARY_DIR}/pybind11)

# ---[ glog
find_package(Glog)

# ---[ opencv
set(OpenCV_FIND_QUIETLY true)
#find_package(OpenCV REQUIRED) //this will include opencv_ts which result in crash on centos
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if(OpenCV_FOUND)
  message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
  message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
else()
  message(FATAL_ERROR "opencv not found!")
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

set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/ CNIS_PYAPI_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/samples/ CNIS_PYAPI_SAMPLE_SRCS)

pybind11_add_module(cnis SHARED ${CNIS_PYAPI_SRCS} ${CNIS_PYAPI_SAMPLE_SRCS})
message(STATUS "@@@@@@@@@@@ Target : cnis")

target_include_directories(cnis PRIVATE
                           ${CNIS_ROOT_DIR}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/
                           ${NEUWARE_INCLUDE_DIR}
                           ${OpenCV_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${GLOG_INCLUDE_DIRS}
                           ${CNIS_ROOT_DIR}/../include)
target_link_libraries(cnis PRIVATE easydk ${GLOG_LIBRARIES} ${OpenCV_LIBS} ${FFMPEG_LIBRARIES})

if(BUILD_TESTS)
  message(STATUS "@@@@@@@@@@@ Target : cnis_cpptest")
  aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/test CNIS_PYAPI_CPPTEST_SRCS)
  pybind11_add_module(cnis_cpptest MODULE ${CNIS_PYAPI_CPPTEST_SRCS})
  add_dependencies(cnis_cpptest cnis)
  target_include_directories(cnis_cpptest PRIVATE
                             ${CNIS_ROOT_DIR}/include
                             ${CMAKE_CURRENT_SOURCE_DIR}/src/
                             ${NEUWARE_INCLUDE_DIR}
                             ${OpenCV_INCLUDE_DIRS}
                             ${CMAKE_CURRENT_SOURCE_DIR}
                             ${GLOG_INCLUDE_DIRS}
                             ${CNIS_ROOT_DIR}/../include)
  target_link_libraries(cnis_cpptest PRIVATE cnis ${GLOG_LIBRARIES})
endif()