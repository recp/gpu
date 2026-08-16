#include "test.h"

int
main(int argc, char **argv) {
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUInstanceCreateInfo  instanceInfo = {0};
  GPUResult              result;
  uint32_t               adapterCount;
  int                    status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-atomic64-cuda-usl artifact.us\n");
    return 1;
  }

  instance = NULL;
  adapter  = NULL;
  status   = 1;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_CUDA;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    puts("CUDA Driver backend unavailable");
    status = 77;
    goto cleanup;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    puts("CUDA adapter unavailable");
    status = 77;
    goto cleanup;
  }

  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_ATOMIC64)) {
    puts("CUDA atomic64 unavailable");
    status = 77;
    goto cleanup;
  }
  if (!gpu_test_atomic64(adapter, argv[1])) {
    fprintf(stderr, "CUDA atomic64 validation failed\n");
    goto cleanup;
  }

  puts("CUDA USL atomic64 validation passed");
  status = 0;

cleanup:
  GPUDestroyInstance(instance);
  return status;
}
