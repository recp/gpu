cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(gpu_sample_tests LANGUAGES C)
  enable_testing()
  find_package(JPEG REQUIRED)
else()
  find_package(JPEG QUIET)
endif()

if(NOT JPEG_FOUND)
  return()
endif()

# test the actual private decoder without Linux window or graphics dependencies.
set(jpegSource "${CMAKE_CURRENT_SOURCE_DIR}/../../samples/common/linux.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${jpegSource}")
file(READ "${jpegSource}" linuxSource)
set(sectionStarts
  "typedef struct GPULinuxJPEGError"
  "static bool\nmultiply_size("
  "static void\njpeg_error_exit("
)
set(sectionEnds
  "struct GPULinuxSample {"
  "static const char*\nasset_name("
  "static uint8_t*\ndecode_image("
)
set(jpegSections)
foreach(section RANGE 0 2)
  list(GET sectionStarts ${section} start)
  list(GET sectionEnds ${section} end)
  string(FIND "${linuxSource}" "${start}" first)
  string(FIND "${linuxSource}" "${end}" last)
  if(first LESS 0 OR last LESS first)
    message(FATAL_ERROR "Cannot locate Linux JPEG decoder section ${section}")
  endif()
  math(EXPR length "${last} - ${first}")
  string(SUBSTRING "${linuxSource}" ${first} ${length} contents)
  string(APPEND jpegSections "${contents}\n")
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/linux_jpeg.inc" "${jpegSections}")

add_executable(gpu-linux-jpeg-test linux_jpeg.c)
target_include_directories(gpu-linux-jpeg-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(gpu-linux-jpeg-test PRIVATE JPEG::JPEG)
set_target_properties(gpu-linux-jpeg-test PROPERTIES
  C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
)
if(NOT MSVC)
  target_compile_options(gpu-linux-jpeg-test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME sample-linux-jpeg COMMAND gpu-linux-jpeg-test)
