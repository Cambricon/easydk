# ---[ OpenCV
set(OpenCV_FIND_QUIETLY true)
find_package(OpenCV OPTIONAL_COMPONENTS imgcodecs)
set(imgcodecs_libs ${OpenCV_LIBS})
find_package(OpenCV REQUIRED core imgproc highgui features2d)
if(OpenCV_FOUND)
  message(STATUS "OpenCV Found")
else()
  message(FATAL_ERROR "Can not find OpenCV libs!")
endif()
if(imgcodecs_FOUND)
  list(APPEND OpenCV_LIBS ${imgcodecs_libs})
endif()

list(APPEND LINK_LIBS dl pthread gtest gtest_main ${OpenCV_LIBS} easydk cnis)

# ---[ api tests
file(GLOB apitest_srcs ${CMAKE_CURRENT_SOURCE_DIR}/api/*.cpp)
add_executable(apitest_cnis ${apitest_srcs})

add_sanitizers(apitest_cnis)

if (CNIS_WITH_CONTRIB)
  target_compile_definitions(apitest_cnis PRIVATE CNIS_WITH_CONTRIB)
endif()

target_include_directories(apitest_cnis PRIVATE
                           ${NEUWARE_HOME}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../contrib
                           ${CMAKE_CURRENT_SOURCE_DIR}/../src
                           ${CMAKE_CURRENT_SOURCE_DIR}/common
                           ${CMAKE_CURRENT_SOURCE_DIR}/api
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
                           ${NEUWARE_HOME}/include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR}/../src
                           ${CMAKE_CURRENT_SOURCE_DIR}/common
                           ${CMAKE_CURRENT_SOURCE_DIR}/unit
                           ${OpenCV_INCLUDE_DIRS})
target_link_libraries(unittest_cnis ${LINK_LIBS})
target_compile_options(unittest_cnis PRIVATE "-Wno-deprecated-declarations")