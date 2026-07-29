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

if(GPU_BUILD_ANDROID_TESTS)
  add_executable(gpu-android-vulkan-test
    tests/android/vulkan.c
    tests/api/copy.c
    tests/api/texture_transfer.c
  )
  target_include_directories(gpu-android-vulkan-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_link_libraries(gpu-android-vulkan-test PRIVATE
    gpu
    Vulkan::Vulkan
    log
  )
  set_target_properties(gpu-android-vulkan-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  target_compile_options(gpu-android-vulkan-test PRIVATE
    -Wall
    -Wextra
    -Werror
  )
endif()

if(GPU_BUILD_ANDROID_SAMPLES)
  if(BUILD_SHARED_LIBS)
    message(FATAL_ERROR
      "GPU_BUILD_ANDROID_SAMPLES requires BUILD_SHARED_LIBS=OFF so each APK "
      "contains one self-contained native library")
  endif()

  set(GPU_USL_HOST_FIXTURE "" CACHE FILEPATH
      "Host gpu-usl-fixture executable used for Android samples")
  set(GPU_USL_HOST_PACKER "" CACHE FILEPATH
      "Host uslpack executable used for Android samples")
  set(GPU_ANDROID_SDK_ROOT "$ENV{ANDROID_SDK_ROOT}" CACHE PATH
      "Android SDK root used to package NativeActivity samples")
  set(GPU_ANDROID_DEBUG_KEYSTORE
      "$ENV{HOME}/.android/debug.keystore"
      CACHE FILEPATH "Android debug keystore used to sign sample APKs")

  if(NOT GPU_USL_HOST_FIXTURE)
    set(_gpu_usl_host_fixture
        "${CMAKE_CURRENT_SOURCE_DIR}/out/build/release-check/gpu-usl-fixture")
    if(EXISTS "${_gpu_usl_host_fixture}")
      set(GPU_USL_HOST_FIXTURE "${_gpu_usl_host_fixture}")
    endif()
  endif()
  if(NOT GPU_USL_HOST_PACKER)
    set(_gpu_usl_host_packer
        "${CMAKE_CURRENT_SOURCE_DIR}/out/build/release-check/uslpack")
    if(EXISTS "${_gpu_usl_host_packer}")
      set(GPU_USL_HOST_PACKER "${_gpu_usl_host_packer}")
    endif()
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_FIXTURE}")
    message(FATAL_ERROR
      "Android samples require a host gpu-usl-fixture. Set "
      "GPU_USL_HOST_FIXTURE.")
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_PACKER}")
    message(FATAL_ERROR
      "Android samples require a host uslpack. Set GPU_USL_HOST_PACKER.")
  endif()
  if(NOT GPU_ANDROID_SDK_ROOT)
    set(GPU_ANDROID_SDK_ROOT "$ENV{ANDROID_HOME}")
  endif()
  if(NOT GPU_ANDROID_SDK_ROOT)
    foreach(_gpu_android_sdk_candidate
            "$ENV{HOME}/Library/Developer/Android/sdk"
            "$ENV{HOME}/Android/Sdk")
      if(EXISTS "${_gpu_android_sdk_candidate}")
        set(GPU_ANDROID_SDK_ROOT "${_gpu_android_sdk_candidate}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT EXISTS "${GPU_ANDROID_SDK_ROOT}")
    message(FATAL_ERROR
      "Android samples require ANDROID_SDK_ROOT or GPU_ANDROID_SDK_ROOT.")
  endif()
  if(NOT EXISTS "${GPU_ANDROID_DEBUG_KEYSTORE}")
    message(FATAL_ERROR
      "Android debug keystore not found. Set GPU_ANDROID_DEBUG_KEYSTORE.")
  endif()

  file(GLOB _gpu_android_build_tools LIST_DIRECTORIES true
       "${GPU_ANDROID_SDK_ROOT}/build-tools/*")
  if(NOT _gpu_android_build_tools)
    message(FATAL_ERROR
      "Android SDK build-tools are not installed under "
      "${GPU_ANDROID_SDK_ROOT}.")
  endif()
  list(SORT _gpu_android_build_tools COMPARE NATURAL ORDER DESCENDING)
  list(GET _gpu_android_build_tools 0 _gpu_android_build_tools_dir)
  file(GLOB _gpu_android_platform_jars
       "${GPU_ANDROID_SDK_ROOT}/platforms/android-*/android.jar")
  if(NOT _gpu_android_platform_jars)
    message(FATAL_ERROR
      "No Android SDK platform is installed under ${GPU_ANDROID_SDK_ROOT}.")
  endif()
  list(SORT _gpu_android_platform_jars COMPARE NATURAL ORDER DESCENDING)
  list(GET _gpu_android_platform_jars 0 GPU_ANDROID_PLATFORM_JAR)

  set(GPU_ANDROID_AAPT2
      "${_gpu_android_build_tools_dir}/aapt2")
  set(GPU_ANDROID_ZIPALIGN
      "${_gpu_android_build_tools_dir}/zipalign")
  set(GPU_ANDROID_APKSIGNER
      "${_gpu_android_build_tools_dir}/apksigner")
  set(GPU_ANDROID_D8
      "${_gpu_android_build_tools_dir}/d8")
  foreach(_gpu_android_tool
          GPU_ANDROID_AAPT2
          GPU_ANDROID_ZIPALIGN
          GPU_ANDROID_APKSIGNER
          GPU_ANDROID_D8
          GPU_ANDROID_PLATFORM_JAR)
    if(NOT EXISTS "${${_gpu_android_tool}}")
      message(FATAL_ERROR
        "Android packaging tool not found: ${_gpu_android_tool}")
    endif()
  endforeach()
  find_program(GPU_ANDROID_ZIP NAMES zip REQUIRED)
  find_program(GPU_ANDROID_JAVAC NAMES javac REQUIRED)
  find_program(GPU_ANDROID_JAR NAMES jar REQUIRED)

  set(GPU_ANDROID_GLUE_DIR
      "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
  add_library(gpu-android-native-app-glue STATIC
    "${GPU_ANDROID_GLUE_DIR}/android_native_app_glue.c"
  )
  target_include_directories(gpu-android-native-app-glue PUBLIC
    "${GPU_ANDROID_GLUE_DIR}"
  )
  target_link_libraries(gpu-android-native-app-glue PUBLIC android log)

  find_path(GPU_CGLM_INCLUDE_DIR
    NAMES cglm/cglm.h
    HINTS
      "${CMAKE_CURRENT_SOURCE_DIR}/../glm/include"
      /opt/homebrew/include
    NO_CMAKE_FIND_ROOT_PATH
  )
  if(NOT GPU_CGLM_INCLUDE_DIR)
    message(FATAL_ERROR
      "Android samples require cglm headers. Set GPU_CGLM_INCLUDE_DIR.")
  endif()

  add_library(gpu-android-sample-common STATIC
    samples/common/android.c
  )
  target_include_directories(gpu-android-sample-common PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/common"
    "${GPU_ANDROID_GLUE_DIR}"
  )
  target_link_libraries(gpu-android-sample-common PUBLIC
    gpu
    gpu-android-native-app-glue
    android
    log
  )
  target_compile_options(gpu-android-sample-common PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-android-sample-common PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  function(gpu_add_android_usl_artifact target shader_source out_dir out_us)
    set(options USES_STDLIB)
    set(oneValueArgs CAPS FALLBACK_CAPS)
    cmake_parse_arguments(GPU_ANDROID_SHADER
      "${options}" "${oneValueArgs}" "" ${ARGN})

    get_filename_component(_gpu_android_shader_name
                           "${shader_source}" NAME_WE)
    get_filename_component(_gpu_android_shader_source
                           "${shader_source}" ABSOLUTE
                           BASE_DIR "${PROJECT_SOURCE_DIR}")

    set(_gpu_android_dir
        "${CMAKE_CURRENT_BINARY_DIR}/android/${target}")
    set(_gpu_android_usl
        "${_gpu_android_dir}/${_gpu_android_shader_name}.usl")
    set(_gpu_android_us
        "${_gpu_android_dir}/${_gpu_android_shader_name}.us")
    set(_gpu_android_environment
        USL_EMIT_BYTECODE=1)
    if(GPU_ANDROID_SHADER_USES_STDLIB)
      list(APPEND _gpu_android_environment
        "USL_STDLIB_PATH=${GPU_USL_ROOT}/stdlib")
    endif()
    if(GPU_ANDROID_SHADER_CAPS)
      list(APPEND _gpu_android_environment
        "USL_TARGET_CAPS=${GPU_ANDROID_SHADER_CAPS}")
    endif()
    set(_gpu_android_fallback_pack_command)
    if(GPU_ANDROID_SHADER_FALLBACK_CAPS)
      list(APPEND _gpu_android_fallback_pack_command
        COMMAND "${CMAKE_COMMAND}"
                "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
                "-DGPU_USL_SOURCE=${_gpu_android_usl}"
                -DGPU_USL_TARGET=spirv
                -DGPU_USL_TARGET_PROFILE=vulkan1.1
                "-DGPU_USL_CAPS=${GPU_ANDROID_SHADER_FALLBACK_CAPS}"
                -P "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
      )
    endif()

    add_custom_command(
      OUTPUT "${_gpu_android_us}"
      BYPRODUCTS "${_gpu_android_usl}.spvasm"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_gpu_android_dir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_gpu_android_shader_source}"
              "${_gpu_android_usl}"
      COMMAND ${CMAKE_COMMAND} -E env
              ${_gpu_android_environment}
              "${GPU_USL_HOST_FIXTURE}"
              vulkan
              "${_gpu_android_usl}"
      COMMAND "${CMAKE_COMMAND}"
              "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
              "-DGPU_USL_SOURCE=${_gpu_android_usl}"
              -DGPU_USL_TARGET=spirv
              -DGPU_USL_TARGET_PROFILE=vulkan1.1
              "-DGPU_USL_CAPS=${GPU_ANDROID_SHADER_CAPS}"
              -P "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
      ${_gpu_android_fallback_pack_command}
      DEPENDS
        "${GPU_USL_HOST_FIXTURE}"
        "${GPU_USL_HOST_PACKER}"
        "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
        "${_gpu_android_shader_source}"
      VERBATIM
    )
    add_custom_target(${target}-artifact DEPENDS "${_gpu_android_us}")
    set(${out_dir} "${_gpu_android_dir}" PARENT_SCOPE)
    set(${out_us} "${_gpu_android_us}" PARENT_SCOPE)
  endfunction()

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

  function(gpu_attach_usl_validation target fixtureTarget)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
        "${PROJECT_BINARY_DIR}/validation/${target}"
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
      "The cglm-based samples require cglm headers. Set "
      "GPU_CGLM_INCLUDE_DIR.")
  endif()
endif()

if(GPU_BUILD_SAMPLES OR GPU_BUILD_ANDROID_SAMPLES)
  add_subdirectory(samples)
endif()

if(GPU_BUILD_TESTS)
  get_property(GPU_GENERATOR_IS_MULTI_CONFIG GLOBAL
               PROPERTY GENERATOR_IS_MULTI_CONFIG)

  set(GPU_API_USL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/usl-reflection-check/reflection.usl"
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
  elseif(GPU_BUILD_WEBGPU)
    set(GPU_API_DEFAULT_BACKEND webgpu)
  else()
    set(GPU_API_DEFAULT_BACKEND vulkan)
  endif()
  gpu_add_usl_fixtures(
    GPU_API_USL_FIXTURES
    "${GPU_API_DEFAULT_BACKEND}"
    api
    ${GPU_API_USL_SOURCES}
  )
  gpu_add_usl_fixtures(
    GPU_API_MSAA_USL_FIXTURE
    "${GPU_API_DEFAULT_BACKEND}"
    api-msaa
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/msaa.usl"
  )
  add_custom_target(
    gpu-api-msaa-fixture
    DEPENDS ${GPU_API_MSAA_USL_FIXTURE}
  )
  set(GPU_API_DESCRIPTOR_INDEXING_BACKEND
      "${GPU_API_DEFAULT_BACKEND}")
  if(GPU_API_DEFAULT_BACKEND STREQUAL "webgpu")
    set(GPU_API_DESCRIPTOR_INDEXING_BACKEND vulkan)
  endif()
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_API_DESCRIPTOR_INDEXING_USL_FIXTURE
    "${GPU_API_DESCRIPTOR_INDEXING_BACKEND}"
    api
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/descriptor_indexing.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES
    ${GPU_API_DESCRIPTOR_INDEXING_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE
    "${GPU_API_DESCRIPTOR_INDEXING_BACKEND}"
    api-buffer-descriptor-array
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/buffer_descriptor_array.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-buffer-descriptor-array-fixture
    DEPENDS ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE
    "${GPU_API_DESCRIPTOR_INDEXING_BACKEND}"
    api-buffer-descriptor-array-dynamic
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/buffer_descriptor_array_dynamic.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-buffer-descriptor-array-dynamic-fixture
    DEPENDS ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}
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
  set(GPU_API_SUBGROUP_MATRIX_BACKEND "${GPU_API_DEFAULT_BACKEND}")
  set(GPU_USL_FIXTURE_TARGET_CAPS subgroup_matrix)
  if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" OR
     GPU_API_DEFAULT_BACKEND STREQUAL "webgpu")
    set(GPU_API_SUBGROUP_MATRIX_BACKEND vulkan)
    set(GPU_API_SUBGROUP_MATRIX_USL_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/subgroup_matrix_vulkan.usl")
    set(GPU_API_SUBGROUP_MATRIX_PROFILE "16x8x8")
  elseif(GPU_API_DEFAULT_BACKEND STREQUAL "dx12")
    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_10,subgroup_matrix)
  endif()
  gpu_add_usl_fixtures(
    GPU_API_SUBGROUP_MATRIX_USL_FIXTURE
    "${GPU_API_SUBGROUP_MATRIX_BACKEND}"
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

  if(GPU_BUILD_METAL)
    set(GPU_USL_FIXTURE_TARGET_CAPS
        ray_query,intersection_function_table)
    gpu_add_usl_fixtures(
      GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE
      metal
      api-intersection-function
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/intersection_function.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-intersection-function-fixture
      DEPENDS ${GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE}
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

    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_5,sampler_feedback)
    gpu_add_usl_fixtures(
      GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE
      dx12
      api-sampler-feedback
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/sampler_feedback.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-dx12-sampler-feedback-fixture
      DEPENDS ${GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    gpu_add_usl_fixtures(
      GPU_METAL_MESH_TEST_USL_FIXTURE
      metal
      mesh-test
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
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
      set(GPU_API_VULKAN_MSAA_USL_FIXTURE
          ${GPU_API_MSAA_USL_FIXTURE})
      set(GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE
          ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE})
      set(GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE
          ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE})
    else()
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_USL_FIXTURES
        vulkan
        api
        ${GPU_API_USL_SOURCES}
      )
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_MSAA_USL_FIXTURE
        vulkan
        api-vulkan-msaa
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/msaa.usl"
      )
      add_custom_target(
        gpu-api-vulkan-msaa-fixture
        DEPENDS ${GPU_API_VULKAN_MSAA_USL_FIXTURE}
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
      set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE
        vulkan
        api-vulkan-buffer-descriptor-array
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/buffer_descriptor_array.usl"
      )
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE
        vulkan
        api-vulkan-buffer-descriptor-array-dynamic
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/buffer_descriptor_array_dynamic.usl"
      )
      unset(GPU_USL_FIXTURE_TARGET_CAPS)
      add_custom_target(
        gpu-api-vulkan-fixtures
        DEPENDS
          ${GPU_API_VULKAN_USL_FIXTURES}
          ${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}
          ${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}
      )
    endif()
  endif()
