cmake_minimum_required(VERSION 3.5)
if(POLICY CMP0046)
  cmake_policy(SET CMP0046 NEW)
endif()
if(POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

set(EDK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

# compile flags
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -D_REENTRANT -fPIC -Wno-deprecated-declarations -Wall -Werror")

set(CMAKE_MODULE_PATH ${EDK_ROOT_DIR}/cmake)

# ---[ 3rdparty
set(3RDPARTY_LIBS "")

# ---[ neuware
if (PLATFORM MATCHES "MLU370" OR PLATFORM MATCHES "MLU590")
find_package(MLU)
  include_directories(${NEUWARE_INCLUDE_DIR})
elseif (PLATFORM MATCHES "CE3226")
  find_package(MPS)
  include_directories(${MPS_INCLUDE_DIR})
else()
  message(FATAL_ERROR "Unsupported PLATFORM: ${PLATFORM}")
endif()

# ---[ pybind11
set(PYBIND11_PYTHON_VERSION 3)
add_subdirectory(${EDK_ROOT_DIR}/3rdparty/pybind11 ${CMAKE_CURRENT_BINARY_DIR}/pybind11)

# ---[ glog
find_package(Glog)
include_directories(${GLOG_INCLUDE_DIRS})
list(APPEND 3RDPARTY_LIBS ${GLOG_LIBRARIES})

# ---[ opencv
set(OpenCV_FIND_QUIETLY true)
#find_package(OpenCV REQUIRED) //this will include opencv_ts which result in crash on centos
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if(OpenCV_FOUND)
  message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
  message(STATUS "OpenCV libraries: ${OpenCV_LIBS}")
  include_directories(${OpenCV_INCLUDE_DIRS})
  list(APPEND 3RDPARTY_LIBS ${OpenCV_LIBS})
else()
  message(FATAL_ERROR "opencv not found!")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../samples/cmake)
# ---[ FFmpeg
find_package(FFmpeg REQUIRED)
if (FFMPEG_FOUND)
  message(STATUS "FFmpeg Found, include: ${FFMPEG_INCLUDE_DIR}")
  message(STATUS "FFmpeg Found, libraries: ${FFMPEG_LIBRARIES}")
  include_directories(${FFMPEG_INCLUDE_DIR})
  list(APPEND 3RDPARTY_LIBS ${FFMPEG_LIBRARIES})
else (FFMPEG_FOUND)
  message(FATAL_ERROR "Can not find FFmpeg libs!")
endif (FFMPEG_FOUND)

set(LIBRARY_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/lib/)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src/ CNIS_PYAPI_SRCS)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/samples/ CNIS_PYAPI_SAMPLE_SRCS)

pybind11_add_module(cnis SHARED ${CNIS_PYAPI_SRCS} ${CNIS_PYAPI_SAMPLE_SRCS})
message(STATUS "@@@@@@@@@@@ Target : cnis")

target_include_directories(cnis PRIVATE
                           ${EDK_ROOT_DIR}/include
                           ${EDK_ROOT_DIR}/include/infer_server
                           ${CMAKE_CURRENT_SOURCE_DIR}/src/)
target_link_libraries(cnis PRIVATE easydk ${3RDPARTY_LIBS})

if (${CMAKE_PROJECT_NAME} MATCHES "easydk")
  if(BUILD_TESTS)
    message(STATUS "@@@@@@@@@@@ Target : cnis_cpptest")
    aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/test CNIS_PYAPI_CPPTEST_SRCS)
    pybind11_add_module(cnis_cpptest MODULE ${CNIS_PYAPI_CPPTEST_SRCS})
    add_dependencies(cnis_cpptest cnis)
    target_include_directories(cnis_cpptest PRIVATE
                              ${EDK_ROOT_DIR}/include
                              ${EDK_ROOT_DIR}/include/infer_server
                              ${CMAKE_CURRENT_SOURCE_DIR}/src/)
    target_link_libraries(cnis_cpptest PRIVATE cnis ${3RDPARTY_LIBS})
  endif()
endif()