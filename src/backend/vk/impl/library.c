/*
 * Copyright (C) 2020 Recep Aslantas
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

#include "../common.h"

GPU_HIDE
GPUShaderLibrary*
vk_newLibraryWithBinary(GPUDevice *device,
                        const void *data,
                        uint64_t size) {
  VkShaderModuleCreateInfo createInfo = {0};
  GPUDeviceVk        *deviceVk;
  GPUShaderLibraryVk *libraryVk;
  GPUShaderLibrary   *library;
  const uint32_t     *words;
  uint32_t           *alignedWords;
  VkResult            result;

  if (!device || !device->_priv || !data || size < 20u ||
      size > (uint64_t)SIZE_MAX || size % sizeof(uint32_t) != 0u) {
    return NULL;
  }

  words        = data;
  alignedWords = NULL;
  if ((uintptr_t)data % _Alignof(uint32_t) != 0u) {
    alignedWords = malloc((size_t)size);
    if (!alignedWords) {
      return NULL;
    }
    memcpy(alignedWords, data, (size_t)size);
    words = alignedWords;
  }
  if (words[0] != 0x07230203u) {
    free(alignedWords);
    return NULL;
  }

  deviceVk  = device->_priv;
  library   = calloc(1, sizeof(*library));
  libraryVk = calloc(1, sizeof(*libraryVk));
  if (!library || !libraryVk) {
    free(libraryVk);
    free(library);
    free(alignedWords);
    return NULL;
  }

  createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = (size_t)size;
  createInfo.pCode    = words;
  result = vkCreateShaderModule(deviceVk->device,
                                &createInfo,
                                NULL,
                                &libraryVk->module);
  free(alignedWords);
  if (result != VK_SUCCESS) {
    free(libraryVk);
    free(library);
    return NULL;
  }

  libraryVk->device = deviceVk->device;
  library->_priv    = libraryVk;
  return library;
}

GPU_HIDE
void
vk_destroyLibrary(GPUShaderLibrary *library) {
  GPUShaderLibraryVk *libraryVk;

  if (!library) {
    return;
  }

  libraryVk = library->_priv;
  if (libraryVk) {
    if (libraryVk->device && libraryVk->module) {
      vkDestroyShaderModule(libraryVk->device, libraryVk->module, NULL);
    }
    free(libraryVk);
  }
  free(library);
}

GPU_HIDE
void
vk_initLibrary(GPUApiLibrary *api) {
  api->newLibraryWithBinary = vk_newLibraryWithBinary;
  api->destroyLibrary       = vk_destroyLibrary;
}
