cmake_minimum_required(VERSION 3.20)

if(APPLE)
  project(gpu VERSION 0.1.0 LANGUAGES C OBJC)
else()
  project(gpu VERSION 0.1.0 LANGUAGES C)
endif()

include(GNUInstallDirs)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(GPU_ENABLE_VULKAN "Build the Vulkan backend" OFF)
option(GPU_BUILD_TESTS "Build GPU validation tests" OFF)
option(GPU_BUILD_SAMPLES "Build GPU samples" OFF)
option(GPU_BUILD_BENCHMARKS "Build GPU microbenchmarks" OFF)
option(GPU_BUILD_WITH_VALIDATION "Build validation and verbose diagnostics" ON)
option(GPU_BUILD_WITH_DEBUG_MARKERS "Build debug marker support" ON)
option(GPU_BACKEND_METAL_ONLY "Build only the Metal backend" OFF)
option(GPU_BACKEND_VULKAN_ONLY "Build only the Vulkan backend" OFF)
option(GPU_BACKEND_DX12_ONLY "Build only the Direct3D 12 backend" OFF)

set(GPU_SINGLE_BACKEND_COUNT 0)
foreach(_gpu_backend_only
        GPU_BACKEND_METAL_ONLY
        GPU_BACKEND_VULKAN_ONLY
        GPU_BACKEND_DX12_ONLY)
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

set(GPU_BUILD_METAL OFF)
set(GPU_BUILD_VULKAN OFF)
set(GPU_BUILD_DX12 OFF)

if(GPU_BACKEND_METAL_ONLY)
  set(GPU_BUILD_METAL ON)
elseif(GPU_BACKEND_VULKAN_ONLY)
  set(GPU_BUILD_VULKAN ON)
elseif(GPU_BACKEND_DX12_ONLY)
  set(GPU_BUILD_DX12 ON)
else()
  if(APPLE)
    set(GPU_BUILD_METAL ON)
  elseif(WIN32)
    set(GPU_BUILD_DX12 ON)
  endif()
  if(GPU_ENABLE_VULKAN)
    set(GPU_BUILD_VULKAN ON)
  endif()
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
  src/api/format.c
  src/api/frame.c
  src/api/instance.c
  src/api/library.c
  src/api/pass/renderpass.c
  src/api/pipeline_cache.c
  src/api/query.c
  src/api/render/pipeline.c
  src/api/render/rce.c
  src/api/sampler.c
  src/api/surface.c
  src/api/swapchain.c
  src/api/texture.c
  src/api/vertex.c
  src/api/vrs.c
  src/backend/backend.c
)

add_library(gpu ${GPU_CORE_SOURCES})
add_library(gpu::gpu ALIAS gpu)

target_compile_definitions(gpu PRIVATE
  GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  GPU_BUILD_WITH_DEBUG_MARKERS=$<BOOL:${GPU_BUILD_WITH_DEBUG_MARKERS}>
  GPU_BACKEND_METAL_ONLY=$<BOOL:${GPU_BACKEND_METAL_ONLY}>
  GPU_BACKEND_VULKAN_ONLY=$<BOOL:${GPU_BACKEND_VULKAN_ONLY}>
  GPU_BACKEND_DX12_ONLY=$<BOOL:${GPU_BACKEND_DX12_ONLY}>
)

if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(gpu PRIVATE Threads::Threads)
endif()

