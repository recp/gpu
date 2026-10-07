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

#include "../api/test.h"

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUAdapterProperties  properties   = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUResult             result;
  uint32_t              adapterCount;
  int                   ok;

  if (argc > 2) {
    fprintf(stderr, "usage: %s [lod.us]\n", argv[0]);
    return 2;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "android-vulkan-test";
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = getenv("GPU_ANDROID_VALIDATION") != NULL;
  instance                      = NULL;

  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "android: Vulkan instance creation failed\n");
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  result       = GPUEnumerateAdapters(instance, &adapterCount, &adapter);

  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY)
      || !adapter
      || GPUGetAdapterProperties(adapter, &properties) != GPU_OK) {
    fprintf(stderr, "android: Vulkan adapter enumeration failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  if (!(device = GPUCreateDeviceWithDefaultQueues(adapter))) {
    fprintf(stderr, "android: Vulkan device creation failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  printf("android: %s\n", properties.name ? properties.name : "Vulkan adapter");
  fflush(stdout);
  ok = gpu_test_copy(device);
  ok = gpu_test_sampler(device) && ok;

  if (argc == 2)
    ok = gpu_test_lod(device, argv[1]) && ok;

  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);

  if (!ok) {
    fprintf(stderr, "android: Vulkan validation failed\n");
    return 1;
  }

  puts("android: copy/blit/mipmap/sampler validation passed");

  return 0;
}