endif()

if(GPU_BUILD_DX12 AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  gpu_add_usl_fixtures(
    GPU_DX12_USL_FIXTURES
    dx12
    validation
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/triangle-dx12-usl/triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/textured-quad-dx12-usl/textured_quad.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/compute-buffer-dx12-usl/compute_buffer.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/compute-atomics-usl/compute_atomics.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/vrs-compare-usl/vrs_compare.usl"
  )
  list(GET GPU_DX12_USL_FIXTURES 0 GPU_DX12_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 1 GPU_DX12_TEXTURED_QUAD_US)
  list(GET GPU_DX12_USL_FIXTURES 2 GPU_DX12_COMPUTE_BUFFER_US)
  list(GET GPU_DX12_USL_FIXTURES 3 GPU_DX12_MESH_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 4 GPU_DX12_COMPUTE_ATOMICS_US)
  list(GET GPU_DX12_USL_FIXTURES 5 GPU_DX12_TEXTURED_CUBE_US)
  list(GET GPU_DX12_USL_FIXTURES 6 GPU_DX12_MRT_BLEND_US)
  list(GET GPU_DX12_USL_FIXTURES 7 GPU_DX12_SHADOW_COMPARE_US)
  list(GET GPU_DX12_USL_FIXTURES 8 GPU_DX12_VRS_COMPARE_US)
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_DX12_BINDLESS_USL_FIXTURE
    dx12
    validation
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_DX12_BINDLESS_USL_FIXTURE 0 GPU_DX12_BINDLESS_US)
  list(APPEND GPU_DX12_USL_FIXTURES ${GPU_DX12_BINDLESS_USL_FIXTURE})
  add_custom_target(gpu-dx12-fixtures DEPENDS ${GPU_DX12_USL_FIXTURES})
