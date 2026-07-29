cmake_minimum_required(VERSION 3.20)

project(gpu VERSION 0.1.0 LANGUAGES C)

if(PROJECT_VERSION_MAJOR EQUAL 0)
  set(GPU_SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
else()
  set(GPU_SOVERSION "${PROJECT_VERSION_MAJOR}")
endif()

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
option(GPU_BUILD_ANDROID_TESTS "Build Android device validation" OFF)
option(GPU_BUILD_ANDROID_SAMPLES "Build Android NativeActivity samples" OFF)
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
if(GPU_BUILD_ANDROID_TESTS OR GPU_BUILD_ANDROID_SAMPLES)
  if(NOT ANDROID)
    message(FATAL_ERROR
      "GPU_BUILD_ANDROID_TESTS and GPU_BUILD_ANDROID_SAMPLES require the "
      "Android NDK")
  endif()
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
set(GPU_CONFIG_HAS_WEBGPU_STATIC_PROVIDER OFF)
set(GPU_CONFIG_WEBGPU_STATIC_PROVIDER_FILENAME "")
set(GPU_INSTALL_TARGETS gpu)

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
  src/api/pass/blit.c
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
  SOVERSION ${GPU_SOVERSION}
)

if(APPLE)
  set_target_properties(gpu PROPERTIES
    OBJC_VISIBILITY_PRESET hidden
    INSTALL_RPATH "@loader_path"
  )
