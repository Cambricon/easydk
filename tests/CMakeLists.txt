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

list(APPEND LINK_LIBS easydk  ${OpenCV_LIBS} pthread dl gtest gtest_main)
file(GLOB test_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

if (WITH_CNPLUGIN)
  file(GLOB test_plugin_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/*.cpp)
  list(APPEND test_srcs ${test_plugin_srcs})
endif()

add_executable(tests ${test_srcs})

if (SANITIZE_MEMORY OR SANITIZE_ADDRESS OR SANITIZE_THREAD OR SANITIZE_UNDEFINED)
    add_sanitizers(tests)
endif()

target_link_libraries(tests ${LINK_LIBS})
target_include_directories(tests PRIVATE
                           ${OpenCV_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(tests PRIVATE "-Wno-deprecated-declarations")
