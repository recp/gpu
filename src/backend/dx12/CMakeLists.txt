target_sources(gpu PRIVATE
  init.c
  impl/blit.c
  impl/buffer.c
  impl/cmdque.c
  impl/compute.c
  impl/descr/descpool.c
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
  impl/renderpass.c
  impl/rce.c
  impl/sampler.c
  impl/sampler_feedback.c
  impl/surface.c
  impl/swapchain.c
  impl/texture.c
  impl/vrs.c
)

target_link_libraries(gpu PRIVATE
  d3d12
  d3dcompiler
  dxgi
  dxguid
)
