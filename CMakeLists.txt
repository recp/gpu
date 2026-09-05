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
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(GPU_ENABLE_CUDA_DEFAULT ON)
else()
  set(GPU_ENABLE_CUDA_DEFAULT OFF)
endif()
option(GPU_ENABLE_CUDA "Build the CUDA Driver backend" ${GPU_ENABLE_CUDA_DEFAULT})
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
option(GPU_BACKEND_CUDA_ONLY "Build only the CUDA Driver backend" OFF)

if(EMSCRIPTEN AND BUILD_SHARED_LIBS)
  message(STATUS "Building GPU statically for browser WebAssembly")
  set(BUILD_SHARED_LIBS OFF)
endif()

set(GPU_SINGLE_BACKEND_COUNT 0)
foreach(_gpu_backend_only
        GPU_BACKEND_METAL_ONLY
        GPU_BACKEND_VULKAN_ONLY
        GPU_BACKEND_DX12_ONLY
        GPU_BACKEND_WEBGPU_ONLY
        GPU_BACKEND_CUDA_ONLY)
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
if(GPU_BACKEND_CUDA_ONLY AND NOT (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  message(FATAL_ERROR "GPU_BACKEND_CUDA_ONLY requires Windows or Linux")
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
if(GPU_BACKEND_CUDA_ONLY)
  set(GPU_ENABLE_CUDA ON)
endif()

set(GPU_BUILD_METAL OFF)
set(GPU_BUILD_VULKAN OFF)
set(GPU_BUILD_DX12 OFF)
set(GPU_BUILD_WEBGPU OFF)
set(GPU_BUILD_CUDA OFF)

if(GPU_BACKEND_METAL_ONLY)
  set(GPU_BUILD_METAL ON)
elseif(GPU_BACKEND_VULKAN_ONLY)
  set(GPU_BUILD_VULKAN ON)
elseif(GPU_BACKEND_DX12_ONLY)
  set(GPU_BUILD_DX12 ON)
elseif(GPU_BACKEND_WEBGPU_ONLY)
  set(GPU_BUILD_WEBGPU ON)
elseif(GPU_BACKEND_CUDA_ONLY)
  set(GPU_BUILD_CUDA ON)
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
  if(GPU_ENABLE_CUDA AND (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    set(GPU_BUILD_CUDA ON)
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

add_library(gpu src/init.c)
add_library(gpu::gpu ALIAS gpu)

target_compile_definitions(gpu PRIVATE
  GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  GPU_BUILD_WITH_DEBUG_MARKERS=$<BOOL:${GPU_BUILD_WITH_DEBUG_MARKERS}>
  GPU_BACKEND_METAL_ONLY=$<BOOL:${GPU_BACKEND_METAL_ONLY}>
  GPU_BACKEND_VULKAN_ONLY=$<BOOL:${GPU_BACKEND_VULKAN_ONLY}>
  GPU_BACKEND_DX12_ONLY=$<BOOL:${GPU_BACKEND_DX12_ONLY}>
  GPU_BACKEND_WEBGPU_ONLY=$<BOOL:${GPU_BACKEND_WEBGPU_ONLY}>
  GPU_BACKEND_CUDA_ONLY=$<BOOL:${GPU_BACKEND_CUDA_ONLY}>
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
list(FILTER GPU_PUBLIC_HEADERS EXCLUDE REGEX "/\\._[^/]+$")
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

if(WIN32)
  get_property(_gpu_generator_multi_config GLOBAL
               PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(_gpu_generator_multi_config)
    set(_gpu_link_runtime_dir "${CMAKE_BINARY_DIR}/$<CONFIG>")
  else()
    set(_gpu_link_runtime_dir "${CMAKE_BINARY_DIR}")
  endif()
  set(_gpu_link_runtime_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_gpu_link_runtime_dir}"
  )
  set(_gpu_link_runtime_dependencies)
  foreach(_gpu_link_runtime_target us ds)
    if(TARGET ${_gpu_link_runtime_target})
      get_target_property(_gpu_link_runtime_type
                          ${_gpu_link_runtime_target} TYPE)
      if(_gpu_link_runtime_type STREQUAL "SHARED_LIBRARY" OR
         _gpu_link_runtime_type STREQUAL "MODULE_LIBRARY")
        list(APPEND _gpu_link_runtime_commands
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${_gpu_link_runtime_target}>
                  "${_gpu_link_runtime_dir}"
        )
        list(APPEND _gpu_link_runtime_dependencies
                    ${_gpu_link_runtime_target})
      endif()
    endif()
  endforeach()
  if(_gpu_link_runtime_dependencies)
    add_custom_target(gpu-link-runtime-stage
      ${_gpu_link_runtime_commands}
      DEPENDS ${_gpu_link_runtime_dependencies}
      VERBATIM
    )
    add_dependencies(gpu gpu-link-runtime-stage)
  endif()
endif()

if(GPU_BUILD_DX12)
  include(cmake/DX12Agility.cmake)
  gpu_enable_dx12_agility(gpu)
  include(cmake/DX12DXC.cmake)
  gpu_enable_dx12_dxc(gpu)
endif()

add_subdirectory(src/api)
add_subdirectory(src/backend)

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
if(GPU_BUILD_DX12 AND GPU_DX12_AGILITY_CORE)
  install(FILES "${GPU_DX12_AGILITY_CORE}"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/D3D12
  )
  if(GPU_BUILD_WITH_VALIDATION)
    install(FILES "${GPU_DX12_AGILITY_LAYERS}"
      DESTINATION ${CMAKE_INSTALL_BINDIR}/D3D12
    )
  endif()
  install(FILES "${GPU_DX12_AGILITY_LICENSE}"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gpu/licenses
    RENAME DirectX12Agility-LICENSE.txt
  )
endif()
if(GPU_BUILD_DX12 AND GPU_DX12_DXC_COMPILER)
  install(FILES
    "${GPU_DX12_DXC_COMPILER}"
    "${GPU_DX12_DXC_VALIDATOR}"
    DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  install(DIRECTORY "${GPU_DX12_DXC_HLSL_INCLUDE}/"
    DESTINATION ${CMAKE_INSTALL_BINDIR}/include/hlsl
  )
  install(FILES ${GPU_DX12_DXC_LICENSES}
    DESTINATION ${CMAKE_INSTALL_DATADIR}/gpu/licenses/dxc
  )
endif()
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
