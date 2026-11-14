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
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <assert.h>

static VkBool32 supportedIO;
static VkBool32 supportedStorage;
static VkBool32 requestedIO;
static VkBool32 requestedStorage;
static VkBool32 requestedF16;
static uint32_t creates;
static uint32_t modules;

static void
check_library(GPUDevice *device, const char *path, bool supported) {
  GPUShaderLibraryCreateInfo info = {0};
  GPUShaderLibrary          *library = NULL;
  void                      *bytes;
  uint64_t                   size;
  uint32_t                   before;
  GPUResult                  result;

  assert((bytes = gpu_test_read_file(path, &size)));
  info.sourceKind       = GPU_SHADER_SOURCE_USL_BYTECODE;
  info.sourceData       = bytes;
  info.sourceSize       = size;
  info.disableDiskCache = true;
  before                = modules;
  result                = GPUCreateShaderLibrary(device, &info, &library);
  assert(result == (supported ? GPU_OK : GPU_ERROR_UNSUPPORTED));
  assert((library != NULL) == supported);
  assert(modules == before + (supported ? 1u : 0u));
  GPUDestroyShaderLibrary(library);
  free(bytes);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateShaderModule(VkDevice                         device,
                     const VkShaderModuleCreateInfo  *info,
                     const VkAllocationCallbacks     *allocator,
                     VkShaderModule                  *module) {
  PFN_vkCreateShaderModule create;

  modules++;
  create = (PFN_vkCreateShaderModule)dlsym(RTLD_NEXT, "vkCreateShaderModule");
  assert(create && create != vkCreateShaderModule);

  return create(device, info, allocator, module);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice             physical,
               const VkDeviceCreateInfo    *info,
               const VkAllocationCallbacks *allocator,
               VkDevice                    *device) {
  VkPhysicalDevice16BitStorageFeatures storage = {0};
  VkPhysicalDeviceFeatures2            features = {0};
  const VkPhysicalDeviceShaderFloat16Int8Features *arithmetic;
  const VkPhysicalDevice16BitStorageFeatures     *requested;
  const VkBaseInStructure                       *next;

  PFN_vkCreateDevice create;

  storage.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
  features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features.pNext = &storage;
  vkGetPhysicalDeviceFeatures2(physical, &features);

  supportedIO      = storage.storageInputOutput16;
  supportedStorage = storage.storageBuffer16BitAccess;

  for (next = info->pNext; next; next = next->pNext) {
    if (next->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES) {
      arithmetic   = (const VkPhysicalDeviceShaderFloat16Int8Features *)next;
      requestedF16 = arithmetic->shaderFloat16;
      continue;
    }

    if (next->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES)
      continue;

    requested        = (const VkPhysicalDevice16BitStorageFeatures *)next;
    requestedIO      = requested->storageInputOutput16;
    requestedStorage = requested->storageBuffer16BitAccess;
  }

  creates++;
  create = (PFN_vkCreateDevice)dlsym(RTLD_NEXT, "vkCreateDevice");
  assert(create && create != vkCreateDevice);

  return create(physical, info, allocator, device);
}

int
main(int argc, char **argv) {
  static const GPUFeature half = GPU_FEATURE_SHADER_F16;

  GPUInstanceCreateInfo instanceInfo = {0};
  GPUDeviceCreateInfo   deviceInfo = {0};
  GPUInstance          *instance = NULL;
  GPUAdapter           *adapter = NULL;
  GPUDevice            *device = NULL;
  bool                  native;

  assert(argc == 5);
  native = strcmp(argv[1], "f16") == 0;

  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  assert(GPUCreateInstance(&instanceInfo, &instance) == GPU_OK);
  assert(gpu_test_request_adapter(instance, &adapter) == GPU_OK);
  if (native && !GPUIsFeatureSupported(adapter, half)) {
    GPUDestroyInstance(instance);
    return 77;
  }

  if (native) {
    deviceInfo.required.pFeatures    = &half;
    deviceInfo.required.featureCount = 1u;
  }
  assert(gpu_test_create_device(adapter, &deviceInfo, &device) == GPU_OK);
  assert(GPUIsFeatureEnabled(device, half) == native && creates == 1u);
  assert(requestedIO == (native && supportedIO));
  assert(requestedStorage == (native && supportedStorage));
  assert(requestedF16 == native);
  /* half stage values need shader_f16; without storageInputOutput16 they travel as f32. */
  check_library(device, argv[2], native);
  check_library(device, argv[3], native);

  check_library(device, argv[4], true);
  printf("{\"f16\":%u,\"requestedF16\":%u,\"supportedIO\":%u,\"requestedIO\":%u,"
         "\"requestedStorage\":%u,\"creates\":%u,\"modules\":%u}\n",
         native, requestedF16, supportedIO, requestedIO, requestedStorage, creates, modules);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  return 0;
}
