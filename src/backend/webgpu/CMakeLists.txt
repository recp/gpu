target_compile_definitions(gpu PRIVATE GPU_ENABLE_WEBGPU=1)
target_sources(gpu PRIVATE
  init.c
  impl/blit.c
  impl/device.c
  impl/instance.c
  impl/surface.c
  impl/swapchain.c
  impl/frame.c
  impl/cmdque.c
  impl/completion.c
  impl/buffer.c
  impl/texture.c
  impl/sampler.c
  impl/library.c
  impl/descriptor.c
  impl/pipeline.c
  impl/compute.c
  impl/query.c
  impl/renderpass.c
  impl/rce.c
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

set(GPU_INSTALL_TARGETS "${GPU_INSTALL_TARGETS}" PARENT_SCOPE)
set(GPU_CONFIG_HAS_WEBGPU_STATIC_PROVIDER
    "${GPU_CONFIG_HAS_WEBGPU_STATIC_PROVIDER}"
    PARENT_SCOPE)
set(GPU_CONFIG_WEBGPU_STATIC_PROVIDER_FILENAME
    "${GPU_CONFIG_WEBGPU_STATIC_PROVIDER_FILENAME}"
    PARENT_SCOPE)
set(_gpu_webgpu_static_provider_library
    "${_gpu_webgpu_static_provider_library}"
    PARENT_SCOPE)
set(_gpu_webgpu_runtime_library
    "${_gpu_webgpu_runtime_library}"
    PARENT_SCOPE)
