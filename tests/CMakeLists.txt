# ---[ OpenCV
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

file(GLOB test_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
  list(APPEND test_srcs ${CMAKE_CURRENT_SOURCE_DIR}/src/resize/test_resize_yuv2yuv_invoke.cpp)
endif()

message(STATUS "@@@@@@@@@@@ Target : test_edk")
add_executable(tests_edk ${test_srcs})
add_sanitizers(tests_edk)

target_include_directories(tests_edk PRIVATE
                           ${NEUWARE_INCLUDE_DIR}
                           ${OpenCV_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/src
                           ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(tests_edk PRIVATE pthread dl gtest gtest_main easydk ${OpenCV_LIBS})
target_compile_options(tests_edk PRIVATE "-Wno-deprecated-declarations")
target_compile_definitions(tests_edk PRIVATE ${EDK_DEFINITIONS})

install(TARGETS tests_edk RUNTIME DESTINATION bin)
