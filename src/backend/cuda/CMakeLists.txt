target_compile_definitions(gpu PRIVATE GPU_ENABLE_CUDA=1)
target_sources(gpu PRIVATE
  init.c
  impl/buffer.c
  impl/cmdque.c
  impl/binding.c
  impl/compute.c
  impl/device.c
  impl/driver.c
  impl/format.c
  impl/instance.c
  impl/library.c
  impl/module.c
  impl/multigpu.c
  impl/sampler.c
  impl/texture.c
  impl/texture_plan.c
)

if(CMAKE_DL_LIBS)
  target_link_libraries(gpu PRIVATE ${CMAKE_DL_LIBS})
endif()
