#include "test.h"

static int
run_queue(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_queue(testCtx->adapter, testCtx->device);
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
  return gpu_test_render(((GPUApiTestContext *)ctx)->device);
}

static int
run_compute(void *ctx) {
  return gpu_test_compute(((GPUApiTestContext *)ctx)->device);
}

static int
run_shader(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_shader(testCtx->device, testCtx->uslBytecodePath);
}

int
main(int argc, char **argv) {
  GPUAdapter *adapter;
  GPUDevice *device;
  GPUApiTestContext ctx;
  GPUApiTest tests[8];
  int ok;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <reflection.us>\n", argv[0]);
    return 2;
  }

  adapter = GPUGetAutoSelectedPhysicalDevice(NULL);
  if (!adapter) {
    fprintf(stderr, "failed to get adapter\n");
    return 1;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "failed to create device\n");
    return 1;
  }

  ctx.adapter = adapter;
  ctx.device = device;
  ctx.uslBytecodePath = argv[1];

  tests[0] = (GPUApiTest){ "queue", run_queue, &ctx };
  tests[1] = (GPUApiTest){ "sampler", run_sampler, &ctx };
  tests[2] = (GPUApiTest){ "bindgroup", run_bindgroup, &ctx };
  tests[3] = (GPUApiTest){ "resources", run_resources, &ctx };
  tests[4] = (GPUApiTest){ "copy", run_copy, &ctx };
  tests[5] = (GPUApiTest){ "render", run_render, &ctx };
  tests[6] = (GPUApiTest){ "compute", run_compute, &ctx };
  tests[7] = (GPUApiTest){ "shader", run_shader, &ctx };

  ok = gpu_run_api_tests(tests, (uint32_t)GPU_ARRAY_LEN(tests));

  GPUDestroyDevice(device);
  if (!ok) {
    return 1;
  }

  printf("GPU API validation passed\n");
  return 0;
}