set_target_properties(gpu PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
  POSITION_INDEPENDENT_CODE ON
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

target_include_directories(gpu
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
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
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
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
    src/backend/mt/impl/pipeline_cache.m
    src/backend/mt/impl/render/pipeline.m
    src/backend/mt/impl/render/rce.m
    src/backend/mt/impl/renderpass.m
    src/backend/mt/impl/swapchain.m
    src/backend/mt/impl/vertex.m
    src/backend/mt/impl/vrs.m
  )

  find_library(GPU_FRAMEWORK_METAL Metal REQUIRED)
  find_library(GPU_FRAMEWORK_METALKIT MetalKit REQUIRED)
  target_link_libraries(gpu PRIVATE
    ${GPU_FRAMEWORK_METAL}
    ${GPU_FRAMEWORK_METALKIT}
  )
endif()

if(APPLE AND (GPU_BUILD_METAL OR GPU_BUILD_VULKAN))
  find_library(GPU_FRAMEWORK_FOUNDATION Foundation REQUIRED)
  find_library(GPU_FRAMEWORK_QUARTZCORE QuartzCore REQUIRED)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_library(GPU_FRAMEWORK_UI UIKit REQUIRED)
  else()
    find_library(GPU_FRAMEWORK_UI AppKit REQUIRED)
  endif()

  target_link_libraries(gpu PRIVATE
    ${GPU_FRAMEWORK_FOUNDATION}
    ${GPU_FRAMEWORK_QUARTZCORE}
    ${GPU_FRAMEWORK_UI}
  )
endif()

if(GPU_BUILD_DX12)
  target_sources(gpu PRIVATE
    src/backend/dx12/init.c
    src/backend/dx12/impl/buffer.c
    src/backend/dx12/impl/cmdque.c
    src/backend/dx12/impl/compute.c
    src/backend/dx12/impl/descr/descpool.c
    src/backend/dx12/impl/device.c
    src/backend/dx12/impl/format.c
    src/backend/dx12/impl/frame.c
    src/backend/dx12/impl/instance.c
    src/backend/dx12/impl/library.c
    src/backend/dx12/impl/pipeline.c
    src/backend/dx12/impl/pipeline_cache.c
    src/backend/dx12/impl/query.c
    src/backend/dx12/impl/renderpass.c
    src/backend/dx12/impl/rce.c
    src/backend/dx12/impl/sampler.c
    src/backend/dx12/impl/surface.c
    src/backend/dx12/impl/swapchain.c
    src/backend/dx12/impl/texture.c
    src/backend/dx12/impl/vrs.c
  )
  target_link_libraries(gpu PRIVATE d3d12 d3dcompiler dxgi dxguid)
endif()

if(GPU_BUILD_VULKAN)
  find_package(Vulkan REQUIRED)
  target_compile_definitions(gpu PRIVATE GPU_ENABLE_VULKAN=1)
  target_sources(gpu PRIVATE
    src/backend/vk/init.c
    src/backend/vk/impl/buffer.c
    src/backend/vk/impl/cmdque.c
    src/backend/vk/impl/compute.c
    src/backend/vk/impl/descriptor.c
    src/backend/vk/impl/device.c
    src/backend/vk/impl/display.c
    src/backend/vk/impl/format.c
    src/backend/vk/impl/frame.c
    src/backend/vk/impl/instance.c
    src/backend/vk/impl/library.c
    src/backend/vk/impl/pipeline.c
    src/backend/vk/impl/pipeline_cache.c
    src/backend/vk/impl/query.c
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

if(GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES OR GPU_BUILD_BENCHMARKS)
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
      foreach(runtimeTarget us ds)
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
      foreach(runtimeTarget us ds)
        if(TARGET ${runtimeTarget})
          add_dependencies(${artifactTarget} ${runtimeTarget})
        endif()
      endforeach()
    endif()
  endfunction()
endif()

if(GPU_BUILD_TESTS)
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
  set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,shader_f16)
  gpu_add_usl_fixtures(
    GPU_API_SHADER_F16_USL_FIXTURE
    vulkan
    api
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/shader_f16.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES ${GPU_API_SHADER_F16_USL_FIXTURE})
  add_custom_target(gpu-api-fixtures DEPENDS ${GPU_API_USL_FIXTURES})

  if(GPU_BUILD_VULKAN)
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
  )
  list(GET GPU_DX12_USL_FIXTURES 0 GPU_DX12_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 1 GPU_DX12_TEXTURED_QUAD_US)
  list(GET GPU_DX12_USL_FIXTURES 2 GPU_DX12_COMPUTE_BUFFER_US)
  list(GET GPU_DX12_USL_FIXTURES 3 GPU_DX12_MESH_TRIANGLE_US)
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
  )
  list(GET GPU_VULKAN_USL_FIXTURES 0 GPU_VULKAN_TRIANGLE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 1 GPU_VULKAN_TEXTURED_QUAD_US)
  list(GET GPU_VULKAN_USL_FIXTURES 2 GPU_VULKAN_COMPUTE_BUFFER_US)
  list(GET GPU_VULKAN_USL_FIXTURES 3 GPU_VULKAN_MESH_TRIANGLE_US)
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
  )
  list(GET GPU_METAL_USL_FIXTURES 0 GPU_METAL_TRIANGLE_US)
  list(GET GPU_METAL_USL_FIXTURES 1 GPU_METAL_TEXTURED_QUAD_US)
  list(GET GPU_METAL_USL_FIXTURES 2 GPU_METAL_COMPUTE_US)
  list(GET GPU_METAL_USL_FIXTURES 3 GPU_METAL_COMPUTE_BUFFER_US)
  list(GET GPU_METAL_USL_FIXTURES 4 GPU_METAL_REFLECTION_US)
  list(GET GPU_METAL_USL_FIXTURES 5 GPU_METAL_REFLECTION_STORAGE_US)
  list(GET GPU_METAL_USL_FIXTURES 6 GPU_METAL_MESH_TRIANGLE_US)
  add_custom_target(gpu-metal-fixtures DEPENDS ${GPU_METAL_USL_FIXTURES})
endif()

