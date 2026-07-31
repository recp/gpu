target_sources(gpu PRIVATE
  init.c
  impl/blit.m
  impl/instance.c
  impl/surface.c
  impl/buffer.m
  impl/cmd/cmdbuf.m
  impl/cmd/cmdque.m
  impl/compute.m
  impl/descriptor.m
  impl/depthstencil.m
  impl/device.m
  impl/frame.m
  impl/format.m
  impl/library.m
  impl/memory.m
  impl/multigpu.m
  impl/pipeline_cache.m
  impl/ray.m
  impl/render/pipeline.m
  impl/render/rce.m
  impl/renderpass.m
  impl/swapchain.m
  impl/texture_view_pool.m
  impl/vertex.m
  impl/vrs.m
)

find_library(GPU_FRAMEWORK_METALKIT MetalKit REQUIRED)
gpu_link_apple_framework(gpu MetalKit "${GPU_FRAMEWORK_METALKIT}")