endif()

if(GPU_BUILD_VULKAN AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  gpu_add_usl_fixtures(
    GPU_VULKAN_USL_FIXTURES
    vulkan
    validation
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/triangle-vulkan-usl/triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/textured-quad-vulkan-usl/textured_quad.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/compute-buffer-vulkan-usl/compute_buffer.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/compute-atomics-usl/compute_atomics.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/vrs-compare-usl/vrs_compare.usl"
  )
  list(GET GPU_VULKAN_USL_FIXTURES 0 GPU_VULKAN_TRIANGLE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 1 GPU_VULKAN_TEXTURED_QUAD_US)
  list(GET GPU_VULKAN_USL_FIXTURES 2 GPU_VULKAN_COMPUTE_BUFFER_US)
  list(GET GPU_VULKAN_USL_FIXTURES 3 GPU_VULKAN_MESH_TRIANGLE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 4 GPU_VULKAN_COMPUTE_ATOMICS_US)
  list(GET GPU_VULKAN_USL_FIXTURES 5 GPU_VULKAN_TEXTURED_CUBE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 6 GPU_VULKAN_MRT_BLEND_US)
  list(GET GPU_VULKAN_USL_FIXTURES 7 GPU_VULKAN_SHADOW_COMPARE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 8 GPU_VULKAN_VRS_COMPARE_US)
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_VULKAN_BINDLESS_USL_FIXTURE
    vulkan
    validation
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_VULKAN_BINDLESS_USL_FIXTURE 0 GPU_VULKAN_BINDLESS_US)
  list(APPEND GPU_VULKAN_USL_FIXTURES ${GPU_VULKAN_BINDLESS_USL_FIXTURE})
  if(GPU_BUILD_TESTS)
    gpu_add_usl_fixtures(
      GPU_VULKAN_TEST_USL_FIXTURES
      vulkan
      tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
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
    validation
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/textured-quad-usl/textured_quad.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/compute-usl/compute_visible.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/compute-buffer-usl/compute_buffer.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/usl-reflection-check/reflection.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/usl-reflection-check/reflection_storage.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/compute-atomics-usl/compute_atomics.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
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
  list(GET GPU_METAL_USL_FIXTURES 9 GPU_METAL_MRT_BLEND_US)
  list(GET GPU_METAL_USL_FIXTURES 10 GPU_METAL_SHADOW_COMPARE_US)
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_METAL_BINDLESS_USL_FIXTURE
    metal
    validation
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_METAL_BINDLESS_USL_FIXTURE 0 GPU_METAL_BINDLESS_US)
  list(APPEND GPU_METAL_USL_FIXTURES ${GPU_METAL_BINDLESS_USL_FIXTURE})
  add_custom_target(gpu-metal-fixtures DEPENDS ${GPU_METAL_USL_FIXTURES})
