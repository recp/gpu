find_package(Vulkan REQUIRED)
set_property(TARGET Vulkan::Vulkan PROPERTY IMPORTED_GLOBAL TRUE)

set(GPU_VULKAN_HAS_XLIB OFF)
set(GPU_VULKAN_HAS_WAYLAND OFF)
if(UNIX AND NOT APPLE AND NOT ANDROID)
  find_package(X11 QUIET)
  if(X11_FOUND)
    set(GPU_VULKAN_HAS_XLIB ON)
    target_compile_definitions(gpu PRIVATE GPU_VULKAN_HAS_XLIB=1)
    target_include_directories(gpu PRIVATE ${X11_INCLUDE_DIR})
  endif()

  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GPU_WAYLAND_CLIENT QUIET wayland-client)
    if(GPU_WAYLAND_CLIENT_FOUND)
      set(GPU_VULKAN_HAS_WAYLAND ON)
      target_compile_definitions(gpu PRIVATE GPU_VULKAN_HAS_WAYLAND=1)
      target_include_directories(gpu PRIVATE
        ${GPU_WAYLAND_CLIENT_INCLUDE_DIRS}
      )
    endif()
  endif()
endif()

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
  init.c
  impl/blit.c
  impl/buffer.c
  impl/cmdque.c
  impl/compute.c
  impl/descriptor.c
  impl/device.c
  impl/execution_graph.c
  impl/format.c
  impl/frame.c
  impl/instance.c
  impl/library.c
  impl/memory.c
  impl/multigpu.c
  impl/pipeline.c
  impl/pipeline_cache.c
  impl/query.c
  impl/ray.c
  impl/rce.c
  impl/renderpass.c
  impl/sampler.c
  impl/sync.c
  impl/surface.c
  impl/swapchain.c
  impl/texture.c
  impl/vrs.c
)
if(GPU_BUILD_WITH_VALIDATION)
  target_sources(gpu PRIVATE impl/debug.c)
endif()

target_link_libraries(gpu PRIVATE Vulkan::Vulkan)
if(ANDROID)
  target_link_libraries(gpu PRIVATE log)
endif()

set(GPU_VULKAN_LIBRARY_DIR "${GPU_VULKAN_LIBRARY_DIR}" PARENT_SCOPE)
set(GPU_VULKAN_ICD "${GPU_VULKAN_ICD}" PARENT_SCOPE)
set(GPU_VULKAN_HAS_XLIB "${GPU_VULKAN_HAS_XLIB}" PARENT_SCOPE)
set(GPU_VULKAN_HAS_WAYLAND "${GPU_VULKAN_HAS_WAYLAND}" PARENT_SCOPE)
