#include "test.h"

static int
run_queue(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_queue(testCtx->instance,
                        testCtx->adapter,
                        testCtx->device);
}

static int
run_sampler(void *ctx) {
  return gpu_test_sampler(((GPUApiTestContext *)ctx)->device);
}

static int
run_bindgroup(void *ctx) {
  return gpu_test_bindgroup(((GPUApiTestContext *)ctx)->device);
}

static int
run_resources(void *ctx) {
  return gpu_test_resources(((GPUApiTestContext *)ctx)->device);
}

static int
run_copy(void *ctx) {
  return gpu_test_copy(((GPUApiTestContext *)ctx)->device);
}

static int
run_render(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_render(testCtx->device, testCtx->mrtBytecodePath);
}

static int
run_compute(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_compute(testCtx->device, testCtx->computeBytecodePath);
}

static int
run_query(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_query(testCtx->adapter,
                        testCtx->device,
                        testCtx->computeBytecodePath);
}

static int
run_barrier(void *ctx) {
  return gpu_test_barrier(((GPUApiTestContext *)ctx)->device);
}

static int
run_runtime(void *ctx) {
  return gpu_test_runtime(((GPUApiTestContext *)ctx)->device);
}

static int
run_threading(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_threading(testCtx->device, testCtx->uslBytecodePath);
}

static int
run_shader(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_shader(testCtx->device,
                         testCtx->uslBytecodePath,
                         testCtx->descriptorArrayBytecodePath);
}

static int
run_source_sampler(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_source_sampler_draw(testCtx->device,
                                      testCtx->sourceSamplerBytecodePath);
}

static int
run_descriptor_array(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_descriptor_array(testCtx->device,
                                   testCtx->descriptorArrayBytecodePath);
}

static int
run_storage_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_storage_texture_view(testCtx->device,
                                       testCtx->storageTextureBytecodePath);
}

static int
run_cube_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_cube_texture_view(testCtx->device,
                                    testCtx->cubeTextureBytecodePath);
}

static int
run_line_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_line_texture_view(testCtx->device,
                                    testCtx->lineTextureBytecodePath);
}

static int
run_volume_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_volume_texture_view(testCtx->device,
                                      testCtx->volumeTextureBytecodePath);
}

static GPUAdapter *
select_adapter(GPUInstance *instance) {
  GPUAdapter *adapter = NULL;
  uint32_t adapterCount = 1;
  GPUResult result;

  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

static bool
parse_backend(const char *name, GPUBackend *outBackend) {
  if (!name || !outBackend) {
    return false;
  }

  if (strcmp(name, "metal") == 0) {
    *outBackend = GPU_BACKEND_METAL;
  } else if (strcmp(name, "vulkan") == 0) {
    *outBackend = GPU_BACKEND_VULKAN;
  } else if (strcmp(name, "dx12") == 0) {
    *outBackend = GPU_BACKEND_DX12;
  } else {
    return false;
  }
  return true;
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPURuntimeConfig      runtimeConfig = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUApiTestContext      ctx;
  GPUApiTest             tests[18];
  int                    ok;

  if (argc != 10 && argc != 11) {
    fprintf(stderr,
            "usage: %s <reflection.us> <render_mrt.us> <compute.us> "
            "<source_sampler.us> <storage_texture.us> <cube_texture.us> "
            "<line_texture.us> <volume_texture.us> <descriptor_arrays.us> "
            "[metal|vulkan|dx12]\n",
            argv[0]);
    return 2;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DEFAULT;
  instanceInfo.enableValidation = true;
  if (argc == 11 &&
      !parse_backend(argv[10], &instanceInfo.preferredBackend)) {
    fprintf(stderr, "unknown backend: %s\n", argv[10]);
    return 2;
  }
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "failed to create instance\n");
    return 1;
  }

  adapter = select_adapter(instance);
  if (!adapter) {
    fprintf(stderr, "failed to get adapter\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "failed to create device\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  runtimeConfig.chain.sType          = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize     = sizeof(runtimeConfig);
  runtimeConfig.enableDebugMarkers   = true;
  runtimeConfig.enableVerboseLogs    = getenv("GPU_API_VERBOSE") != NULL;
  if (GPUConfigureRuntime(device, &runtimeConfig) != GPU_OK) {
    fprintf(stderr, "failed to enable debug markers\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  ctx.instance                    = instance;
  ctx.adapter                     = adapter;
  ctx.device                      = device;
  ctx.uslBytecodePath             = argv[1];
  ctx.mrtBytecodePath             = argv[2];
  ctx.computeBytecodePath         = argv[3];
  ctx.sourceSamplerBytecodePath   = argv[4];
  ctx.storageTextureBytecodePath  = argv[5];
  ctx.cubeTextureBytecodePath     = argv[6];
  ctx.lineTextureBytecodePath     = argv[7];
  ctx.volumeTextureBytecodePath   = argv[8];
  ctx.descriptorArrayBytecodePath = argv[9];

  tests[0]  = (GPUApiTest){ "queue", run_queue, &ctx };
  tests[1]  = (GPUApiTest){ "sampler", run_sampler, &ctx };
  tests[2]  = (GPUApiTest){ "bindgroup", run_bindgroup, &ctx };
  tests[3]  = (GPUApiTest){ "resources", run_resources, &ctx };
  tests[4]  = (GPUApiTest){ "threading", run_threading, &ctx };
  tests[5]  = (GPUApiTest){ "copy", run_copy, &ctx };
  tests[6]  = (GPUApiTest){ "render", run_render, &ctx };
  tests[7]  = (GPUApiTest){ "compute", run_compute, &ctx };
  tests[8]  = (GPUApiTest){ "query", run_query, &ctx };
  tests[9]  = (GPUApiTest){ "barrier", run_barrier, &ctx };
  tests[10] = (GPUApiTest){ "runtime", run_runtime, &ctx };
  tests[11] = (GPUApiTest){ "shader", run_shader, &ctx };
  tests[12] = (GPUApiTest){ "source-sampler", run_source_sampler, &ctx };
  tests[13] = (GPUApiTest){ "storage-texture", run_storage_texture, &ctx };
  tests[14] = (GPUApiTest){ "cube-texture", run_cube_texture, &ctx };
  tests[15] = (GPUApiTest){ "line-texture", run_line_texture, &ctx };
  tests[16] = (GPUApiTest){ "volume-texture", run_volume_texture, &ctx };
  tests[17] = (GPUApiTest){ "descriptor-array", run_descriptor_array, &ctx };

  ok = gpu_run_api_tests(tests, (uint32_t)GPU_ARRAY_LEN(tests));

  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  if (!ok) {
    return 1;
  }

  printf("GPU API validation passed\n");
  return 0;
}