endif()

if(GPU_BUILD_BENCHMARKS)
  set(GPU_BENCHMARK_USL_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/api/render_mrt.usl"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
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
  if(GPU_BUILD_WEBGPU AND NOT EMSCRIPTEN)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_WEBGPU_USL_FIXTURES
      webgpu
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_WEBGPU_USL_FIXTURES}
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
  elseif(GPU_BUILD_WEBGPU AND NOT EMSCRIPTEN)
    set(GPU_BENCHMARK_BACKEND webgpu)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_WEBGPU_USL_FIXTURES}
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
    list(LENGTH GPU_BENCHMARK_SELECTED_USL_FIXTURES
         GPU_BENCHMARK_SELECTED_USL_FIXTURE_COUNT)
    if(GPU_BENCHMARK_SELECTED_USL_FIXTURE_COUNT GREATER 3)
      list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
           3
           GPU_BENCHMARK_BINDLESS_DISPATCH_US)
    endif()
  endif()

  add_custom_target(gpu-benchmark-fixtures
    DEPENDS ${GPU_BENCHMARK_USL_FIXTURES}
  )
endif()

if(GPU_BUILD_TESTS OR GPU_BUILD_BENCHMARKS)
  enable_testing()
endif()

if(GPU_BUILD_TESTS)
  if(GPU_BUILD_WEBGPU AND NOT EMSCRIPTEN AND
     _gpu_webgpu_provider STREQUAL "WGPU_NATIVE")
    add_executable(gpu-webgpu-native-queue-test
      tests/webgpu/queue.c
    )
    target_link_libraries(gpu-webgpu-native-queue-test PRIVATE gpu)
    set_target_properties(gpu-webgpu-native-queue-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(NOT MSVC)
      target_compile_options(gpu-webgpu-native-queue-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
    endif()
    if(NOT WIN32)
      target_compile_definitions(gpu-webgpu-native-queue-test PRIVATE
        _POSIX_C_SOURCE=200809L
      )
    endif()
    add_test(NAME webgpu-native-queue
             COMMAND gpu-webgpu-native-queue-test)
  endif()

  add_executable(gpu-usl-target-test
    tests/api/usl_target.c
  )
  target_include_directories(gpu-usl-target-test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  )
  if(TARGET us)
    target_link_libraries(gpu-usl-target-test PRIVATE us)
  elseif(TARGET gpu_usl)
    target_link_libraries(gpu-usl-target-test PRIVATE gpu_usl)
  endif()
  set_target_properties(gpu-usl-target-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-usl-target-test PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME api-usl-target COMMAND gpu-usl-target-test)

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
      -DGPU_PACKAGE_VERSION=${PROJECT_VERSION}
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
    tests/api/buffer_descriptor_array.c
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
    tests/api/msaa.c
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
  add_dependencies(gpu-api-test gpu-api-msaa-fixture)
  add_dependencies(gpu-api-test gpu-api-atomic64-fixture)
  add_dependencies(gpu-api-test gpu-api-buffer-descriptor-array-fixture)
  add_dependencies(gpu-api-test
                   gpu-api-buffer-descriptor-array-dynamic-fixture)
  if(TARGET gpu-api-subgroup-matrix-fixture)
    add_dependencies(gpu-api-test gpu-api-subgroup-matrix-fixture)
  endif()
  if(TARGET gpu-api-ray-query-fixture)
    add_dependencies(gpu-api-test gpu-api-ray-query-fixture)
  endif()
  if(TARGET gpu-api-intersection-function-fixture)
    add_dependencies(gpu-api-test gpu-api-intersection-function-fixture)
  endif()
  if(TARGET gpu-api-vulkan-fixtures)
    add_dependencies(gpu-api-test gpu-api-vulkan-fixtures)
  endif()
  if(TARGET gpu-api-vulkan-msaa-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-msaa-fixture)
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
  if(TARGET gpu-api-dx12-sampler-feedback-fixture)
    add_dependencies(gpu-api-test gpu-api-dx12-sampler-feedback-fixture)
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
  if(NOT WIN32)
    target_compile_definitions(gpu-api-test PRIVATE
      _POSIX_C_SOURCE=200809L
    )
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
            ${GPU_API_DEFAULT_BACKEND}
  )
  set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
    "GPU_MSAA_USL_PATH=${GPU_API_MSAA_USL_FIXTURE}"
    "GPU_BUFFER_DESCRIPTOR_ARRAY_USL_PATH=${GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}"
    "GPU_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_PATH=${GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}"
  )
  if(GPU_API_RAY_QUERY_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_RAY_QUERY_USL_PATH=${GPU_API_RAY_QUERY_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_INTERSECTION_FUNCTION_USL_PATH=${GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE}"
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
  if(GPU_API_DEFAULT_BACKEND STREQUAL "dx12" AND
     GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_SAMPLER_FEEDBACK_USL_PATH=${GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE}"
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
    add_executable(gpu-metal-mesh-test
      tests/api/mesh.c
      tests/api/runner.c
    )
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
    set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
      "GPU_MSAA_USL_PATH=${GPU_API_VULKAN_MSAA_USL_FIXTURE}"
      "GPU_BUFFER_DESCRIPTOR_ARRAY_USL_PATH=${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}"
      "GPU_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_PATH=${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}"
    )
    if(GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
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
      tests/api/runner.c
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

    add_executable(gpu-dx12-mesh-test
      tests/api/mesh.c
      tests/api/runner.c
    )
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

if(GPU_BUILD_SAMPLES)
  add_subdirectory(tests/validation)
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
            vulkan-shadow-compare
            vulkan-vrs-compare
            vulkan-vrs-attachment
            vulkan-textured-cube
            vulkan-mrt-blend
            vulkan-bindless
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

  foreach(gpu_benchmark_target
          gpu-dispatch-bench
          gpu-ray-dispatch-bench
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
    target_sources(${gpu_benchmark_target} PRIVATE benchmarks/bench_gpu.c)
  endforeach()

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
    set(GPU_BENCHMARK_TEXTURE_UPLOAD_MODE depth)
    if(GPU_BENCHMARK_BACKEND STREQUAL "webgpu")
      set(GPU_BENCHMARK_TEXTURE_UPLOAD_MODE color)
    endif()

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
      ${GPU_BENCHMARK_TEXTURE_UPLOAD_MODE}
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
    if(GPU_BENCHMARK_BINDLESS_DISPATCH_US)
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
    endif()
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
