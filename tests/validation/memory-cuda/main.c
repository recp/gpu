/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "test.h"

int
main(void) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUResult             result;
  uint32_t              adapterCount;
  int                   status;

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
  result       = GPUEnumerateAdapters(instance, &adapterCount, &adapter);

  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY)
      || !adapter) {
    puts("CUDA adapter unavailable");
    status = 77;
    goto cleanup;
  }

  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_BUFFER_DEVICE_ADDRESS)) {
    puts("CUDA buffer device address unavailable");
    status = 77;
    goto cleanup;
  }

  if (!gpu_test_memory(adapter)) {
    fprintf(stderr, "CUDA memory validation failed\n");
    goto cleanup;
  }

  puts("CUDA memory validation passed");
  status = 0;

cleanup:
  GPUDestroyInstance(instance);
  return status;
}
