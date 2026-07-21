cmake_minimum_required(VERSION 3.20)

project(gpu VERSION 0.1.0 LANGUAGES C)

if(APPLE AND NOT EMSCRIPTEN)
  enable_language(OBJC)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(GPU_ENABLE_VULKAN "Build the Vulkan backend" OFF)
option(GPU_ENABLE_WEBGPU "Build the WebGPU backend" OFF)
option(GPU_BUILD_TESTS "Build GPU validation tests" OFF)
option(GPU_BUILD_SAMPLES "Build GPU samples" OFF)
option(GPU_BUILD_BENCHMARKS "Build GPU microbenchmarks" OFF)
option(GPU_BUILD_WITH_VALIDATION "Build validation and verbose diagnostics" ON)
option(GPU_BUILD_WITH_DEBUG_MARKERS "Build debug marker support" ON)
option(GPU_BACKEND_METAL_ONLY "Build only the Metal backend" OFF)
option(GPU_BACKEND_VULKAN_ONLY "Build only the Vulkan backend" OFF)
option(GPU_BACKEND_DX12_ONLY "Build only the Direct3D 12 backend" OFF)
option(GPU_BACKEND_WEBGPU_ONLY "Build only the WebGPU backend" OFF)

if(EMSCRIPTEN AND BUILD_SHARED_LIBS)
  message(STATUS "Building GPU statically for browser WebAssembly")
  set(BUILD_SHARED_LIBS OFF)
endif()

set(GPU_SINGLE_BACKEND_COUNT 0)
foreach(_gpu_backend_only
        GPU_BACKEND_METAL_ONLY
        GPU_BACKEND_VULKAN_ONLY
        GPU_BACKEND_DX12_ONLY
        GPU_BACKEND_WEBGPU_ONLY)
  if(${_gpu_backend_only})
    math(EXPR GPU_SINGLE_BACKEND_COUNT "${GPU_SINGLE_BACKEND_COUNT} + 1")
  endif()
endforeach()

if(GPU_SINGLE_BACKEND_COUNT GREATER 1)
  message(FATAL_ERROR "Select at most one GPU_BACKEND_*_ONLY option")
endif()
if(GPU_BACKEND_METAL_ONLY AND NOT APPLE)
  message(FATAL_ERROR "GPU_BACKEND_METAL_ONLY requires an Apple platform")
endif()
if(GPU_BACKEND_DX12_ONLY AND NOT WIN32)
  message(FATAL_ERROR "GPU_BACKEND_DX12_ONLY requires Windows")
endif()
if(GPU_BACKEND_VULKAN_ONLY)
  set(GPU_ENABLE_VULKAN ON)
endif()
if(GPU_BACKEND_WEBGPU_ONLY)
  set(GPU_ENABLE_WEBGPU ON)
endif()

set(GPU_BUILD_METAL OFF)
set(GPU_BUILD_VULKAN OFF)
set(GPU_BUILD_DX12 OFF)
set(GPU_BUILD_WEBGPU OFF)

if(GPU_BACKEND_METAL_ONLY)
  set(GPU_BUILD_METAL ON)
elseif(GPU_BACKEND_VULKAN_ONLY)
  set(GPU_BUILD_VULKAN ON)
elseif(GPU_BACKEND_DX12_ONLY)
  set(GPU_BUILD_DX12 ON)
elseif(GPU_BACKEND_WEBGPU_ONLY)
  set(GPU_BUILD_WEBGPU ON)
else()
  if(APPLE)
    set(GPU_BUILD_METAL ON)
  elseif(WIN32)
    set(GPU_BUILD_DX12 ON)
  endif()
  if(GPU_ENABLE_VULKAN)
    set(GPU_BUILD_VULKAN ON)
  endif()
  if(GPU_ENABLE_WEBGPU)
    set(GPU_BUILD_WEBGPU ON)
  endif()
endif()

set(GPU_CONFIG_NEEDS_VULKAN OFF)
if(NOT BUILD_SHARED_LIBS AND GPU_BUILD_VULKAN)
  set(GPU_CONFIG_NEEDS_VULKAN ON)
endif()

set(GPU_USL_ROOT "" CACHE PATH "UniversalShading/us checkout")
set(GPU_USL_INCLUDE_DIR "" CACHE PATH "USL public include directory")
set(GPU_USL_LIBRARY "" CACHE FILEPATH "Prebuilt USL library")

set(GPU_CORE_SOURCES
  src/init.c
  src/api/barrier.c
  src/api/buffer.c
  src/api/cmd/cmdbuf.c
  src/api/cmd/cmdque.c
  src/api/compute.c
  src/api/descr/descriptor.c
  src/api/device.c
  src/api/execution_graph.c
  src/api/sampler_feedback.c
  src/api/format.c
  src/api/frame.c
  src/api/instance.c
  src/api/library.c
  src/api/memory.c
  src/api/pass/renderpass.c
  src/api/pipeline_cache.c
  src/api/query.c
  src/api/ray.c
  src/api/render/pipeline.c
  src/api/render/rce.c
  src/api/sampler.c
  src/api/surface.c
  src/api/subgroup.c
  src/api/swapchain.c
  src/api/texture.c
  src/api/vertex.c
  src/api/vrs.c
  src/backend/backend.c
  src/backend/cache_file.c
)

add_library(gpu ${GPU_CORE_SOURCES})
add_library(gpu::gpu ALIAS gpu)

target_compile_definitions(gpu PRIVATE
  GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  GPU_BUILD_WITH_DEBUG_MARKERS=$<BOOL:${GPU_BUILD_WITH_DEBUG_MARKERS}>
  GPU_BACKEND_METAL_ONLY=$<BOOL:${GPU_BACKEND_METAL_ONLY}>
  GPU_BACKEND_VULKAN_ONLY=$<BOOL:${GPU_BACKEND_VULKAN_ONLY}>
  GPU_BACKEND_DX12_ONLY=$<BOOL:${GPU_BACKEND_DX12_ONLY}>
  GPU_BACKEND_WEBGPU_ONLY=$<BOOL:${GPU_BACKEND_WEBGPU_ONLY}>
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR EMSCRIPTEN)
  target_compile_definitions(gpu PRIVATE _POSIX_C_SOURCE=200809L)
endif()

if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(gpu PRIVATE Threads::Threads)
endif()