elseif(UNIX)
  set_target_properties(gpu PROPERTIES
    INSTALL_RPATH "\$ORIGIN"
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
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${GPU_PUBLIC_HEADERS}
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
  if(EMSCRIPTEN OR ANDROID)
    set(US_RUNTIME_ONLY ON CACHE BOOL "" FORCE)
  endif()
  if(NOT BUILD_SHARED_LIBS OR
     CMAKE_SYSTEM_NAME STREQUAL "iOS" OR
     EMSCRIPTEN)
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

if(GPU_BUILD_METAL OR (APPLE AND GPU_BUILD_WEBGPU))
  find_library(GPU_FRAMEWORK_METAL Metal REQUIRED)
  gpu_link_apple_framework(gpu Metal "${GPU_FRAMEWORK_METAL}")
endif()

if(GPU_BUILD_METAL)
  target_sources(gpu PRIVATE
    src/backend/mt/init.c
    src/backend/mt/impl/blit.m
    src/backend/mt/impl/instance.c
    src/backend/mt/impl/surface.c
    src/backend/mt/impl/buffer.m
    src/backend/mt/impl/cmd/cmdbuf.m
    src/backend/mt/impl/cmd/cmdque.m
    src/backend/mt/impl/compute.m
    src/backend/mt/impl/descriptor.m
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

  find_library(GPU_FRAMEWORK_METALKIT MetalKit REQUIRED)
  gpu_link_apple_framework(gpu MetalKit "${GPU_FRAMEWORK_METALKIT}")
endif()

if(APPLE AND (GPU_BUILD_VULKAN OR GPU_BUILD_WEBGPU))
  target_sources(gpu PRIVATE src/backend/surface_apple.m)
endif()

if(APPLE AND (GPU_BUILD_METAL OR GPU_BUILD_VULKAN OR GPU_BUILD_WEBGPU))
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
    src/backend/dx12/impl/blit.c
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
    src/backend/vk/impl/blit.c
    src/backend/vk/impl/buffer.c
    src/backend/vk/impl/cmdque.c
    src/backend/vk/impl/compute.c
    src/backend/vk/impl/descriptor.c
    src/backend/vk/impl/device.c
    src/backend/vk/impl/execution_graph.c
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
  target_link_libraries(gpu PRIVATE Vulkan::Vulkan)
  if(ANDROID)
    target_link_libraries(gpu PRIVATE log)
  endif()
endif()

if(GPU_BUILD_WEBGPU)
  target_compile_definitions(gpu PRIVATE GPU_ENABLE_WEBGPU=1)
  target_sources(gpu PRIVATE
    src/backend/webgpu/init.c
    src/backend/webgpu/impl/blit.c
    src/backend/webgpu/impl/device.c
    src/backend/webgpu/impl/instance.c
    src/backend/webgpu/impl/surface.c
    src/backend/webgpu/impl/swapchain.c
    src/backend/webgpu/impl/frame.c
    src/backend/webgpu/impl/cmdque.c
    src/backend/webgpu/impl/completion.c
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
    target_compile_definitions(gpu PRIVATE
      GPU_WEBGPU_PROVIDER_DAWN=1
      GPU_WEBGPU_PROVIDER_WGPU_NATIVE=0
    )
    target_compile_options(gpu PRIVATE --use-port=emdawnwebgpu)
    target_link_options(gpu PRIVATE --use-port=emdawnwebgpu)
  else()
    set(GPU_WEBGPU_PROVIDER "AUTO" CACHE STRING
        "Native WebGPU provider: AUTO, DAWN, or WGPU_NATIVE")
    set_property(CACHE GPU_WEBGPU_PROVIDER PROPERTY STRINGS
                 AUTO DAWN WGPU_NATIVE)
    set(GPU_WEBGPU_INCLUDE_DIR "" CACHE PATH
        "Directory containing webgpu/webgpu.h")
    set(GPU_WEBGPU_LIBRARY "" CACHE FILEPATH
        "Native Dawn or wgpu-native WebGPU library")
    set(GPU_WEBGPU_RUNTIME_LIBRARY "" CACHE FILEPATH
        "Native WebGPU provider runtime library installed with gpu")
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
    string(TOUPPER "${GPU_WEBGPU_PROVIDER}" _gpu_webgpu_provider)
    if(_gpu_webgpu_provider STREQUAL "AUTO")
      if(EXISTS "${GPU_WEBGPU_INCLUDE_DIR}/webgpu/wgpu.h")
        set(_gpu_webgpu_provider "WGPU_NATIVE")
      else()
        set(_gpu_webgpu_provider "DAWN")
      endif()
    endif()
    if(_gpu_webgpu_provider STREQUAL "DAWN")
      target_compile_definitions(gpu PRIVATE
        GPU_WEBGPU_PROVIDER_DAWN=1
        GPU_WEBGPU_PROVIDER_WGPU_NATIVE=0
      )
    elseif(_gpu_webgpu_provider STREQUAL "WGPU_NATIVE")
      if(NOT EXISTS "${GPU_WEBGPU_INCLUDE_DIR}/webgpu/wgpu.h")
        message(FATAL_ERROR
          "GPU_WEBGPU_PROVIDER=WGPU_NATIVE requires webgpu/wgpu.h.")
      endif()
      target_compile_definitions(gpu PRIVATE
        GPU_WEBGPU_PROVIDER_DAWN=0
        GPU_WEBGPU_PROVIDER_WGPU_NATIVE=1
      )
    else()
      message(FATAL_ERROR
        "GPU_WEBGPU_PROVIDER must be AUTO, DAWN, or WGPU_NATIVE.")
    endif()
    message(STATUS "GPU native WebGPU provider: ${_gpu_webgpu_provider}")
    target_include_directories(gpu PRIVATE "${GPU_WEBGPU_INCLUDE_DIR}")
    if(BUILD_SHARED_LIBS)
      target_link_libraries(gpu PRIVATE "${GPU_WEBGPU_LIBRARY}")
    else()
      add_library(gpu_webgpu_provider INTERFACE)
      set_target_properties(gpu_webgpu_provider PROPERTIES
        EXPORT_NAME webgpu_provider
      )
      target_link_libraries(gpu_webgpu_provider INTERFACE
        "$<BUILD_INTERFACE:${GPU_WEBGPU_LIBRARY}>"
      )
      target_link_libraries(gpu PRIVATE gpu_webgpu_provider)
      list(APPEND GPU_INSTALL_TARGETS gpu_webgpu_provider)

      get_filename_component(
        GPU_CONFIG_WEBGPU_STATIC_PROVIDER_FILENAME
        "${GPU_WEBGPU_LIBRARY}"
        NAME
      )
      set(GPU_CONFIG_HAS_WEBGPU_STATIC_PROVIDER ON)
      set(_gpu_webgpu_static_provider_library "${GPU_WEBGPU_LIBRARY}")
    endif()

    set(_gpu_webgpu_runtime_library "${GPU_WEBGPU_RUNTIME_LIBRARY}")
    if(NOT _gpu_webgpu_runtime_library)
      if((APPLE AND GPU_WEBGPU_LIBRARY MATCHES "\\.dylib$") OR
         (UNIX AND NOT APPLE AND
          GPU_WEBGPU_LIBRARY MATCHES "\\.so(\\.[0-9.]+)?$"))
        set(_gpu_webgpu_runtime_library "${GPU_WEBGPU_LIBRARY}")
      endif()
    endif()
  endif()
endif()


if((GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES OR GPU_BUILD_BENCHMARKS) AND
   NOT EMSCRIPTEN)
  include(cmake/USLFixtures.cmake)
endif()

if(GPU_BUILD_TESTS OR GPU_BUILD_BENCHMARKS)
  enable_testing()
endif()

if(GPU_BUILD_SAMPLES OR GPU_BUILD_ANDROID_SAMPLES)
  add_subdirectory(samples)
endif()

if(GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES OR GPU_BUILD_ANDROID_TESTS)
  add_subdirectory(tests)
endif()

if(GPU_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

install(TARGETS ${GPU_INSTALL_TARGETS}
  EXPORT gpuTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(GPU_BUILD_WEBGPU AND _gpu_webgpu_static_provider_library)
  install(FILES "${_gpu_webgpu_static_provider_library}"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()
if(GPU_BUILD_WEBGPU AND _gpu_webgpu_runtime_library)
  if(NOT EXISTS "${_gpu_webgpu_runtime_library}")
    message(FATAL_ERROR
      "GPU_WEBGPU_RUNTIME_LIBRARY does not exist: "
      "${_gpu_webgpu_runtime_library}")
  endif()
  if(WIN32)
    install(FILES "${_gpu_webgpu_runtime_library}"
      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  else()
    install(FILES "${_gpu_webgpu_runtime_library}"
      DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
  endif()
endif()
install(FILES ${GPU_PUBLIC_HEADERS}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gpu
)
install(FILES LICENSE
  DESTINATION ${CMAKE_INSTALL_DATADIR}/gpu
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

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)
