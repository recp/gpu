target_sources(gpu PRIVATE
  backend.c
  cache_file.c
)

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

if(APPLE AND (GPU_BUILD_VULKAN OR GPU_BUILD_WEBGPU))
  target_sources(gpu PRIVATE surface_apple.m)
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

if(GPU_BUILD_METAL)
  add_subdirectory(mt)
endif()

if(GPU_BUILD_DX12)
  add_subdirectory(dx12)
endif()

if(GPU_BUILD_VULKAN)
  add_subdirectory(vk)
  set(GPU_VULKAN_LIBRARY_DIR "${GPU_VULKAN_LIBRARY_DIR}" PARENT_SCOPE)
  set(GPU_VULKAN_ICD "${GPU_VULKAN_ICD}" PARENT_SCOPE)
  set(GPU_VULKAN_HAS_XLIB "${GPU_VULKAN_HAS_XLIB}" PARENT_SCOPE)
  set(GPU_VULKAN_HAS_WAYLAND "${GPU_VULKAN_HAS_WAYLAND}" PARENT_SCOPE)
endif()

if(GPU_BUILD_WEBGPU)
  add_subdirectory(webgpu)
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
endif()
