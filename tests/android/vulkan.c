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

#include <gpu/gpu.h>

#include <stdio.h>

int gpu_test_copy(GPUDevice *device);

int
main(void) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUAdapterProperties  properties   = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUResult             result;
  uint32_t              adapterCount;
  int                   ok;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "android-vulkan-test";
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
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
  ok = gpu_test_copy(device);

  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);

  if (!ok) {
    fprintf(stderr, "android: copy/blit/mipmap validation failed\n");
    return 1;
  }

  puts("android: copy/blit/mipmap validation passed");

  return 0;
}
