target_sources(gpu PRIVATE
  barrier.c
  buffer.c
  cmd/cmdbuf.c
  cmd/cmdque.c
  compile_pool.c
  compute.c
  constants.c
  descr/descriptor.c
  device.c
  execution_graph.c
  sampler_feedback.c
  format.c
  frame.c
  frame_feedback.c
  image_buffer.c
  instance.c
  library.c
  memory.c
  multigpu.c
  pacer.c
  pass/blit.c
  pass/renderpass.c
  pipeline_cache.c
  pipeline_report.c
  query.c
  ray.c
  render/pipeline.c
  render/rce.c
  retire.c
  sampler.c
  surface.c
  subgroup.c
  suballoc.c
  swapchain.c
  texture.c
  uploader.c
  tensor.c
  tile.c
  ml.c
  vertex.c
  vrs.c
)

# frame pacing for backends without a display link of their own (pacer_internal.h)
if(ANDROID)
  target_sources(gpu PRIVATE pacer_android.c)
  target_link_libraries(gpu PRIVATE android)
elseif(WIN32)
  target_sources(gpu PRIVATE pacer_win32.c)
elseif(EMSCRIPTEN)
  target_sources(gpu PRIVATE pacer_web.c)
elseif(APPLE)
  if(GPU_BUILD_VULKAN OR GPU_BUILD_WEBGPU)
    target_sources(gpu PRIVATE pacer_apple.m)
  else()
    target_compile_definitions(gpu PRIVATE GPU_PACER_NO_CLOCK=1)
  endif()
else()
  target_sources(gpu PRIVATE pacer_thread.c)
endif()