set_target_properties(gpu PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

if(APPLE)
  set_target_properties(gpu PROPERTIES
    OBJC_VISIBILITY_PRESET hidden
  )
endif()

target_include_directories(gpu
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

file(GLOB GPU_PUBLIC_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/include/gpu/*.h"
)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(gpu PRIVATE GPU_EXPORTS)
else()
  target_compile_definitions(gpu PUBLIC GPU_STATIC)
endif()

if(MSVC)
  target_compile_options(gpu PRIVATE /W3)
  target_compile_definitions(gpu PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
  )
else()
  target_compile_options(gpu PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
  )
endif()

if(NOT GPU_USL_ROOT)
  if(DEFINED ENV{USL_ROOT} AND EXISTS "$ENV{USL_ROOT}/us/CMakeLists.txt")
    set(GPU_USL_ROOT "$ENV{USL_ROOT}" CACHE PATH "UniversalShading/us checkout" FORCE)
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../UniversalShading/us/us/CMakeLists.txt")
    get_filename_component(
      _gpu_usl_root
      "${CMAKE_CURRENT_SOURCE_DIR}/../UniversalShading/us"
      ABSOLUTE
    )
    set(GPU_USL_ROOT "${_gpu_usl_root}" CACHE PATH "UniversalShading/us checkout" FORCE)
  endif()
endif()

if(TARGET us)
  target_link_libraries(gpu PRIVATE us)
elseif(GPU_USL_ROOT AND EXISTS "${GPU_USL_ROOT}/us/CMakeLists.txt")
  set(US_USE_TEST OFF CACHE BOOL "" FORCE)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EMSCRIPTEN)
    set(US_SHARED OFF CACHE BOOL "" FORCE)
    set(US_STATIC ON CACHE BOOL "" FORCE)
    set(DS_SHARED OFF CACHE BOOL "" FORCE)
    set(DS_STATIC ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory("${GPU_USL_ROOT}/us" "${CMAKE_BINARY_DIR}/_deps/us")
  target_link_libraries(gpu PRIVATE us)
elseif(GPU_USL_INCLUDE_DIR AND GPU_USL_LIBRARY)
  add_library(gpu_usl UNKNOWN IMPORTED)
  set_target_properties(gpu_usl PROPERTIES
    IMPORTED_LOCATION "${GPU_USL_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GPU_USL_INCLUDE_DIR}"
  )
  target_link_libraries(gpu PRIVATE gpu_usl)
else()
  message(FATAL_ERROR
    "USL not found. Set GPU_USL_ROOT or GPU_USL_INCLUDE_DIR and GPU_USL_LIBRARY."
  )
endif()

if(APPLE)
  function(gpu_link_apple_framework target name path)
    set(link_item "$<BUILD_INTERFACE:${path}>")
    string(APPEND link_item "$<INSTALL_INTERFACE:-framework ${name}>")
    target_link_libraries(${target} PRIVATE "${link_item}")
  endfunction()
endif()

if(GPU_BUILD_METAL)
  target_sources(gpu PRIVATE
    src/backend/mt/init.c
    src/backend/mt/impl/impl.c
    src/backend/mt/impl/instance.c
    src/backend/mt/impl/surface.c
    src/backend/mt/impl/buffer.m
    src/backend/mt/impl/cmd/cmdbuf.m
    src/backend/mt/impl/cmd/cmdque.m
    src/backend/mt/impl/compute.m
    src/backend/mt/impl/depthstencil.m
    src/backend/mt/impl/device.m
    src/backend/mt/impl/frame.m
    src/backend/mt/impl/format.m
    src/backend/mt/impl/library.m
    src/backend/mt/impl/memory.m
    src/backend/mt/impl/pipeline_cache.m
    src/backend/mt/impl/ray.m
    src/backend/mt/impl/render/pipeline.m
    src/backend/mt/impl/render/rce.m
    src/backend/mt/impl/renderpass.m
    src/backend/mt/impl/swapchain.m
    src/backend/mt/impl/texture_view_pool.m
    src/backend/mt/impl/vertex.m
    src/backend/mt/impl/vrs.m
  )

  find_library(GPU_FRAMEWORK_METAL Metal REQUIRED)
  find_library(GPU_FRAMEWORK_METALKIT MetalKit REQUIRED)
  gpu_link_apple_framework(gpu Metal "${GPU_FRAMEWORK_METAL}")
  gpu_link_apple_framework(gpu MetalKit "${GPU_FRAMEWORK_METALKIT}")
endif()

if(APPLE AND (GPU_BUILD_METAL OR GPU_BUILD_VULKAN))
  find_library(GPU_FRAMEWORK_FOUNDATION Foundation REQUIRED)
  find_library(GPU_FRAMEWORK_QUARTZCORE QuartzCore REQUIRED)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_library(GPU_FRAMEWORK_UI UIKit REQUIRED)
    set(GPU_FRAMEWORK_UI_NAME UIKit)
  else()
    find_library(GPU_FRAMEWORK_UI AppKit REQUIRED)
    set(GPU_FRAMEWORK_UI_NAME AppKit)
  endif()

  gpu_link_apple_framework(gpu Foundation "${GPU_FRAMEWORK_FOUNDATION}")
  gpu_link_apple_framework(gpu QuartzCore "${GPU_FRAMEWORK_QUARTZCORE}")
  gpu_link_apple_framework(gpu
                           "${GPU_FRAMEWORK_UI_NAME}"
                           "${GPU_FRAMEWORK_UI}")
endif()

if(GPU_BUILD_DX12)
  target_sources(gpu PRIVATE
    src/backend/dx12/init.c
    src/backend/dx12/impl/buffer.c
    src/backend/dx12/impl/cmdque.c
    src/backend/dx12/impl/compute.c
    src/backend/dx12/impl/descr/descpool.c
    src/backend/dx12/impl/device.c
    src/backend/dx12/impl/execution_graph.c
    src/backend/dx12/impl/format.c
    src/backend/dx12/impl/frame.c
    src/backend/dx12/impl/instance.c
    src/backend/dx12/impl/library.c
    src/backend/dx12/impl/memory.c
    src/backend/dx12/impl/pipeline.c
    src/backend/dx12/impl/pipeline_cache.c
    src/backend/dx12/impl/query.c
    src/backend/dx12/impl/ray.c
    src/backend/dx12/impl/renderpass.c
    src/backend/dx12/impl/rce.c
    src/backend/dx12/impl/sampler.c
    src/backend/dx12/impl/sampler_feedback.c
    src/backend/dx12/impl/surface.c
    src/backend/dx12/impl/swapchain.c
    src/backend/dx12/impl/texture.c
    src/backend/dx12/impl/vrs.c
  )
  target_link_libraries(gpu PRIVATE d3d12 d3dcompiler dxgi dxguid)
endif()

if(GPU_BUILD_VULKAN)
  find_package(Vulkan REQUIRED)
  get_filename_component(GPU_VULKAN_LIBRARY_DIR "${Vulkan_LIBRARY}" DIRECTORY)
  if(APPLE)
    get_filename_component(GPU_VULKAN_PLATFORM_DIR
                           "${GPU_VULKAN_LIBRARY_DIR}"
                           DIRECTORY)
    set(GPU_VULKAN_ICD
      "${GPU_VULKAN_PLATFORM_DIR}/share/vulkan/icd.d/MoltenVK_icd.json"
    )
  endif()
  target_compile_definitions(gpu PRIVATE GPU_ENABLE_VULKAN=1)
  target_sources(gpu PRIVATE
    src/backend/vk/init.c
    src/backend/vk/impl/buffer.c
    src/backend/vk/impl/cmdque.c
    src/backend/vk/impl/compute.c
    src/backend/vk/impl/descriptor.c
    src/backend/vk/impl/device.c
    src/backend/vk/impl/execution_graph.c
    src/backend/vk/impl/display.c
    src/backend/vk/impl/format.c
    src/backend/vk/impl/frame.c
    src/backend/vk/impl/instance.c
    src/backend/vk/impl/library.c
    src/backend/vk/impl/memory.c
    src/backend/vk/impl/pipeline.c
    src/backend/vk/impl/pipeline_cache.c
    src/backend/vk/impl/query.c
    src/backend/vk/impl/ray.c
    src/backend/vk/impl/rce.c
    src/backend/vk/impl/renderpass.c
    src/backend/vk/impl/sampler.c
    src/backend/vk/impl/sync.c
    src/backend/vk/impl/surface.c
    src/backend/vk/impl/swapchain.c
    src/backend/vk/impl/texture.c
    src/backend/vk/impl/vrs.c
  )
  if(GPU_BUILD_WITH_VALIDATION)
    target_sources(gpu PRIVATE src/backend/vk/impl/debug.c)
  endif()
  if(APPLE)
    target_sources(gpu PRIVATE src/backend/vk/impl/surface_apple.m)
  endif()
  target_link_libraries(gpu PRIVATE Vulkan::Vulkan)
endif()

if(GPU_BUILD_WEBGPU)
  target_compile_definitions(gpu PRIVATE GPU_ENABLE_WEBGPU=1)
  target_sources(gpu PRIVATE
    src/backend/webgpu/init.c
    src/backend/webgpu/impl/device.c
    src/backend/webgpu/impl/instance.c
    src/backend/webgpu/impl/surface.c
    src/backend/webgpu/impl/swapchain.c
    src/backend/webgpu/impl/frame.c
    src/backend/webgpu/impl/cmdque.c
    src/backend/webgpu/impl/buffer.c
    src/backend/webgpu/impl/texture.c
    src/backend/webgpu/impl/sampler.c
    src/backend/webgpu/impl/library.c
    src/backend/webgpu/impl/descriptor.c
    src/backend/webgpu/impl/pipeline.c
    src/backend/webgpu/impl/compute.c
    src/backend/webgpu/impl/query.c
    src/backend/webgpu/impl/renderpass.c
    src/backend/webgpu/impl/rce.c
  )

  if(EMSCRIPTEN)
    target_compile_options(gpu PRIVATE --use-port=emdawnwebgpu)
    target_link_options(gpu PRIVATE --use-port=emdawnwebgpu)
  else()
    set(GPU_WEBGPU_INCLUDE_DIR "" CACHE PATH
        "Directory containing webgpu/webgpu.h")
    set(GPU_WEBGPU_LIBRARY "" CACHE FILEPATH
        "Native Dawn or wgpu-native WebGPU library")
    if(NOT GPU_WEBGPU_INCLUDE_DIR)
      find_path(GPU_WEBGPU_INCLUDE_DIR webgpu/webgpu.h)
    endif()
    if(NOT GPU_WEBGPU_LIBRARY)
      find_library(GPU_WEBGPU_LIBRARY NAMES webgpu_dawn wgpu_native webgpu)
    endif()
    if(NOT GPU_WEBGPU_INCLUDE_DIR OR NOT GPU_WEBGPU_LIBRARY)
      message(FATAL_ERROR
        "GPU_ENABLE_WEBGPU requires webgpu/webgpu.h and a native WebGPU "
        "provider. Set GPU_WEBGPU_INCLUDE_DIR and GPU_WEBGPU_LIBRARY.")
    endif()
    target_include_directories(gpu PRIVATE "${GPU_WEBGPU_INCLUDE_DIR}")
    target_link_libraries(gpu PRIVATE "${GPU_WEBGPU_LIBRARY}")
  endif()
endif()

if((GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES OR GPU_BUILD_BENCHMARKS) AND
   NOT EMSCRIPTEN)
  add_executable(gpu-usl-fixture tests/usl_fixture.c)
  if(TARGET us)
    target_link_libraries(gpu-usl-fixture PRIVATE us)
  elseif(TARGET gpu_usl)
    target_link_libraries(gpu-usl-fixture PRIVATE gpu_usl)
  endif()
  set_target_properties(gpu-usl-fixture PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  if(GPU_USL_ROOT AND EXISTS "${GPU_USL_ROOT}/tools/uslpack.c")
    add_executable(gpu-uslpack "${GPU_USL_ROOT}/tools/uslpack.c")
    target_include_directories(gpu-uslpack PRIVATE "${GPU_USL_ROOT}/us/include")
    if(TARGET us)
      target_link_libraries(gpu-uslpack PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-uslpack PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-uslpack PROPERTIES
      OUTPUT_NAME uslpack
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
  endif()

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-usl-fixture POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-usl-fixture>
      )
    endif()
  endforeach()

  function(gpu_add_usl_fixtures outVar backend group)
    set(runtimeCommands)
    set(runtimeDependencies)
    set(targetCapsEnvironment)
    if(DEFINED GPU_USL_FIXTURE_TARGET_CAPS)
      list(APPEND targetCapsEnvironment
        "USL_TARGET_CAPS=${GPU_USL_FIXTURE_TARGET_CAPS}"
      )
    endif()
    foreach(runtimeTarget us ds)
      if(TARGET ${runtimeTarget})
        list(APPEND runtimeCommands
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtimeTarget}>
                  $<TARGET_FILE_DIR:gpu-usl-fixture>
        )
        list(APPEND runtimeDependencies $<TARGET_FILE:${runtimeTarget}>)
      endif()
    endforeach()

    set(outputs)
    foreach(source IN ITEMS ${ARGN})
      get_filename_component(name "${source}" NAME_WE)
      set(outputDir "${CMAKE_CURRENT_BINARY_DIR}/usl/${backend}/${group}")
      set(fixtureSource "${outputDir}/${name}.usl")
      set(fixtureOutput "${outputDir}/${name}.us")
      add_custom_command(
        OUTPUT "${fixtureOutput}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${outputDir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source}"
                "${fixtureSource}"
        ${runtimeCommands}
        COMMAND ${CMAKE_COMMAND} -E env
                USL_EMIT_BYTECODE=1
                USL_NO_BACKEND_SIDECAR=1
                ${targetCapsEnvironment}
                $<TARGET_FILE:gpu-usl-fixture>
                "${backend}"
                "${fixtureSource}"
        DEPENDS gpu-usl-fixture "${source}" ${runtimeDependencies}
        VERBATIM
      )
      list(APPEND outputs "${fixtureOutput}")
    endforeach()
    set(${outVar} "${outputs}" PARENT_SCOPE)
  endfunction()

  function(gpu_attach_usl_sample target fixtureTarget)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/samples/${target}"
    )
    set(artifactTarget "${target}-artifacts")
    set(artifactStamp
      "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${artifactTarget}.stamp"
    )
    set(artifactDependencies ${ARGN})
    set(artifactCommands
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:${target}>
    )
    if(WIN32)
      foreach(runtimeTarget gpu us ds)
        if(TARGET ${runtimeTarget})
          list(APPEND artifactDependencies $<TARGET_FILE:${runtimeTarget}>)
          list(APPEND artifactCommands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${runtimeTarget}>
                    $<TARGET_FILE_DIR:${target}>
          )
        endif()
      endforeach()
    endif()
    foreach(artifact IN ITEMS ${ARGN})
      list(APPEND artifactCommands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${artifact}"
                $<TARGET_FILE_DIR:${target}>
      )
    endforeach()
    list(APPEND artifactCommands
      COMMAND ${CMAKE_COMMAND} -E touch "${artifactStamp}"
    )
    add_custom_command(
      OUTPUT "${artifactStamp}"
      ${artifactCommands}
      DEPENDS ${artifactDependencies}
      VERBATIM
    )
    add_custom_target(${artifactTarget} DEPENDS "${artifactStamp}")
    add_dependencies(${artifactTarget} ${fixtureTarget})
    add_dependencies(${target} ${artifactTarget})
    if(WIN32)
      foreach(runtimeTarget gpu us ds)
        if(TARGET ${runtimeTarget})
          add_dependencies(${artifactTarget} ${runtimeTarget})
        endif()
      endforeach()
    endif()
  endfunction()
endif()

if(GPU_BUILD_SAMPLES AND
   ((GPU_BUILD_WEBGPU AND EMSCRIPTEN) OR
    (WIN32 AND GPU_BUILD_DX12) OR
    (APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS" AND
     (GPU_BUILD_METAL OR GPU_BUILD_VULKAN))))
  find_path(GPU_CGLM_INCLUDE_DIR
    NAMES cglm/cglm.h
    HINTS
      "${CMAKE_CURRENT_SOURCE_DIR}/../glm/include"
      /opt/homebrew/include
  )
  if(NOT GPU_CGLM_INCLUDE_DIR)
    message(FATAL_ERROR
      "The textured cube samples require cglm headers. Set "
      "GPU_CGLM_INCLUDE_DIR.")
  endif()
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_WEBGPU AND EMSCRIPTEN)
  set(GPU_USL_HOST_FIXTURE "" CACHE FILEPATH
      "Host gpu-usl-fixture executable used for WebGPU samples")
  if(NOT GPU_USL_HOST_FIXTURE)
    set(_gpu_usl_host_fixture
        "${CMAKE_CURRENT_SOURCE_DIR}/out/build/release-check/gpu-usl-fixture")
    if(EXISTS "${_gpu_usl_host_fixture}")
      set(GPU_USL_HOST_FIXTURE "${_gpu_usl_host_fixture}")
    endif()
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_FIXTURE}")
    message(FATAL_ERROR
      "WebGPU samples require a host gpu-usl-fixture executable. Set "
      "GPU_USL_HOST_FIXTURE to a native build of that target.")
  endif()

  set(GPU_WEBGPU_GALLERY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/samples/webgpu")
  set(GPU_WEBGPU_GALLERY_SOURCE_DIR
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/webgpu-gallery")
  set(GPU_WEBGPU_SAMPLE_COMMON_SOURCE
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/common/webgpu.c")
  file(MAKE_DIRECTORY
       "${GPU_WEBGPU_GALLERY_DIR}"
       "${GPU_WEBGPU_GALLERY_DIR}/sources")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/index.html"
    "${GPU_WEBGPU_GALLERY_DIR}/index.html"
    COPYONLY
  )
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/gallery.css"
    "${GPU_WEBGPU_GALLERY_DIR}/gallery.css"
    COPYONLY
  )
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample.js"
    "${GPU_WEBGPU_GALLERY_DIR}/sample.js"
    COPYONLY
  )

  set(GPU_WEBGPU_TRIANGLE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TRIANGLE_SOURCE
      "${GPU_WEBGPU_TRIANGLE_DIR}/triangle.usl")
  set(GPU_WEBGPU_TRIANGLE_US
      "${GPU_WEBGPU_TRIANGLE_DIR}/triangle.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "First pixel")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / vertex-generated")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "A bufferless triangle compiled from USL and drawn through the portable render path.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/triangle.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/triangle.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/triangle.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-triangle-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TRIANGLE_US}"
    BYPRODUCTS "${GPU_WEBGPU_TRIANGLE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_TRIANGLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-webgpu-usl/triangle.usl"
            "${GPU_WEBGPU_TRIANGLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TRIANGLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/triangle.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TRIANGLE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/triangle.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TRIANGLE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/triangle.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-webgpu-usl/triangle.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-triangle-artifact
    DEPENDS "${GPU_WEBGPU_TRIANGLE_US}"
  )

  add_executable(gpu-triangle-webgpu-usl
    samples/triangle-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-triangle-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-triangle-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TRIANGLE_US}@/triangle.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-triangle-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-triangle-webgpu-usl gpu-webgpu-triangle-artifact)
  set_target_properties(gpu-triangle-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-triangle-webgpu-usl-shell.html;${GPU_WEBGPU_TRIANGLE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_PUSH_CONSTANTS_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_PUSH_CONSTANTS_SOURCE
      "${GPU_WEBGPU_PUSH_CONSTANTS_DIR}/push_constants.usl")
  set(GPU_WEBGPU_PUSH_CONSTANTS_US
      "${GPU_WEBGPU_PUSH_CONSTANTS_DIR}/push_constants.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Push constants")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / dynamic constants")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Per-draw constants lowered to a native fast path or a WebGPU dynamic uniform slice.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/push-constants.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/push-constants.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/push-constants.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-push-constants-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_PUSH_CONSTANTS_US}"
    BYPRODUCTS "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_PUSH_CONSTANTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/push-constants-webgpu-usl/push_constants.usl"
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/push-constants-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/push-constants.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/push-constants.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/push-constants.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/push-constants-webgpu-usl/push_constants.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/push-constants-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-push-constants-artifact
    DEPENDS "${GPU_WEBGPU_PUSH_CONSTANTS_US}"
  )

  add_executable(gpu-push-constants-webgpu-usl
    samples/push-constants-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-push-constants-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-push-constants-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_PUSH_CONSTANTS_US}@/push_constants.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-push-constants-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-push-constants-webgpu-usl
                   gpu-webgpu-push-constants-artifact)
  set_target_properties(gpu-push-constants-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-push-constants-webgpu-usl-shell.html;${GPU_WEBGPU_PUSH_CONSTANTS_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TEXTURED_QUAD_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TEXTURED_QUAD_SOURCE
      "${GPU_WEBGPU_TEXTURED_QUAD_DIR}/textured_quad.usl")
  set(GPU_WEBGPU_TEXTURED_QUAD_US
      "${GPU_WEBGPU_TEXTURED_QUAD_DIR}/textured_quad.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Texture path")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / texture + sampler")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "A copied checkerboard validating transfer, USL reflection, typed texture layouts, and bind groups.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/textured-quad.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/textured-quad.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/textured-quad.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-quad-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TEXTURED_QUAD_US}"
    BYPRODUCTS "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TEXTURED_QUAD_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-webgpu-usl/textured_quad.usl"
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-quad.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-quad.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-quad.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-webgpu-usl/textured_quad.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-textured-quad-artifact
    DEPENDS "${GPU_WEBGPU_TEXTURED_QUAD_US}"
  )

  add_executable(gpu-textured-quad-webgpu-usl
    samples/textured-quad-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-textured-quad-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-textured-quad-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TEXTURED_QUAD_US}@/textured_quad.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-quad-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-textured-quad-webgpu-usl
                   gpu-webgpu-textured-quad-artifact)
  set_target_properties(gpu-textured-quad-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-quad-webgpu-usl-shell.html;${GPU_WEBGPU_TEXTURED_QUAD_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_COMPUTE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_COMPUTE_SOURCE
      "${GPU_WEBGPU_COMPUTE_DIR}/compute.usl")
  set(GPU_WEBGPU_COMPUTE_US
      "${GPU_WEBGPU_COMPUTE_DIR}/compute.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Compute handoff")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute / storage buffer")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Compute uses per-dispatch constants to write the vertex stream; render consumes it without readback.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/compute.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/compute.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/compute.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-compute-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_COMPUTE_US}"
    BYPRODUCTS "${GPU_WEBGPU_COMPUTE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_COMPUTE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-webgpu-usl/compute.usl"
            "${GPU_WEBGPU_COMPUTE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_COMPUTE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/compute.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_COMPUTE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/compute.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_COMPUTE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/compute.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-webgpu-usl/compute.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-compute-artifact
    DEPENDS "${GPU_WEBGPU_COMPUTE_US}"
  )

  add_executable(gpu-compute-webgpu-usl
    samples/compute-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-compute-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-compute-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_COMPUTE_US}@/compute.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-compute-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-compute-webgpu-usl gpu-webgpu-compute-artifact)
  set_target_properties(gpu-compute-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-compute-webgpu-usl-shell.html;${GPU_WEBGPU_COMPUTE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_STORAGE_TEXTURE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_STORAGE_TEXTURE_SOURCE
      "${GPU_WEBGPU_STORAGE_TEXTURE_DIR}/storage_texture.usl")
  set(GPU_WEBGPU_STORAGE_TEXTURE_US
      "${GPU_WEBGPU_STORAGE_TEXTURE_DIR}/storage_texture.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Storage texture")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute / typed storage texture")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Compute writes a typed RGBA8 texture; render samples it in the same command buffer.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/storage-texture.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/storage-texture.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/storage-texture.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-storage-texture-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_STORAGE_TEXTURE_US}"
    BYPRODUCTS "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_STORAGE_TEXTURE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/storage-texture-webgpu-usl/storage_texture.usl"
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/storage-texture-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/storage-texture.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/storage-texture.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/storage-texture.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/storage-texture-webgpu-usl/storage_texture.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/storage-texture-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-storage-texture-artifact
    DEPENDS "${GPU_WEBGPU_STORAGE_TEXTURE_US}"
  )

  add_executable(gpu-storage-texture-webgpu-usl
    samples/storage-texture-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-storage-texture-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-storage-texture-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_STORAGE_TEXTURE_US}@/storage_texture.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-storage-texture-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-storage-texture-webgpu-usl
                   gpu-webgpu-storage-texture-artifact)
  set_target_properties(gpu-storage-texture-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-storage-texture-webgpu-usl-shell.html;${GPU_WEBGPU_STORAGE_TEXTURE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_INDEXED_DEPTH_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_INDEXED_DEPTH_SOURCE
      "${GPU_WEBGPU_INDEXED_DEPTH_DIR}/indexed_depth.usl")
  set(GPU_WEBGPU_INDEXED_DEPTH_US
      "${GPU_WEBGPU_INDEXED_DEPTH_DIR}/indexed_depth.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Indexed depth")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / index + depth")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "One indexed draw validates the depth texture, pipeline, attachment, and occlusion path.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/indexed-depth.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/indexed-depth.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/indexed-depth.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-indexed-depth-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_INDEXED_DEPTH_US}"
    BYPRODUCTS "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_INDEXED_DEPTH_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/indexed-depth-webgpu-usl/indexed_depth.usl"
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/indexed-depth-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/indexed-depth.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/indexed-depth.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/indexed-depth.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/indexed-depth-webgpu-usl/indexed_depth.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/indexed-depth-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-indexed-depth-artifact
    DEPENDS "${GPU_WEBGPU_INDEXED_DEPTH_US}"
  )

  add_executable(gpu-indexed-depth-webgpu-usl
    samples/indexed-depth-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-indexed-depth-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-indexed-depth-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_INDEXED_DEPTH_US}@/indexed_depth.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-indexed-depth-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-indexed-depth-webgpu-usl
                   gpu-webgpu-indexed-depth-artifact)
  set_target_properties(gpu-indexed-depth-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-indexed-depth-webgpu-usl-shell.html;${GPU_WEBGPU_INDEXED_DEPTH_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_INSTANCING_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_INSTANCING_SOURCE
      "${GPU_WEBGPU_INSTANCING_DIR}/instancing.usl")
  set(GPU_WEBGPU_INSTANCING_US
      "${GPU_WEBGPU_INSTANCING_DIR}/instancing.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Instancing")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / instances + offsets")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Two dynamic uniform slices drive eight instances without rebuilding the bind group.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/instancing.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/instancing.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/instancing.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-instancing-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_INSTANCING_US}"
    BYPRODUCTS "${GPU_WEBGPU_INSTANCING_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_INSTANCING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/instancing-webgpu-usl/instancing.usl"
            "${GPU_WEBGPU_INSTANCING_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_INSTANCING_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/instancing-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/instancing.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INSTANCING_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/instancing.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INSTANCING_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/instancing.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/instancing-webgpu-usl/instancing.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/instancing-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-instancing-artifact
    DEPENDS "${GPU_WEBGPU_INSTANCING_US}"
  )

  add_executable(gpu-instancing-webgpu-usl
    samples/instancing-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-instancing-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-instancing-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_INSTANCING_US}@/instancing.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-instancing-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-instancing-webgpu-usl gpu-webgpu-instancing-artifact)
  set_target_properties(gpu-instancing-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-instancing-webgpu-usl-shell.html;${GPU_WEBGPU_INSTANCING_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TEXTURED_CUBE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TEXTURED_CUBE_SOURCE
      "${GPU_WEBGPU_TEXTURED_CUBE_DIR}/textured_cube.usl")
  set(GPU_WEBGPU_TEXTURED_CUBE_US
      "${GPU_WEBGPU_TEXTURED_CUBE_DIR}/textured_cube.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Rotating cube")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / cglm + texture")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "<a class=\"project-link\" href=\"https://github.com/recp/cglm\" target=\"_blank\" rel=\"noreferrer\">cglm</a> drives a rotating model-view-projection matrix through an indexed, depth-tested textured draw.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/textured-cube.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/textured-cube.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/textured-cube.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-cube-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TEXTURED_CUBE_US}"
    BYPRODUCTS "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TEXTURED_CUBE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-cube-usl/textured_cube.usl"
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-cube-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-cube.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-cube.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-cube.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-cube-usl/textured_cube.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-cube-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-textured-cube-artifact
    DEPENDS "${GPU_WEBGPU_TEXTURED_CUBE_US}"
  )

  add_executable(gpu-textured-cube-webgpu-usl
    samples/textured-cube-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_include_directories(gpu-textured-cube-webgpu-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  target_link_libraries(gpu-textured-cube-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-textured-cube-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TEXTURED_CUBE_US}@/textured_cube.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-cube-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-textured-cube-webgpu-usl
                   gpu-webgpu-textured-cube-artifact)
  set_target_properties(gpu-textured-cube-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-cube-webgpu-usl-shell.html;${GPU_WEBGPU_TEXTURED_CUBE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_MSAA_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_MSAA_SOURCE
      "${GPU_WEBGPU_MSAA_DIR}/msaa.usl")
  set(GPU_WEBGPU_MSAA_US
      "${GPU_WEBGPU_MSAA_DIR}/msaa.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "MSAA resolve")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / 4x MSAA")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Four-sample rasterization resolves directly into the swapchain and discards the multisample attachment contents.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/msaa.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/msaa.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/msaa.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_MSAA_US}"
    BYPRODUCTS "${GPU_WEBGPU_MSAA_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_MSAA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/msaa-webgpu-usl/msaa.usl"
            "${GPU_WEBGPU_MSAA_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_MSAA_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/msaa-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MSAA_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MSAA_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/msaa-webgpu-usl/msaa.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/msaa-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-msaa-artifact
    DEPENDS "${GPU_WEBGPU_MSAA_US}"
  )

  add_executable(gpu-msaa-webgpu-usl
    samples/msaa-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-msaa-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-msaa-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_MSAA_US}@/msaa.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-msaa-webgpu-usl gpu-webgpu-msaa-artifact)
  set_target_properties(gpu-msaa-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-webgpu-usl-shell.html;${GPU_WEBGPU_MSAA_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_MRT_BLEND_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_MRT_BLEND_SOURCE
      "${GPU_WEBGPU_MRT_BLEND_DIR}/mrt_blend.usl")
  set(GPU_WEBGPU_MRT_BLEND_US
      "${GPU_WEBGPU_MRT_BLEND_DIR}/mrt_blend.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "MRT blend")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / MRT + blending")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Two offscreen color targets use independent alpha and additive blend state, then composite into the swapchain.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/mrt-blend.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/mrt-blend.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/mrt-blend.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-mrt-blend-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_MRT_BLEND_US}"
    BYPRODUCTS "${GPU_WEBGPU_MRT_BLEND_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_MRT_BLEND_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/mrt-blend-webgpu-usl/mrt_blend.usl"
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/mrt-blend-webgpu-usl/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/mrt-blend.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/mrt-blend.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/mrt-blend.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/mrt-blend-webgpu-usl/mrt_blend.usl"
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/mrt-blend-webgpu-usl/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-mrt-blend-artifact
    DEPENDS "${GPU_WEBGPU_MRT_BLEND_US}"
  )

  add_executable(gpu-mrt-blend-webgpu-usl
    samples/mrt-blend-webgpu-usl/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-mrt-blend-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-mrt-blend-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_MRT_BLEND_US}@/mrt_blend.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-mrt-blend-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-mrt-blend-webgpu-usl
                   gpu-webgpu-mrt-blend-artifact)
  set_target_properties(gpu-mrt-blend-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-mrt-blend-webgpu-usl-shell.html;${GPU_WEBGPU_MRT_BLEND_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )
endif()

if(GPU_BUILD_TESTS)
  get_property(GPU_GENERATOR_IS_MULTI_CONFIG GLOBAL
               PROPERTY GENERATOR_IS_MULTI_CONFIG)

  set(GPU_API_USL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/usl-reflection-check/reflection.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/render_mrt.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/compute.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/source_sampler.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/storage_texture.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/cube_texture.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/line_texture.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/volume_texture.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/descriptor_arrays.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/coordinate.usl"
  )
  if(GPU_BUILD_METAL)
    set(GPU_API_DEFAULT_BACKEND metal)
  elseif(GPU_BUILD_DX12)
    set(GPU_API_DEFAULT_BACKEND dx12)
  else()
    set(GPU_API_DEFAULT_BACKEND vulkan)
  endif()
  gpu_add_usl_fixtures(
    GPU_API_USL_FIXTURES
    "${GPU_API_DEFAULT_BACKEND}"
    api
    ${GPU_API_USL_SOURCES}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_API_DESCRIPTOR_INDEXING_USL_FIXTURE
    "${GPU_API_DEFAULT_BACKEND}"
    api
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/descriptor_indexing.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES
    ${GPU_API_DESCRIPTOR_INDEXING_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS subgroup)
  gpu_add_usl_fixtures(
    GPU_API_SUBGROUP_USL_FIXTURE
    vulkan
    api
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/subgroup.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES ${GPU_API_SUBGROUP_USL_FIXTURE})
  set(GPU_API_SUBGROUP_MATRIX_USL_SOURCE
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/subgroup_matrix.usl")
  set(GPU_API_SUBGROUP_MATRIX_PROFILE "8x8x8")
  set(GPU_USL_FIXTURE_TARGET_CAPS subgroup_matrix)
  if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan")
    set(GPU_API_SUBGROUP_MATRIX_USL_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/subgroup_matrix_vulkan.usl")
    set(GPU_API_SUBGROUP_MATRIX_PROFILE "16x8x8")
  elseif(GPU_API_DEFAULT_BACKEND STREQUAL "dx12")
    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_10,subgroup_matrix)
  endif()
  gpu_add_usl_fixtures(
    GPU_API_SUBGROUP_MATRIX_USL_FIXTURE
    "${GPU_API_DEFAULT_BACKEND}"
    api-subgroup-matrix
    "${GPU_API_SUBGROUP_MATRIX_USL_SOURCE}"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-subgroup-matrix-fixture
    DEPENDS ${GPU_API_SUBGROUP_MATRIX_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,shader_f16)
  gpu_add_usl_fixtures(
    GPU_API_SHADER_F16_USL_FIXTURE
    vulkan
    api
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/shader_f16.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES ${GPU_API_SHADER_F16_USL_FIXTURE})
  set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_6,atomic64)
  gpu_add_usl_fixtures(
    GPU_API_ATOMIC64_USL_FIXTURE
    dx12
    api-atomic64
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/atomic64.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-atomic64-fixture
    DEPENDS ${GPU_API_ATOMIC64_USL_FIXTURE}
  )
  add_custom_target(gpu-api-fixtures DEPENDS ${GPU_API_USL_FIXTURES})

  if(GPU_BUILD_METAL OR GPU_BUILD_DX12)
    if(GPU_BUILD_METAL)
      set(GPU_API_RAY_QUERY_BACKEND metal)
    else()
      set(GPU_API_RAY_QUERY_BACKEND dx12)
    endif()
    set(GPU_USL_FIXTURE_TARGET_CAPS ray_query)
    gpu_add_usl_fixtures(
      GPU_API_RAY_QUERY_USL_FIXTURE
      "${GPU_API_RAY_QUERY_BACKEND}"
      api
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/ray_query.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    unset(GPU_API_RAY_QUERY_BACKEND)
    add_custom_target(
      gpu-api-ray-query-fixture
      DEPENDS ${GPU_API_RAY_QUERY_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_DX12)
    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_3,ray_tracing_pipeline)
    gpu_add_usl_fixtures(
      GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE
      dx12
      api-ray-pipeline
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/ray_pipeline.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-dx12-ray-pipeline-fixture
      DEPENDS ${GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_8,execution_graph)
    gpu_add_usl_fixtures(
      GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE
      dx12
      api-execution-graph
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/execution_graph.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-dx12-execution-graph-fixture
      DEPENDS ${GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    gpu_add_usl_fixtures(
      GPU_METAL_MESH_TEST_USL_FIXTURE
      metal
      mesh-test
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/mesh-triangle-usl/mesh_triangle.usl"
    )
    list(GET GPU_METAL_MESH_TEST_USL_FIXTURE 0 GPU_METAL_MESH_TEST_US)
    add_custom_target(
      gpu-metal-mesh-test-fixture
      DEPENDS ${GPU_METAL_MESH_TEST_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_VULKAN)
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,shader_subgroup_clock)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/shader_subgroup_clock.usl"
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,shader_device_clock)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/shader_device_clock.usl"
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,compute_derivatives_quads)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/compute_derivatives_quads.usl"
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,compute_derivatives_linear)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/compute_derivatives_linear.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    set(GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES
      ${GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE}
      ${GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE}
      ${GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE}
      ${GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE}
    )
    add_custom_target(
      gpu-api-vulkan-clock-derivative-fixtures
      DEPENDS ${GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES}
    )

    gpu_add_usl_fixtures(
      GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE
      vulkan
      api-untyped-pointer
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/untyped_pointer.usl"
    )
    add_custom_target(
      gpu-api-vulkan-untyped-pointer-fixture
      DEPENDS ${GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS ray_query)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE
      vulkan
      api-ray-query
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/ray_query.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-vulkan-ray-query-fixture
      DEPENDS ${GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,ray_tracing_pipeline)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE
      vulkan
      api-ray-pipeline
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/ray_pipeline.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-vulkan-ray-pipeline-fixture
      DEPENDS ${GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_3,execution_graph)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE
      vulkan
      api-execution-graph
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/execution_graph.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-vulkan-execution-graph-fixture
      DEPENDS ${GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE}
    )

    if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan")
      set(GPU_API_VULKAN_USL_FIXTURES ${GPU_API_USL_FIXTURES})
    else()
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_USL_FIXTURES
        vulkan
        api
        ${GPU_API_USL_SOURCES}
      )
      set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_DESCRIPTOR_INDEXING_USL_FIXTURE
        vulkan
        api
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/descriptor_indexing.usl"
      )
      unset(GPU_USL_FIXTURE_TARGET_CAPS)
      list(APPEND GPU_API_VULKAN_USL_FIXTURES
        ${GPU_API_VULKAN_DESCRIPTOR_INDEXING_USL_FIXTURE}
        ${GPU_API_SUBGROUP_USL_FIXTURE}
        ${GPU_API_SHADER_F16_USL_FIXTURE}
      )
      add_custom_target(
        gpu-api-vulkan-fixtures
        DEPENDS ${GPU_API_VULKAN_USL_FIXTURES}
      )
    endif()
  endif()
endif()

if(GPU_BUILD_DX12 AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  gpu_add_usl_fixtures(
    GPU_DX12_USL_FIXTURES
    dx12
    samples
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-dx12-usl/triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-dx12-usl/textured_quad.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-dx12-usl/compute_buffer.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/mesh-triangle-usl/mesh_triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-atomics-usl/compute_atomics.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-cube-usl/textured_cube.usl"
  )
  list(GET GPU_DX12_USL_FIXTURES 0 GPU_DX12_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 1 GPU_DX12_TEXTURED_QUAD_US)
  list(GET GPU_DX12_USL_FIXTURES 2 GPU_DX12_COMPUTE_BUFFER_US)
  list(GET GPU_DX12_USL_FIXTURES 3 GPU_DX12_MESH_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 4 GPU_DX12_COMPUTE_ATOMICS_US)
  list(GET GPU_DX12_USL_FIXTURES 5 GPU_DX12_TEXTURED_CUBE_US)
  add_custom_target(gpu-dx12-fixtures DEPENDS ${GPU_DX12_USL_FIXTURES})
endif()

if(GPU_BUILD_VULKAN AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  gpu_add_usl_fixtures(
    GPU_VULKAN_USL_FIXTURES
    vulkan
    samples
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-vulkan-usl/triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-vulkan-usl/textured_quad.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-vulkan-usl/compute_buffer.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/mesh-triangle-vulkan-usl/mesh_triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-atomics-usl/compute_atomics.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-cube-usl/textured_cube.usl"
  )
  list(GET GPU_VULKAN_USL_FIXTURES 0 GPU_VULKAN_TRIANGLE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 1 GPU_VULKAN_TEXTURED_QUAD_US)
  list(GET GPU_VULKAN_USL_FIXTURES 2 GPU_VULKAN_COMPUTE_BUFFER_US)
  list(GET GPU_VULKAN_USL_FIXTURES 3 GPU_VULKAN_MESH_TRIANGLE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 4 GPU_VULKAN_COMPUTE_ATOMICS_US)
  list(GET GPU_VULKAN_USL_FIXTURES 5 GPU_VULKAN_TEXTURED_CUBE_US)
  if(GPU_BUILD_TESTS)
    gpu_add_usl_fixtures(
      GPU_VULKAN_TEST_USL_FIXTURES
      vulkan
      tests
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-usl/triangle.usl"
    )
    list(GET GPU_VULKAN_TEST_USL_FIXTURES 0 GPU_VULKAN_DYNAMIC_TRIANGLE_US)
    list(APPEND GPU_VULKAN_USL_FIXTURES ${GPU_VULKAN_TEST_USL_FIXTURES})
  endif()
  add_custom_target(gpu-vulkan-fixtures DEPENDS ${GPU_VULKAN_USL_FIXTURES})
endif()

if(GPU_BUILD_METAL AND GPU_BUILD_SAMPLES AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  gpu_add_usl_fixtures(
    GPU_METAL_USL_FIXTURES
    metal
    samples
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-usl/triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-quad-usl/textured_quad.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-usl/compute_visible.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-buffer-usl/compute_buffer.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/usl-reflection-check/reflection.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/usl-reflection-check/reflection_storage.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/mesh-triangle-usl/mesh_triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/compute-atomics-usl/compute_atomics.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/textured-cube-usl/textured_cube.usl"
  )
  list(GET GPU_METAL_USL_FIXTURES 0 GPU_METAL_TRIANGLE_US)
  list(GET GPU_METAL_USL_FIXTURES 1 GPU_METAL_TEXTURED_QUAD_US)
  list(GET GPU_METAL_USL_FIXTURES 2 GPU_METAL_COMPUTE_US)
  list(GET GPU_METAL_USL_FIXTURES 3 GPU_METAL_COMPUTE_BUFFER_US)
  list(GET GPU_METAL_USL_FIXTURES 4 GPU_METAL_REFLECTION_US)
  list(GET GPU_METAL_USL_FIXTURES 5 GPU_METAL_REFLECTION_STORAGE_US)
  list(GET GPU_METAL_USL_FIXTURES 6 GPU_METAL_MESH_TRIANGLE_US)
  list(GET GPU_METAL_USL_FIXTURES 7 GPU_METAL_COMPUTE_ATOMICS_US)
  list(GET GPU_METAL_USL_FIXTURES 8 GPU_METAL_TEXTURED_CUBE_US)
  add_custom_target(gpu-metal-fixtures DEPENDS ${GPU_METAL_USL_FIXTURES})
endif()

if(GPU_BUILD_BENCHMARKS)
  set(GPU_BENCHMARK_USL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/render_mrt.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-usl/triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/binding_churn.usl"
  )
  set(GPU_BENCHMARK_BINDLESS_USL_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/bindless_dispatch.usl"
  )
  set(GPU_BENCHMARK_USL_FIXTURES)

  if(GPU_BUILD_METAL)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_METAL_USL_FIXTURES
      metal
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_METAL_BINDLESS_USL_FIXTURE
      metal
      benchmarks
      ${GPU_BENCHMARK_BINDLESS_USL_SOURCE}
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    list(APPEND GPU_BENCHMARK_METAL_USL_FIXTURES
      ${GPU_BENCHMARK_METAL_BINDLESS_USL_FIXTURE}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_METAL_USL_FIXTURES}
    )
  endif()
  if(GPU_BUILD_VULKAN)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_VULKAN_USL_FIXTURES
      vulkan
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_VULKAN_BINDLESS_USL_FIXTURE
      vulkan
      benchmarks
      ${GPU_BENCHMARK_BINDLESS_USL_SOURCE}
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    list(APPEND GPU_BENCHMARK_VULKAN_USL_FIXTURES
      ${GPU_BENCHMARK_VULKAN_BINDLESS_USL_FIXTURE}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_VULKAN_USL_FIXTURES}
    )
  endif()
  if(GPU_BUILD_DX12)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_DX12_USL_FIXTURES
      dx12
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_DX12_BINDLESS_USL_FIXTURE
      dx12
      benchmarks
      ${GPU_BENCHMARK_BINDLESS_USL_SOURCE}
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    list(APPEND GPU_BENCHMARK_DX12_USL_FIXTURES
      ${GPU_BENCHMARK_DX12_BINDLESS_USL_FIXTURE}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_DX12_USL_FIXTURES}
    )
  endif()

  if(GPU_BUILD_METAL)
    set(GPU_BENCHMARK_BACKEND metal)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_METAL_USL_FIXTURES}
    )
  elseif(GPU_BUILD_DX12)
    set(GPU_BENCHMARK_BACKEND dx12)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_DX12_USL_FIXTURES}
    )
  elseif(GPU_BUILD_VULKAN)
    set(GPU_BENCHMARK_BACKEND vulkan)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_VULKAN_USL_FIXTURES}
    )
  endif()
  if(GPU_BENCHMARK_SELECTED_USL_FIXTURES)
    list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
         0
         GPU_BENCHMARK_RENDER_MRT_US)
    list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
         1
         GPU_BENCHMARK_TRIANGLE_US)
    list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
         2
         GPU_BENCHMARK_BINDING_US)
    list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
         3
         GPU_BENCHMARK_BINDLESS_DISPATCH_US)
  endif()

  add_custom_target(gpu-benchmark-fixtures
    DEPENDS ${GPU_BENCHMARK_USL_FIXTURES}
  )
endif()

if(GPU_BUILD_TESTS OR GPU_BUILD_BENCHMARKS)
  enable_testing()
endif()

if(GPU_BUILD_TESTS)
  add_executable(gpu-public-layout-test
    tests/api/layout.c
  )
  target_include_directories(gpu-public-layout-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  set_target_properties(gpu-public-layout-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-public-layout-test PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME api-public-layout COMMAND gpu-public-layout-test)

  set(GPU_PUBLIC_EXPORTS)
  foreach(header IN LISTS GPU_PUBLIC_HEADERS)
    file(READ "${header}" header_contents)
    string(REGEX MATCHALL
      "[\r\n]GPU_EXPORT[ \t\r\n]+[^;{}]+;"
      export_declarations
      "${header_contents}"
    )
    foreach(declaration IN LISTS export_declarations)
      if(NOT declaration)
        continue()
      endif()
      string(REGEX MATCH
        "(GPU[A-Z][A-Za-z0-9_]*)[ \t\r\n]*\\("
        export_match
        "${declaration}"
      )
      if(NOT export_match)
        message(FATAL_ERROR "Could not parse public export in ${header}")
      endif()
      list(APPEND GPU_PUBLIC_EXPORTS "${CMAKE_MATCH_1}")
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES GPU_PUBLIC_EXPORTS)
  list(SORT GPU_PUBLIC_EXPORTS)

  set(GPU_EXPORT_LINKAGE_SOURCE
      "${CMAKE_CURRENT_BINARY_DIR}/generated/export_linkage.c")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
  file(WRITE "${GPU_EXPORT_LINKAGE_SOURCE}"
    "#include <gpu/gpu.h>\n\n"
    "typedef void (*GPUExportSymbol)(void);\n\n"
    "static GPUExportSymbol volatile gpuExportSymbols[] = {\n"
  )
  foreach(export_name IN LISTS GPU_PUBLIC_EXPORTS)
    file(APPEND "${GPU_EXPORT_LINKAGE_SOURCE}"
      "  (GPUExportSymbol)${export_name},\n"
    )
  endforeach()
  file(APPEND "${GPU_EXPORT_LINKAGE_SOURCE}"
    "};\n\n"
    "int\n"
    "main(void) {\n"
    "  for (size_t i = 0u; i < GPU_ARRAY_LEN(gpuExportSymbols); i++) {\n"
    "    if (!gpuExportSymbols[i]) {\n"
    "      return 1;\n"
    "    }\n"
    "  }\n"
    "  return 0;\n"
    "}\n"
  )

  add_executable(gpu-export-linkage-test
    "${GPU_EXPORT_LINKAGE_SOURCE}"
  )
  target_link_libraries(gpu-export-linkage-test PRIVATE gpu)
  set_target_properties(gpu-export-linkage-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-export-linkage-test PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME api-export-linkage COMMAND gpu-export-linkage-test)

  add_test(
    NAME package-consumer
    COMMAND ${CMAKE_COMMAND}
      -DGPU_PACKAGE_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DGPU_PACKAGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DGPU_PACKAGE_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-consumer-test
      -DGPU_PACKAGE_CONFIG=$<CONFIG>
      -DGPU_PACKAGE_GENERATOR=${CMAKE_GENERATOR}
      -DGPU_PACKAGE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
      -DGPU_PACKAGE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
      -DGPU_PACKAGE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
      -DGPU_PACKAGE_MULTI_CONFIG=${GPU_GENERATOR_IS_MULTI_CONFIG}
      -DGPU_PACKAGE_RUNTIME_DIR=${GPU_VULKAN_LIBRARY_DIR}
      -DGPU_PACKAGE_VULKAN_ICD=${GPU_VULKAN_ICD}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_test.cmake
  )
  set_tests_properties(package-consumer PROPERTIES LABELS package)

  add_executable(gpu-api-test
    tests/api/atomic64.c
    tests/api/barrier.c
    tests/api/bindgroup.c
    tests/api/compute.c
    tests/api/clock_derivative.c
    tests/api/coordinate.c
    tests/api/copy.c
    tests/api/cube_texture.c
    tests/api/descriptor_array.c
    tests/api/execution_graph.c
    tests/api/line_texture.c
    tests/api/main.c
    tests/api/memory.c
    tests/api/metal_vertex_slots.c
    tests/api/query.c
    tests/api/queue.c
    tests/api/ray.c
    tests/api/render.c
    tests/api/resources.c
    tests/api/runner.c
    tests/api/runtime.c
    tests/api/sampler.c
    tests/api/sampler_feedback.c
    tests/api/shader.c
    tests/api/shader_f16.c
    tests/api/shader_sampler.c
    tests/api/storage_texture.c
    tests/api/subgroup.c
    tests/api/subgroup_matrix.c
    tests/api/texture_integer_clear.c
    tests/api/texture_transfer.c
    tests/api/texture_view_render.c
    tests/api/threading.c
    tests/api/untyped_pointer.c
    tests/api/volume_texture.c
    tests/api/vrs.c
  )
  target_link_libraries(gpu-api-test PRIVATE gpu)
  add_dependencies(gpu-api-test gpu-api-fixtures)
  add_dependencies(gpu-api-test gpu-api-atomic64-fixture)
  if(TARGET gpu-api-subgroup-matrix-fixture)
    add_dependencies(gpu-api-test gpu-api-subgroup-matrix-fixture)
  endif()
  if(TARGET gpu-api-ray-query-fixture)
    add_dependencies(gpu-api-test gpu-api-ray-query-fixture)
  endif()
  if(TARGET gpu-api-vulkan-fixtures)
    add_dependencies(gpu-api-test gpu-api-vulkan-fixtures)
  endif()
  if(TARGET gpu-api-vulkan-ray-query-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-ray-query-fixture)
  endif()
  if(TARGET gpu-api-dx12-ray-pipeline-fixture)
    add_dependencies(gpu-api-test gpu-api-dx12-ray-pipeline-fixture)
  endif()
  if(TARGET gpu-api-vulkan-ray-pipeline-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-ray-pipeline-fixture)
  endif()
  if(TARGET gpu-api-dx12-execution-graph-fixture)
    add_dependencies(gpu-api-test gpu-api-dx12-execution-graph-fixture)
  endif()
  if(TARGET gpu-api-vulkan-execution-graph-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-execution-graph-fixture)
  endif()
  if(TARGET gpu-api-vulkan-clock-derivative-fixtures)
    add_dependencies(gpu-api-test gpu-api-vulkan-clock-derivative-fixtures)
  endif()
  if(TARGET gpu-api-vulkan-untyped-pointer-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-untyped-pointer-fixture)
  endif()
  target_compile_definitions(gpu-api-test PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
    GPU_BUILD_WITH_DEBUG_MARKERS=$<BOOL:${GPU_BUILD_WITH_DEBUG_MARKERS}>
  )
  set_target_properties(gpu-api-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-api-test PRIVATE -Wall -Wextra -Werror)
  endif()

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-api-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-api-test>
      )
    endif()
  endforeach()

  add_test(NAME api-validation
    COMMAND gpu-api-test
            ${GPU_API_USL_FIXTURES}
  )
  if(GPU_API_RAY_QUERY_USL_FIXTURE)
    set_tests_properties(api-validation PROPERTIES
      ENVIRONMENT
        "GPU_RAY_QUERY_USL_PATH=${GPU_API_RAY_QUERY_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "dx12" AND
     GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_RAY_PIPELINE_USL_PATH=${GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE}"
    )
  elseif(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
         GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_RAY_PIPELINE_USL_PATH=${GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "dx12" AND
     GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_EXECUTION_GRAPH_USL_PATH=${GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE}"
    )
  elseif(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
         GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_EXECUTION_GRAPH_USL_PATH=${GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
     GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_SHADER_SUBGROUP_CLOCK_USL_PATH=${GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE}"
      "GPU_SHADER_DEVICE_CLOCK_USL_PATH=${GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE}"
      "GPU_COMPUTE_DERIVATIVE_QUADS_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE}"
      "GPU_COMPUTE_DERIVATIVE_LINEAR_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
     GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_UNTYPED_POINTER_USL_PATH=${GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_SUBGROUP_MATRIX_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_SUBGROUP_MATRIX_USL_PATH=${GPU_API_SUBGROUP_MATRIX_USL_FIXTURE}"
      "GPU_SUBGROUP_MATRIX_PROFILE=${GPU_API_SUBGROUP_MATRIX_PROFILE}"
    )
  endif()
  set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
    "GPU_ATOMIC64_USL_PATH=${GPU_API_ATOMIC64_USL_FIXTURE}"
  )

  if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(gpu-metal-mesh-test tests/api/mesh.c)
    target_link_libraries(gpu-metal-mesh-test PRIVATE gpu)
    add_dependencies(gpu-metal-mesh-test gpu-metal-mesh-test-fixture)
    set_target_properties(gpu-metal-mesh-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(NOT MSVC)
      target_compile_options(gpu-metal-mesh-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
    endif()
    add_test(NAME metal-mesh
      COMMAND gpu-metal-mesh-test
              metal
              "${GPU_METAL_MESH_TEST_US}"
    )
    set_tests_properties(metal-mesh PROPERTIES
      SKIP_RETURN_CODE 77
    )
  endif()

  if(GPU_BUILD_VULKAN)
    list(GET GPU_API_VULKAN_USL_FIXTURES 1 GPU_VULKAN_RENDER_MRT_US)

    add_test(NAME api-validation-vulkan
      COMMAND gpu-api-test
              ${GPU_API_VULKAN_USL_FIXTURES}
              vulkan
    )
    if(GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE)
      set_tests_properties(api-validation-vulkan PROPERTIES
        ENVIRONMENT
          "GPU_RAY_QUERY_USL_PATH=${GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_RAY_PIPELINE_USL_PATH=${GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_EXECUTION_GRAPH_USL_PATH=${GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_SHADER_SUBGROUP_CLOCK_USL_PATH=${GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE}"
        "GPU_SHADER_DEVICE_CLOCK_USL_PATH=${GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE}"
        "GPU_COMPUTE_DERIVATIVE_QUADS_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE}"
        "GPU_COMPUTE_DERIVATIVE_LINEAR_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_UNTYPED_POINTER_USL_PATH=${GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan")
      set(GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE
          ${GPU_API_SUBGROUP_MATRIX_USL_FIXTURE})
    else()
      set(GPU_USL_FIXTURE_TARGET_CAPS subgroup_matrix)
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE
        vulkan
        api-vulkan-subgroup-matrix
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/subgroup_matrix_vulkan.usl"
      )
      unset(GPU_USL_FIXTURE_TARGET_CAPS)
      add_custom_target(
        gpu-api-vulkan-subgroup-matrix-fixture
        DEPENDS ${GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE}
      )
      add_dependencies(gpu-api-test
                       gpu-api-vulkan-subgroup-matrix-fixture)
    endif()
    set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
      "GPU_SUBGROUP_MATRIX_USL_PATH=${GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE}"
      "GPU_SUBGROUP_MATRIX_PROFILE=16x8x8"
      "GPU_ATOMIC64_USL_PATH=${GPU_API_ATOMIC64_USL_FIXTURE}"
    )

    add_executable(gpu-vulkan-queue-test
      tests/vulkan/queue.c
    )
    target_include_directories(gpu-vulkan-queue-test PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gpu-vulkan-queue-test PRIVATE gpu Vulkan::Vulkan)
    if(TARGET us)
      target_link_libraries(gpu-vulkan-queue-test PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-vulkan-queue-test PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-vulkan-queue-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_executable(gpu-vulkan-shader-test
      tests/vulkan/shader.c
      tests/vulkan/texture.c
      tests/api/copy.c
      tests/api/texture_transfer.c
    )
    target_include_directories(gpu-vulkan-shader-test PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gpu-vulkan-shader-test PRIVATE gpu Vulkan::Vulkan)
    add_dependencies(gpu-vulkan-shader-test gpu-vulkan-fixtures)
    set_target_properties(gpu-vulkan-shader-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_executable(gpu-vulkan-mesh-test
      tests/api/mesh.c
    )
    target_link_libraries(gpu-vulkan-mesh-test PRIVATE gpu Vulkan::Vulkan)
    add_dependencies(gpu-vulkan-mesh-test gpu-vulkan-fixtures)
    set_target_properties(gpu-vulkan-mesh-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_executable(gpu-vulkan-indirect-copy-test
      tests/vulkan/indirect_copy.c
    )
    target_link_libraries(gpu-vulkan-indirect-copy-test PRIVATE
      gpu
      Vulkan::Vulkan
    )
    set_target_properties(gpu-vulkan-indirect-copy-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    if(NOT MSVC)
      target_compile_options(gpu-vulkan-queue-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
      target_compile_options(gpu-vulkan-shader-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
      target_compile_options(gpu-vulkan-mesh-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
      target_compile_options(gpu-vulkan-indirect-copy-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
    endif()

    add_test(NAME vulkan-queue
      COMMAND gpu-vulkan-queue-test
    )
    add_test(NAME vulkan-shader
      COMMAND gpu-vulkan-shader-test
              "${GPU_VULKAN_DYNAMIC_TRIANGLE_US}"
              "${GPU_VULKAN_RENDER_MRT_US}"
              "${GPU_VULKAN_TEXTURED_QUAD_US}"
    )
    add_test(NAME vulkan-mesh
      COMMAND gpu-vulkan-mesh-test
              vulkan
              "${GPU_VULKAN_MESH_TRIANGLE_US}"
    )
    set_tests_properties(vulkan-mesh PROPERTIES
      SKIP_RETURN_CODE 77
    )
    add_test(NAME vulkan-indirect-copy
      COMMAND gpu-vulkan-indirect-copy-test
    )
    set_tests_properties(vulkan-indirect-copy PROPERTIES
      SKIP_RETURN_CODE 77
    )
  endif()

  if(GPU_BUILD_DX12)
    add_executable(gpu-dx12-queue-test tests/dx12/queue.c)
    target_link_libraries(gpu-dx12-queue-test PRIVATE gpu)
    set_target_properties(gpu-dx12-queue-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    if(TARGET us)
      add_custom_command(TARGET gpu-dx12-queue-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:us>
                $<TARGET_FILE_DIR:gpu-dx12-queue-test>
      )
    endif()
    if(TARGET ds)
      add_custom_command(TARGET gpu-dx12-queue-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:ds>
                $<TARGET_FILE_DIR:gpu-dx12-queue-test>
      )
    endif()

    add_test(NAME dx12-queue COMMAND gpu-dx12-queue-test)

    add_executable(gpu-dx12-barrier-test tests/dx12/barrier.c)
    target_link_libraries(gpu-dx12-barrier-test PRIVATE gpu dxguid)
    if(TARGET us)
      target_link_libraries(gpu-dx12-queue-test PRIVATE us)
      target_link_libraries(gpu-dx12-barrier-test PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-dx12-queue-test PRIVATE gpu_usl)
      target_link_libraries(gpu-dx12-barrier-test PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-dx12-barrier-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    foreach(runtime_target us ds)
      if(TARGET ${runtime_target})
        add_custom_command(TARGET gpu-dx12-barrier-test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtime_target}>
                  $<TARGET_FILE_DIR:gpu-dx12-barrier-test>
        )
      endif()
    endforeach()

    add_test(NAME dx12-barrier COMMAND gpu-dx12-barrier-test)

    add_executable(gpu-dx12-depth-test tests/dx12/depth.c)
    target_link_libraries(gpu-dx12-depth-test PRIVATE gpu)
    add_dependencies(gpu-dx12-depth-test gpu-dx12-fixtures)
    set_target_properties(gpu-dx12-depth-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    foreach(runtime_target us ds)
      if(TARGET ${runtime_target})
        add_custom_command(TARGET gpu-dx12-depth-test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtime_target}>
                  $<TARGET_FILE_DIR:gpu-dx12-depth-test>
        )
      endif()
    endforeach()

    add_test(NAME dx12-depth-pipeline
      COMMAND gpu-dx12-depth-test
              "${GPU_DX12_TRIANGLE_US}"
    )

    add_executable(gpu-dx12-texture-test tests/dx12/texture.c)
    target_link_libraries(gpu-dx12-texture-test PRIVATE gpu)
    add_dependencies(gpu-dx12-texture-test gpu-dx12-fixtures)
    set_target_properties(gpu-dx12-texture-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    foreach(runtime_target us ds)
      if(TARGET ${runtime_target})
        add_custom_command(TARGET gpu-dx12-texture-test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtime_target}>
                  $<TARGET_FILE_DIR:gpu-dx12-texture-test>
        )
      endif()
    endforeach()

    add_test(NAME dx12-texture
      COMMAND gpu-dx12-texture-test
              "${GPU_DX12_TEXTURED_QUAD_US}"
    )

    add_executable(gpu-dx12-mesh-test tests/api/mesh.c)
    target_link_libraries(gpu-dx12-mesh-test PRIVATE gpu)
    add_dependencies(gpu-dx12-mesh-test gpu-dx12-fixtures)
    set_target_properties(gpu-dx12-mesh-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    foreach(runtime_target us ds)
      if(TARGET ${runtime_target})
        add_custom_command(TARGET gpu-dx12-mesh-test POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtime_target}>
                  $<TARGET_FILE_DIR:gpu-dx12-mesh-test>
        )
      endif()
    endforeach()

    add_test(NAME dx12-mesh
      COMMAND gpu-dx12-mesh-test
              dx12
              "${GPU_DX12_MESH_TRIANGLE_US}"
    )
    set_tests_properties(dx12-mesh PROPERTIES
      SKIP_RETURN_CODE 77
    )
  endif()
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL)
  function(gpu_configure_metal_sample target)
    target_compile_options(${target} PRIVATE -fobjc-arc)
    target_link_libraries(${target} PRIVATE
      gpu
      ${GPU_FRAMEWORK_UI}
      ${GPU_FRAMEWORK_METAL}
      ${GPU_FRAMEWORK_FOUNDATION}
      ${GPU_FRAMEWORK_QUARTZCORE}
    )
  endfunction()
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gpu-triangle-manual
    samples/triangle-manual/main.m
  )
  gpu_configure_metal_sample(gpu-triangle-manual)
  set_target_properties(gpu-triangle-manual PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
      "${CMAKE_CURRENT_BINARY_DIR}/samples/gpu-triangle-manual"
  )
  set(GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP
    "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/gpu-triangle-manual-artifacts.stamp"
  )
  add_custom_command(
    OUTPUT "${GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:gpu-triangle-manual>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-manual/triangle.usl.metal"
            $<TARGET_FILE_DIR:gpu-triangle-manual>
    COMMAND ${CMAKE_COMMAND} -E touch
            "${GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-manual/triangle.usl.metal"
    VERBATIM
  )
  add_custom_target(gpu-triangle-manual-artifacts
    DEPENDS "${GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP}"
  )
  add_dependencies(gpu-triangle-manual gpu-triangle-manual-artifacts)
  if(GPU_BUILD_TESTS)
    add_test(NAME metal-resize-lifecycle
             COMMAND gpu-triangle-manual)
    set_tests_properties(metal-resize-lifecycle PROPERTIES
      ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=4;GPU_SAMPLE_RESIZE_AFTER_FRAMES=1"
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-triangle-metal-usl
    samples/triangle-usl/main.m
  )
  gpu_configure_metal_sample(gpu-triangle-metal-usl)
  gpu_attach_usl_sample(gpu-triangle-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_TRIANGLE_US}")

  add_executable(gpu-textured-quad-metal-usl
    samples/textured-quad-usl/main.m
  )
  gpu_configure_metal_sample(gpu-textured-quad-metal-usl)
  gpu_attach_usl_sample(gpu-textured-quad-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_TEXTURED_QUAD_US}")

  add_executable(gpu-textured-cube-metal-usl
    samples/textured-cube-usl/main.m
  )
  target_include_directories(gpu-textured-cube-metal-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  gpu_configure_metal_sample(gpu-textured-cube-metal-usl)
  gpu_attach_usl_sample(gpu-textured-cube-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_TEXTURED_CUBE_US}")

  add_executable(gpu-mesh-triangle-metal-usl
    samples/mesh-triangle-usl/main.m
  )
  gpu_configure_metal_sample(gpu-mesh-triangle-metal-usl)
  gpu_attach_usl_sample(gpu-mesh-triangle-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_MESH_TRIANGLE_US}")

  add_executable(gpu-compute-metal-usl
    samples/compute-usl/main.m
  )
  gpu_configure_metal_sample(gpu-compute-metal-usl)
  gpu_attach_usl_sample(gpu-compute-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_COMPUTE_US}")

  add_executable(gpu-compute-buffer-metal-usl
    samples/compute-buffer-usl/main.m
  )
  gpu_configure_metal_sample(gpu-compute-buffer-metal-usl)
  gpu_attach_usl_sample(gpu-compute-buffer-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_COMPUTE_BUFFER_US}")

  add_executable(gpu-compute-atomics-metal-usl
    samples/compute-atomics-usl/main.c
  )
  target_compile_definitions(gpu-compute-atomics-metal-usl PRIVATE
    GPU_SAMPLE_BACKEND=GPU_BACKEND_METAL
  )
  target_link_libraries(gpu-compute-atomics-metal-usl PRIVATE gpu)
  gpu_attach_usl_sample(gpu-compute-atomics-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_COMPUTE_ATOMICS_US}")
  set_target_properties(gpu-compute-atomics-metal-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME metal-compute-atomics
      COMMAND gpu-compute-atomics-metal-usl
              "${GPU_METAL_COMPUTE_ATOMICS_US}"
    )
  endif()

  add_executable(gpu-usl-reflection-check
    samples/usl-reflection-check/main.c
  )
  target_link_libraries(gpu-usl-reflection-check PRIVATE gpu)
  gpu_attach_usl_sample(gpu-usl-reflection-check
                        gpu-metal-fixtures
                        "${GPU_METAL_REFLECTION_US}"
                        "${GPU_METAL_REFLECTION_STORAGE_US}")
  set_target_properties(gpu-usl-reflection-check PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL AND
   CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(GPU_USL_HOST_FIXTURE "" CACHE FILEPATH
      "Host gpu-usl-fixture executable used for cross-compiled samples")
  if(NOT GPU_USL_HOST_FIXTURE)
    find_program(GPU_USL_HOST_FIXTURE_AUTO
      NAMES gpu-usl-fixture
      HINTS
        "${CMAKE_CURRENT_SOURCE_DIR}/out/build/macos-debug"
        "${CMAKE_CURRENT_SOURCE_DIR}/out/build/macos-release"
    )
    if(GPU_USL_HOST_FIXTURE_AUTO)
      set(GPU_USL_HOST_FIXTURE "${GPU_USL_HOST_FIXTURE_AUTO}")
    endif()
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_FIXTURE}")
    message(FATAL_ERROR
      "iOS samples require a host gpu-usl-fixture. Build the macOS sample "
      "fixtures first or set GPU_USL_HOST_FIXTURE.")
  endif()

  set(GPU_IOS_TRIANGLE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/metal/ios-triangle")
  set(GPU_IOS_TRIANGLE_SOURCE
      "${GPU_IOS_TRIANGLE_DIR}/triangle.usl")
  set(GPU_IOS_TRIANGLE_US
      "${GPU_IOS_TRIANGLE_DIR}/triangle.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_TRIANGLE_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_IOS_TRIANGLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-usl/triangle.usl"
            "${GPU_IOS_TRIANGLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_TRIANGLE_SOURCE}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-usl/triangle.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_TRIANGLE_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-triangle-ios-usl MACOSX_BUNDLE
    samples/triangle-ios-usl/main.m
    "${GPU_IOS_TRIANGLE_US}"
  )
  gpu_configure_metal_sample(gpu-triangle-ios-usl)
  target_compile_options(gpu-triangle-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-triangle-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "com.recp.gpu.triangle-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_MESH_TRIANGLE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/metal/ios-mesh-triangle")
  set(GPU_IOS_MESH_TRIANGLE_SOURCE
      "${GPU_IOS_MESH_TRIANGLE_DIR}/mesh_triangle.usl")
  set(GPU_IOS_MESH_TRIANGLE_US
      "${GPU_IOS_MESH_TRIANGLE_DIR}/mesh_triangle.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_MESH_TRIANGLE_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_IOS_MESH_TRIANGLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/mesh-triangle-usl/mesh_triangle.usl"
            "${GPU_IOS_MESH_TRIANGLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_MESH_TRIANGLE_SOURCE}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/mesh-triangle-usl/mesh_triangle.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_MESH_TRIANGLE_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-mesh-triangle-ios-usl MACOSX_BUNDLE
    samples/mesh-triangle-ios-usl/main.m
    "${GPU_IOS_MESH_TRIANGLE_US}"
  )
  gpu_configure_metal_sample(gpu-mesh-triangle-ios-usl)
  target_compile_options(gpu-mesh-triangle-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-mesh-triangle-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "com.recp.gpu.mesh-triangle-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_RAY_QUERY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/metal/ios-ray-query")
  set(GPU_IOS_RAY_QUERY_SOURCE
      "${GPU_IOS_RAY_QUERY_DIR}/ray_query.usl")
  set(GPU_IOS_RAY_QUERY_US
      "${GPU_IOS_RAY_QUERY_DIR}/ray_query.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_RAY_QUERY_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_IOS_RAY_QUERY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/ray_query.usl"
            "${GPU_IOS_RAY_QUERY_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            USL_TARGET_CAPS=ray_query
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_RAY_QUERY_SOURCE}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/ray_query.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_RAY_QUERY_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-ray-query-ios-usl MACOSX_BUNDLE
    samples/ray-query-ios-usl/main.m
    tests/api/ray.c
    tests/api/runner.c
    "${GPU_IOS_RAY_QUERY_US}"
  )
  gpu_configure_metal_sample(gpu-ray-query-ios-usl)
  target_compile_options(gpu-ray-query-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-ray-query-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "com.recp.gpu.ray-query-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_VRS_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/metal/ios-vrs")
  set(GPU_IOS_VRS_SOURCE
      "${GPU_IOS_VRS_DIR}/vrs.usl")
  set(GPU_IOS_VRS_US
      "${GPU_IOS_VRS_DIR}/vrs.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_VRS_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_IOS_VRS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/vrs-ios-usl/vrs.usl"
            "${GPU_IOS_VRS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_VRS_SOURCE}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/vrs-ios-usl/vrs.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_VRS_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-vrs-ios-usl MACOSX_BUNDLE
    samples/vrs-ios-usl/main.m
    "${GPU_IOS_VRS_US}"
  )
  gpu_configure_metal_sample(gpu-vrs-ios-usl)
  target_compile_options(gpu-vrs-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-vrs-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "com.recp.gpu.vrs-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_METAL4_BINDLESS_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/metal/ios-metal4-bindless")
  set(GPU_IOS_METAL4_BINDLESS_SOURCE
      "${GPU_IOS_METAL4_BINDLESS_DIR}/metal4_bindless.usl")
  set(GPU_IOS_METAL4_BINDLESS_US
      "${GPU_IOS_METAL4_BINDLESS_DIR}/metal4_bindless.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_METAL4_BINDLESS_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_IOS_METAL4_BINDLESS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/samples/metal4-bindless-ios-usl/metal4_bindless.usl"
            "${GPU_IOS_METAL4_BINDLESS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            USL_TARGET_CAPS=descriptor_indexing
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_METAL4_BINDLESS_SOURCE}"
    DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/metal4-bindless-ios-usl/metal4_bindless.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_METAL4_BINDLESS_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-metal4-bindless-ios-usl MACOSX_BUNDLE
    samples/metal4-bindless-ios-usl/main.m
    "${GPU_IOS_METAL4_BINDLESS_US}"
  )
  gpu_configure_metal_sample(gpu-metal4-bindless-ios-usl)
  target_compile_options(gpu-metal4-bindless-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-metal4-bindless-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "com.recp.gpu.metal4-bindless-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_DX12)
  add_executable(gpu-triangle-dx12-usl
    samples/triangle-dx12-usl/main.c
  )
  target_link_libraries(gpu-triangle-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_sample(gpu-triangle-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_TRIANGLE_US}")
  set_target_properties(gpu-triangle-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-textured-quad-dx12-usl
    samples/textured-quad-dx12-usl/main.c
  )
  target_link_libraries(gpu-textured-quad-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_sample(gpu-textured-quad-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_TEXTURED_QUAD_US}")
  set_target_properties(gpu-textured-quad-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-textured-cube-dx12-usl
    samples/textured-cube-dx12-usl/main.c
  )
  target_include_directories(gpu-textured-cube-dx12-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  target_link_libraries(gpu-textured-cube-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_sample(gpu-textured-cube-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_TEXTURED_CUBE_US}")
  set_target_properties(gpu-textured-cube-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-buffer-dx12-usl
    samples/compute-buffer-dx12-usl/main.c
  )
  target_link_libraries(gpu-compute-buffer-dx12-usl PRIVATE gpu)
  gpu_attach_usl_sample(gpu-compute-buffer-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_COMPUTE_BUFFER_US}")
  set_target_properties(gpu-compute-buffer-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-atomics-dx12-usl
    samples/compute-atomics-usl/main.c
  )
  target_compile_definitions(gpu-compute-atomics-dx12-usl PRIVATE
    GPU_SAMPLE_BACKEND=GPU_BACKEND_DX12
  )
  target_link_libraries(gpu-compute-atomics-dx12-usl PRIVATE gpu)
  gpu_attach_usl_sample(gpu-compute-atomics-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_COMPUTE_ATOMICS_US}")
  set_target_properties(gpu-compute-atomics-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-render-dx12-usl
    samples/compute-buffer-dx12-usl/window.c
  )
  target_link_libraries(gpu-compute-render-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_sample(gpu-compute-render-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_COMPUTE_BUFFER_US}")
  set_target_properties(gpu-compute-render-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  if(GPU_BUILD_TESTS)
    add_test(
      NAME dx12-compute-atomics
      COMMAND gpu-compute-atomics-dx12-usl
              "${GPU_DX12_COMPUTE_ATOMICS_US}"
    )
    add_test(
      NAME dx12-compute-buffer
      COMMAND gpu-compute-buffer-dx12-usl
              "${GPU_DX12_COMPUTE_BUFFER_US}"
    )
    add_test(NAME dx12-triangle
             COMMAND gpu-triangle-dx12-usl)
    add_test(NAME dx12-textured-quad
             COMMAND gpu-textured-quad-dx12-usl)
    add_test(NAME dx12-textured-cube
             COMMAND gpu-textured-cube-dx12-usl)
    add_test(NAME dx12-compute-render
             COMMAND gpu-compute-render-dx12-usl)
    set_tests_properties(
      dx12-triangle
      dx12-textured-quad
      dx12-textured-cube
      dx12-compute-render
      PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 30
    )
    set_property(
      TEST dx12-triangle dx12-textured-quad dx12-textured-cube
           dx12-compute-render
      PROPERTY ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=96"
        "GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
        "GPU_SAMPLE_SKIP_NONINTERACTIVE=1"
    )
  endif()
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_VULKAN)
  add_executable(gpu-compute-buffer-vulkan-usl
    samples/compute-buffer-vulkan-usl/main.c
  )
  target_link_libraries(gpu-compute-buffer-vulkan-usl PRIVATE gpu)
  if(NOT WIN32)
    target_link_libraries(gpu-compute-buffer-vulkan-usl PRIVATE m)
  endif()
  gpu_attach_usl_sample(gpu-compute-buffer-vulkan-usl
                        gpu-vulkan-fixtures
                        "${GPU_VULKAN_COMPUTE_BUFFER_US}")
  set_target_properties(gpu-compute-buffer-vulkan-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-atomics-vulkan-usl
    samples/compute-atomics-usl/main.c
  )
  target_compile_definitions(gpu-compute-atomics-vulkan-usl PRIVATE
    GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
  )
  target_link_libraries(gpu-compute-atomics-vulkan-usl PRIVATE gpu)
  gpu_attach_usl_sample(gpu-compute-atomics-vulkan-usl
                        gpu-vulkan-fixtures
                        "${GPU_VULKAN_COMPUTE_ATOMICS_US}")
  set_target_properties(gpu-compute-atomics-vulkan-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  if(GPU_BUILD_TESTS)
    add_test(
      NAME vulkan-compute-atomics
      COMMAND gpu-compute-atomics-vulkan-usl
              "${GPU_VULKAN_COMPUTE_ATOMICS_US}"
    )
    add_test(
      NAME vulkan-compute-buffer
      COMMAND gpu-compute-buffer-vulkan-usl
              "${GPU_VULKAN_COMPUTE_BUFFER_US}"
    )
  endif()

  if(APPLE)
    add_executable(gpu-triangle-vulkan-usl
      samples/triangle-vulkan-usl/main.m
    )
    target_compile_options(gpu-triangle-vulkan-usl PRIVATE -fobjc-arc)
    target_link_libraries(gpu-triangle-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_sample(gpu-triangle-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_TRIANGLE_US}")

    add_executable(gpu-textured-quad-vulkan-usl
      samples/textured-quad-vulkan-usl/main.m
    )
    target_compile_options(gpu-textured-quad-vulkan-usl PRIVATE -fobjc-arc)
    target_link_libraries(gpu-textured-quad-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_sample(gpu-textured-quad-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_TEXTURED_QUAD_US}")

    add_executable(gpu-textured-cube-vulkan-usl
      samples/textured-cube-usl/main.m
    )
    target_compile_options(gpu-textured-cube-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-textured-cube-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_include_directories(gpu-textured-cube-vulkan-usl PRIVATE
      "${GPU_CGLM_INCLUDE_DIR}"
    )
    target_link_libraries(gpu-textured-cube-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_sample(gpu-textured-cube-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_TEXTURED_CUBE_US}")

    add_executable(gpu-mesh-triangle-vulkan-usl
      samples/mesh-triangle-vulkan-usl/main.m
    )
    target_compile_options(gpu-mesh-triangle-vulkan-usl PRIVATE -fobjc-arc)
    target_link_libraries(gpu-mesh-triangle-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_sample(gpu-mesh-triangle-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_MESH_TRIANGLE_US}")

    add_executable(gpu-compute-render-vulkan-usl
      samples/compute-buffer-usl/main.m
    )
    target_compile_options(gpu-compute-render-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-compute-render-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_link_libraries(gpu-compute-render-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_sample(gpu-compute-render-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_COMPUTE_BUFFER_US}")

    if(GPU_BUILD_TESTS)
      add_test(NAME vulkan-triangle
               COMMAND gpu-triangle-vulkan-usl)
      add_test(NAME vulkan-textured-quad
               COMMAND gpu-textured-quad-vulkan-usl)
      add_test(NAME vulkan-textured-cube
               COMMAND gpu-textured-cube-vulkan-usl)
      add_test(NAME vulkan-mesh-triangle
               COMMAND gpu-mesh-triangle-vulkan-usl)
      add_test(NAME vulkan-compute-render
               COMMAND gpu-compute-render-vulkan-usl)
      add_test(NAME vulkan-resize-lifecycle
               COMMAND gpu-triangle-vulkan-usl)
      set_tests_properties(
        vulkan-triangle
        vulkan-textured-quad
        vulkan-textured-cube
        vulkan-mesh-triangle
        vulkan-compute-render
        PROPERTIES
          ENVIRONMENT
            "GPU_SAMPLE_EXIT_AFTER_FRAMES=96;GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
          TIMEOUT 30
      )
      set_tests_properties(vulkan-resize-lifecycle PROPERTIES
        ENVIRONMENT
          "GPU_SAMPLE_EXIT_AFTER_FRAMES=4;GPU_SAMPLE_RESIZE_AFTER_FRAMES=1"
        TIMEOUT 30
      )
    endif()
  endif()
endif()

if((GPU_BUILD_TESTS OR GPU_BUILD_BENCHMARKS) AND
   GPU_BUILD_VULKAN AND APPLE)
  if(EXISTS "${GPU_VULKAN_ICD}")
    foreach(vulkanTest
            api-validation-vulkan
            vulkan-queue
            vulkan-shader
            vulkan-mesh
            vulkan-indirect-copy
            vulkan-compute-buffer
            vulkan-triangle
            vulkan-textured-quad
            vulkan-textured-cube
            vulkan-mesh-triangle
            vulkan-compute-render
            vulkan-resize-lifecycle)
      if(TEST ${vulkanTest})
        set_property(TEST ${vulkanTest} APPEND PROPERTY ENVIRONMENT
          "VK_ICD_FILENAMES=${GPU_VULKAN_ICD}"
        )
      endif()
    endforeach()
  endif()
endif()

if(GPU_BUILD_BENCHMARKS)
  add_executable(gpu-dispatch-bench
    benchmarks/bench.c
    benchmarks/dispatch.c
  )
  target_link_libraries(gpu-dispatch-bench PRIVATE gpu)
  target_include_directories(gpu-dispatch-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-dispatch-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
    GPU_BACKEND_METAL_ONLY=$<BOOL:${GPU_BACKEND_METAL_ONLY}>
    GPU_BACKEND_VULKAN_ONLY=$<BOOL:${GPU_BACKEND_VULKAN_ONLY}>
    GPU_BACKEND_DX12_ONLY=$<BOOL:${GPU_BACKEND_DX12_ONLY}>
  )
  set_target_properties(gpu-dispatch-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-dispatch-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-dispatch-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-ray-dispatch-bench
    benchmarks/bench.c
    benchmarks/ray_dispatch.c
  )
  target_link_libraries(gpu-ray-dispatch-bench PRIVATE gpu)
  target_include_directories(gpu-ray-dispatch-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-ray-dispatch-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
    GPU_BACKEND_METAL_ONLY=$<BOOL:${GPU_BACKEND_METAL_ONLY}>
    GPU_BACKEND_VULKAN_ONLY=$<BOOL:${GPU_BACKEND_VULKAN_ONLY}>
    GPU_BACKEND_DX12_ONLY=$<BOOL:${GPU_BACKEND_DX12_ONLY}>
  )
  set_target_properties(gpu-ray-dispatch-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-ray-dispatch-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-ray-dispatch-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-queue-upload-bench
    benchmarks/bench.c
    benchmarks/queue_upload.c
  )
  target_link_libraries(gpu-queue-upload-bench PRIVATE gpu)
  target_include_directories(gpu-queue-upload-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-queue-upload-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-queue-upload-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-queue-upload-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-queue-submit-bench
    benchmarks/bench.c
    benchmarks/queue_submit.c
  )
  target_link_libraries(gpu-queue-submit-bench PRIVATE gpu)
  target_include_directories(gpu-queue-submit-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-queue-submit-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-queue-submit-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-queue-submit-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-texture-upload-bench
    benchmarks/bench.c
    benchmarks/texture_upload.c
  )
  target_link_libraries(gpu-texture-upload-bench PRIVATE gpu)
  target_include_directories(gpu-texture-upload-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-texture-upload-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-texture-upload-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-texture-upload-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-static-scene-bench
    benchmarks/bench.c
    benchmarks/render.c
    benchmarks/static_scene.c
  )
  target_link_libraries(gpu-static-scene-bench PRIVATE gpu)
  target_include_directories(gpu-static-scene-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-static-scene-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-static-scene-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-static-scene-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-static-scene-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-state-churn-bench
    benchmarks/bench.c
    benchmarks/render.c
    benchmarks/state_churn.c
  )
  target_link_libraries(gpu-state-churn-bench PRIVATE gpu)
  target_include_directories(gpu-state-churn-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-state-churn-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-state-churn-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-state-churn-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-state-churn-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-binding-churn-bench
    benchmarks/bench.c
    benchmarks/render.c
    benchmarks/binding_churn.c
  )
  target_link_libraries(gpu-binding-churn-bench PRIVATE gpu)
  target_include_directories(gpu-binding-churn-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-binding-churn-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-binding-churn-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-binding-churn-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-binding-churn-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-bindless-update-bench
    benchmarks/bench.c
    benchmarks/bindless_update.c
  )
  target_link_libraries(gpu-bindless-update-bench PRIVATE gpu)
  target_include_directories(gpu-bindless-update-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-bindless-update-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-bindless-update-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-bindless-update-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-bindless-dispatch-bench
    benchmarks/bench.c
    benchmarks/bindless_dispatch.c
  )
  target_link_libraries(gpu-bindless-dispatch-bench PRIVATE gpu)
  target_include_directories(gpu-bindless-dispatch-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-bindless-dispatch-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-bindless-dispatch-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-bindless-dispatch-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-bindless-render-bench
    benchmarks/bench.c
    benchmarks/render.c
    benchmarks/bindless_render.c
  )
  target_link_libraries(gpu-bindless-render-bench PRIVATE gpu)
  target_include_directories(gpu-bindless-render-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-bindless-render-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-bindless-render-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-bindless-render-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-bindless-render-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-upload-heavy-bench
    benchmarks/bench.c
    benchmarks/render.c
    benchmarks/upload_heavy.c
  )
  target_link_libraries(gpu-upload-heavy-bench PRIVATE gpu)
  target_include_directories(gpu-upload-heavy-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-upload-heavy-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-upload-heavy-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-upload-heavy-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-upload-heavy-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-pipeline-stress-bench
    benchmarks/bench.c
    benchmarks/render.c
    benchmarks/pipeline_stress.c
  )
  target_link_libraries(gpu-pipeline-stress-bench PRIVATE gpu)
  target_include_directories(gpu-pipeline-stress-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-pipeline-stress-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-pipeline-stress-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-pipeline-stress-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-pipeline-stress-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-validation-bench
    benchmarks/bench.c
    benchmarks/validation.c
  )
  target_link_libraries(gpu-validation-bench PRIVATE gpu)
  target_include_directories(gpu-validation-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-validation-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-validation-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-validation-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-validation-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-marker-bench
    benchmarks/bench.c
    benchmarks/markers.c
  )
  target_link_libraries(gpu-marker-bench PRIVATE gpu)
  target_include_directories(gpu-marker-bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-marker-bench PRIVATE
    GPU_BUILD_WITH_DEBUG_MARKERS=$<BOOL:${GPU_BUILD_WITH_DEBUG_MARKERS}>
  )
  set_target_properties(gpu-marker-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-marker-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-marker-bench PRIVATE -Wall -Wextra)
  endif()

  if(APPLE AND GPU_BUILD_METAL)
    add_executable(gpu-native-metal-render-bench
      benchmarks/bench.c
      benchmarks/native_metal_render.m
    )
    target_include_directories(gpu-native-metal-render-bench PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(gpu-native-metal-render-bench PRIVATE
      "-framework Foundation"
      "-framework Metal"
    )
    set_target_properties(gpu-native-metal-render-bench PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    target_compile_options(gpu-native-metal-render-bench PRIVATE
      -Wall
      -Wextra
    )
  endif()

  if(GPU_BUILD_VULKAN)
    add_executable(gpu-native-vulkan-render-bench
      benchmarks/bench.c
      benchmarks/native_vulkan_render.c
    )
    target_include_directories(gpu-native-vulkan-render-bench PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(gpu-native-vulkan-render-bench PRIVATE
      Vulkan::Vulkan
    )
    if(TARGET us)
      target_link_libraries(gpu-native-vulkan-render-bench PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-native-vulkan-render-bench PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-native-vulkan-render-bench PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(MSVC)
      target_compile_options(gpu-native-vulkan-render-bench PRIVATE /W3)
    else()
      target_compile_options(gpu-native-vulkan-render-bench PRIVATE
        -Wall
        -Wextra
      )
    endif()
  endif()

  foreach(benchmark_target
          gpu-static-scene-bench
          gpu-state-churn-bench
          gpu-binding-churn-bench
          gpu-bindless-dispatch-bench
          gpu-bindless-render-bench
          gpu-upload-heavy-bench
          gpu-pipeline-stress-bench)
    add_dependencies(${benchmark_target} gpu-benchmark-fixtures)
  endforeach()
  if(TARGET gpu-native-vulkan-render-bench)
    add_dependencies(gpu-native-vulkan-render-bench gpu-benchmark-fixtures)
  endif()

  if(DEFINED GPU_BENCHMARK_BACKEND)
    function(gpu_add_perf_smoke name target)
      add_test(NAME ${name} COMMAND ${target} ${ARGN})
      set_tests_properties(${name} PROPERTIES
        LABELS perf-smoke
        RUN_SERIAL TRUE
        TIMEOUT 60
      )
    endfunction()

    gpu_add_perf_smoke(
      perf-smoke-queue-submit
      gpu-queue-submit-bench
      ${GPU_BENCHMARK_BACKEND} 4 64 3
    )
    gpu_add_perf_smoke(
      perf-smoke-ray-dispatch
      gpu-ray-dispatch-bench
      100000
    )
    gpu_add_perf_smoke(
      perf-smoke-queue-upload
      gpu-queue-upload-bench
      ${GPU_BENCHMARK_BACKEND} 64 256
    )
    gpu_add_perf_smoke(
      perf-smoke-texture-upload
      gpu-texture-upload-bench
      ${GPU_BENCHMARK_BACKEND} 8 16 16
    )
    gpu_add_perf_smoke(
      perf-smoke-static-scene
      gpu-static-scene-bench
      "${GPU_BENCHMARK_RENDER_MRT_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-state-churn
      gpu-state-churn-bench
      "${GPU_BENCHMARK_RENDER_MRT_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-binding-churn
      gpu-binding-churn-bench
      "${GPU_BENCHMARK_BINDING_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-bindless-update
      gpu-bindless-update-bench
      ${GPU_BENCHMARK_BACKEND} 16 32 3
    )
    gpu_add_perf_smoke(
      perf-smoke-bindless-dispatch
      gpu-bindless-dispatch-bench
      "${GPU_BENCHMARK_BINDLESS_DISPATCH_US}"
      ${GPU_BENCHMARK_BACKEND} 32 3
    )
    gpu_add_perf_smoke(
      perf-smoke-bindless-render
      gpu-bindless-render-bench
      "${GPU_BENCHMARK_BINDLESS_DISPATCH_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-upload-heavy
      gpu-upload-heavy-bench
      "${GPU_BENCHMARK_TRIANGLE_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-pipeline-stress
      gpu-pipeline-stress-bench
      "${GPU_BENCHMARK_RENDER_MRT_US}"
      ${GPU_BENCHMARK_BACKEND} 12 1
    )
    if(TARGET gpu-native-metal-render-bench)
      foreach(native_mode static state binding upload)
        gpu_add_perf_smoke(
          perf-smoke-native-metal-${native_mode}
          gpu-native-metal-render-bench
          ${native_mode} 32 4 12 1
        )
      endforeach()
    endif()
    if(TARGET gpu-native-vulkan-render-bench)
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-static
        gpu-native-vulkan-render-bench
        static "${GPU_BENCHMARK_RENDER_MRT_US}" 32 4 12 1
      )
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-state
        gpu-native-vulkan-render-bench
        state "${GPU_BENCHMARK_RENDER_MRT_US}" 32 4 12 1
      )
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-binding
        gpu-native-vulkan-render-bench
        binding "${GPU_BENCHMARK_BINDING_US}" 32 4 12 1
      )
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-upload
        gpu-native-vulkan-render-bench
        upload "${GPU_BENCHMARK_TRIANGLE_US}" 32 4 12 1
      )
    endif()

    if(GPU_BENCHMARK_BACKEND STREQUAL "vulkan" AND
       DEFINED GPU_VULKAN_ICD AND EXISTS "${GPU_VULKAN_ICD}")
      foreach(perfSmokeTest
              perf-smoke-queue-submit
              perf-smoke-queue-upload
              perf-smoke-texture-upload
              perf-smoke-static-scene
              perf-smoke-state-churn
              perf-smoke-binding-churn
              perf-smoke-bindless-update
              perf-smoke-bindless-dispatch
              perf-smoke-bindless-render
              perf-smoke-upload-heavy
              perf-smoke-pipeline-stress
              perf-smoke-native-vulkan-static
              perf-smoke-native-vulkan-state
              perf-smoke-native-vulkan-binding
              perf-smoke-native-vulkan-upload)
        set_property(TEST ${perfSmokeTest} APPEND PROPERTY ENVIRONMENT
          "VK_ICD_FILENAMES=${GPU_VULKAN_ICD}"
        )
      endforeach()
    endif()
  endif()

  if(WIN32)
    foreach(benchmark_target
            gpu-dispatch-bench
            gpu-queue-upload-bench
            gpu-queue-submit-bench
            gpu-texture-upload-bench
            gpu-static-scene-bench
            gpu-state-churn-bench
            gpu-binding-churn-bench
            gpu-bindless-update-bench
            gpu-bindless-dispatch-bench
            gpu-bindless-render-bench
            gpu-upload-heavy-bench
            gpu-pipeline-stress-bench
            gpu-validation-bench
            gpu-marker-bench)
      foreach(runtime_target us ds)
        if(TARGET ${runtime_target})
          add_custom_command(TARGET ${benchmark_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${runtime_target}>
                    $<TARGET_FILE_DIR:${benchmark_target}>
          )
        endif()
      endforeach()
    endforeach()
  endif()
endif()

install(TARGETS gpu
  EXPORT gpuTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${GPU_PUBLIC_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gpu
)
install(EXPORT gpuTargets
  NAMESPACE gpu::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gpu
)

configure_package_config_file(
  cmake/gpuConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/gpuConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gpu
  PATH_VARS CMAKE_INSTALL_LIBDIR
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/gpuConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMinorVersion
)
install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/gpuConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/gpuConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gpu
)
