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
set(GPU_BUILD_OPENGL OFF)

if(GPU_BACKEND_METAL_ONLY)
  set(GPU_BUILD_METAL ON)
elseif(GPU_BACKEND_VULKAN_ONLY)
  set(GPU_BUILD_VULKAN ON)
elseif(GPU_BACKEND_DX12_ONLY)
  set(GPU_BUILD_DX12 ON)
else()
  set(GPU_BUILD_OPENGL ON)
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

if(GPU_BUILD_OPENGL)
  target_sources(gpu PRIVATE
    src/backend/gl/api.c
    src/backend/gl/impl/device.c
    src/backend/gl/impl/render/pipeline.c
  )
endif()

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
    src/backend/mt/impl/render/pipeline.m
    src/backend/mt/impl/render/rce.m
    src/backend/mt/impl/renderpass.m
    src/backend/mt/impl/swapchain.m
    src/backend/mt/impl/vertex.m
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
    src/backend/dx12/impl/query.c
    src/backend/dx12/impl/renderpass.c
    src/backend/dx12/impl/rce.c
    src/backend/dx12/impl/sampler.c
    src/backend/dx12/impl/surface.c
    src/backend/dx12/impl/swapchain.c
    src/backend/dx12/impl/texture.c
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
    src/backend/vk/impl/query.c
    src/backend/vk/impl/rce.c
    src/backend/vk/impl/renderpass.c
    src/backend/vk/impl/sampler.c
    src/backend/vk/impl/surface.c
    src/backend/vk/impl/swapchain.c
    src/backend/vk/impl/texture.c
  )
  if(GPU_BUILD_WITH_VALIDATION)
    target_sources(gpu PRIVATE src/backend/vk/impl/debug.c)
  endif()
  if(APPLE)
    target_sources(gpu PRIVATE src/backend/vk/impl/surface_apple.m)
  endif()
  target_link_libraries(gpu PRIVATE Vulkan::Vulkan)
endif()

if(GPU_BUILD_TESTS OR (GPU_BUILD_SAMPLES AND GPU_BUILD_DX12))
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
  )
  list(GET GPU_DX12_USL_FIXTURES 0 GPU_DX12_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 1 GPU_DX12_TEXTURED_QUAD_US)
  list(GET GPU_DX12_USL_FIXTURES 2 GPU_DX12_COMPUTE_BUFFER_US)
  add_custom_target(gpu-dx12-fixtures DEPENDS ${GPU_DX12_USL_FIXTURES})
endif()

if(GPU_BUILD_TESTS)
  enable_testing()

  add_executable(gpu-api-test
    tests/api/barrier.c
    tests/api/bindgroup.c
    tests/api/compute.c
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
    tests/api/shader_sampler.c
    tests/api/storage_texture.c
    tests/api/texture_integer_clear.c
    tests/api/texture_transfer.c
    tests/api/texture_view_render.c
    tests/api/threading.c
    tests/api/volume_texture.c
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
    add_test(NAME api-validation-vulkan
      COMMAND gpu-api-test
              ${GPU_API_VULKAN_USL_FIXTURES}
              vulkan
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
  endif()
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_DX12)
  add_executable(gpu-triangle-dx12-usl
    samples/triangle-dx12-usl/main.c
  )
  target_link_libraries(gpu-triangle-dx12-usl PRIVATE gpu user32)
  add_dependencies(gpu-triangle-dx12-usl gpu-dx12-fixtures)
  set_target_properties(gpu-triangle-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-triangle-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-triangle-dx12-usl>
      )
    endif()
  endforeach()

  add_custom_command(TARGET gpu-triangle-dx12-usl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_TRIANGLE_US}"
            $<TARGET_FILE_DIR:gpu-triangle-dx12-usl>
  )

  add_executable(gpu-textured-quad-dx12-usl
    samples/textured-quad-dx12-usl/main.c
  )
  target_link_libraries(gpu-textured-quad-dx12-usl PRIVATE gpu user32)
  add_dependencies(gpu-textured-quad-dx12-usl gpu-dx12-fixtures)
  set_target_properties(gpu-textured-quad-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-textured-quad-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-textured-quad-dx12-usl>
      )
    endif()
  endforeach()

  add_custom_command(TARGET gpu-textured-quad-dx12-usl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_TEXTURED_QUAD_US}"
            $<TARGET_FILE_DIR:gpu-textured-quad-dx12-usl>
  )

  add_executable(gpu-compute-buffer-dx12-usl
    samples/compute-buffer-dx12-usl/main.c
  )
  target_link_libraries(gpu-compute-buffer-dx12-usl PRIVATE gpu)
  add_dependencies(gpu-compute-buffer-dx12-usl gpu-dx12-fixtures)
  set_target_properties(gpu-compute-buffer-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-compute-buffer-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-compute-buffer-dx12-usl>
      )
    endif()
  endforeach()

  add_custom_command(TARGET gpu-compute-buffer-dx12-usl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_COMPUTE_BUFFER_US}"
            $<TARGET_FILE_DIR:gpu-compute-buffer-dx12-usl>
  )

  add_executable(gpu-compute-render-dx12-usl
    samples/compute-buffer-dx12-usl/window.c
  )
  target_link_libraries(gpu-compute-render-dx12-usl PRIVATE gpu user32)
  add_dependencies(gpu-compute-render-dx12-usl gpu-dx12-fixtures)
  set_target_properties(gpu-compute-render-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  foreach(runtime_target us ds)
    if(TARGET ${runtime_target})
      add_custom_command(TARGET gpu-compute-render-dx12-usl POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtime_target}>
                $<TARGET_FILE_DIR:gpu-compute-render-dx12-usl>
      )
    endif()
  endforeach()

  add_custom_command(TARGET gpu-compute-render-dx12-usl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_COMPUTE_BUFFER_US}"
            $<TARGET_FILE_DIR:gpu-compute-render-dx12-usl>
  )

  if(GPU_BUILD_TESTS)
    add_test(
      NAME dx12-compute-buffer
      COMMAND gpu-compute-buffer-dx12-usl
              "${GPU_DX12_COMPUTE_BUFFER_US}"
    )
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

  if(WIN32)
    foreach(benchmark_target
            gpu-dispatch-bench
            gpu-queue-upload-bench
            gpu-texture-upload-bench
            gpu-static-scene-bench
            gpu-state-churn-bench
            gpu-binding-churn-bench
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
