find_package(Vulkan REQUIRED)
set_property(TARGET Vulkan::Vulkan PROPERTY IMPORTED_GLOBAL TRUE)

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
