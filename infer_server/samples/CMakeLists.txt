if (NOT CNIS_WITH_CONTRIB)
  message(FATAL_ERROR "CNIS sample depend on cnis/contrib content, enable CNIS_WITH_CONTRIB to build samples!")
endif()

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

# ---[ thread
list(APPEND Samples_LINK_LIBS pthread dl)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/bin/)

message(STATUS "@@@@@@@@@@@ Target : cnis_demo")
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} sample_srcs)

add_executable(cnis_demo ${sample_srcs})
add_sanitizers(cnis_demo)

target_include_directories(cnis_demo PRIVATE 
                           ${Samples_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}/../include
                           ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(cnis_demo cnis ${Samples_LINK_LIBS})

install(TARGETS cnis_demo RUNTIME DESTINATION bin)