if(GPU_BUILD_BENCHMARKS)
  set(GPU_BENCHMARK_USL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/render_mrt.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/triangle-usl/triangle.usl"
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
  file(GLOB GPU_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/gpu/*.h"
  )
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

  add_executable(gpu-api-test
    tests/api/barrier.c
    tests/api/bindgroup.c
    tests/api/compute.c
    tests/api/coordinate.c
    tests/api/copy.c
    tests/api/cube_texture.c
    tests/api/descriptor_array.c
    tests/api/line_texture.c
    tests/api/main.c
    tests/api/metal_vertex_slots.c
    tests/api/query.c
    tests/api/queue.c
    tests/api/render.c
    tests/api/resources.c
    tests/api/runner.c
    tests/api/runtime.c
    tests/api/sampler.c
    tests/api/shader.c
    tests/api/shader_f16.c
    tests/api/shader_sampler.c
    tests/api/storage_texture.c
    tests/api/subgroup.c
    tests/api/texture_integer_clear.c
    tests/api/texture_transfer.c
    tests/api/texture_view_render.c
    tests/api/threading.c
    tests/api/volume_texture.c
    tests/api/vrs.c
  )
  target_link_libraries(gpu-api-test PRIVATE gpu)
  add_dependencies(gpu-api-test gpu-api-fixtures)
  if(TARGET gpu-api-vulkan-fixtures)
    add_dependencies(gpu-api-test gpu-api-vulkan-fixtures)
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

  if(GPU_BUILD_VULKAN)
    list(GET GPU_API_VULKAN_USL_FIXTURES 1 GPU_VULKAN_RENDER_MRT_US)

    add_test(NAME api-validation-vulkan
      COMMAND gpu-api-test
              ${GPU_API_VULKAN_USL_FIXTURES}
              vulkan
    )

    add_executable(gpu-vulkan-queue-test
      tests/vulkan/queue.c
    )
    target_include_directories(gpu-vulkan-queue-test PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(gpu-vulkan-queue-test PRIVATE gpu)
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
    target_link_libraries(gpu-vulkan-shader-test PRIVATE gpu)
    add_dependencies(gpu-vulkan-shader-test gpu-vulkan-fixtures)
    set_target_properties(gpu-vulkan-shader-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_executable(gpu-vulkan-mesh-test
      tests/api/mesh.c
    )
    target_link_libraries(gpu-vulkan-mesh-test PRIVATE gpu)
    add_dependencies(gpu-vulkan-mesh-test gpu-vulkan-fixtures)
    set_target_properties(gpu-vulkan-mesh-test PROPERTIES
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
      NAME dx12-compute-buffer
      COMMAND gpu-compute-buffer-dx12-usl
              "${GPU_DX12_COMPUTE_BUFFER_US}"
    )
    add_test(NAME dx12-triangle
             COMMAND gpu-triangle-dx12-usl)
    add_test(NAME dx12-textured-quad
             COMMAND gpu-textured-quad-dx12-usl)
    add_test(NAME dx12-compute-render
             COMMAND gpu-compute-render-dx12-usl)
    set_tests_properties(
      dx12-triangle
      dx12-textured-quad
      dx12-compute-render
      PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 30
    )
    set_property(
      TEST dx12-triangle dx12-textured-quad dx12-compute-render
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

  if(GPU_BUILD_TESTS)
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
      add_test(NAME vulkan-mesh-triangle
               COMMAND gpu-mesh-triangle-vulkan-usl)
      add_test(NAME vulkan-compute-render
               COMMAND gpu-compute-render-vulkan-usl)
      add_test(NAME vulkan-resize-lifecycle
               COMMAND gpu-triangle-vulkan-usl)
      set_tests_properties(
        vulkan-triangle
        vulkan-textured-quad
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
  get_filename_component(GPU_VULKAN_LIBRARY_DIR "${Vulkan_LIBRARY}" DIRECTORY)
  get_filename_component(GPU_VULKAN_PLATFORM_DIR
                         "${GPU_VULKAN_LIBRARY_DIR}"
                         DIRECTORY)
  set(GPU_VULKAN_ICD
    "${GPU_VULKAN_PLATFORM_DIR}/share/vulkan/icd.d/MoltenVK_icd.json"
  )
  if(EXISTS "${GPU_VULKAN_ICD}")
    foreach(vulkanTest
            api-validation-vulkan
            vulkan-queue
            vulkan-shader
            vulkan-mesh
            vulkan-compute-buffer
            vulkan-triangle
            vulkan-textured-quad
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
      ${GPU_BENCHMARK_BACKEND} 4 8 1
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
      "${GPU_BENCHMARK_TRIANGLE_US}"
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
              perf-smoke-pipeline-stress)
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
install(DIRECTORY include/gpu DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT gpuTargets
  NAMESPACE gpu::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gpu
)
