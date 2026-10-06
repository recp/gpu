cmake_minimum_required(VERSION 3.20)
project(gpu_stdlib_artifacts LANGUAGES NONE)

set(GPU_USL_ROOT "${GPU_TEST_INPUTS}")
set(GPU_ASSETKIT_ROOT "${GPU_TEST_INPUTS}/unused-assetkit")
include("${GPU_SOURCE_DIR}/samples/cmake/stdlib.cmake")
include("${GPU_SOURCE_DIR}/samples/cmake/windows.cmake")

add_executable(gpu-usl-fixture IMPORTED)
set_target_properties(gpu-usl-fixture PROPERTIES
  IMPORTED_LOCATION "${GPU_TEST_FIXTURE}")

gpu_windows_gallery_artifact(stdlib
  "${GPU_TEST_INPUTS}/probe.usl" TRUE "" "" stdlibArtifact)
gpu_windows_gallery_artifact(plain
  "${GPU_TEST_INPUTS}/plain.usl" FALSE "" "" plainArtifact)
add_custom_target(artifacts ALL DEPENDS
  "${stdlibArtifact}" "${plainArtifact}")
