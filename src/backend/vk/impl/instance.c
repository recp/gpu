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
GPUInstance*
vk_createInstance(GPUApi * __restrict api, void * __restrict unused) {
  GPUInstance *inst;
  VkInstance   vkinst;
  VkResult     err;
  bool         portabilityEnumerationActive;

  portabilityEnumerationActive = false;

  const VkApplicationInfo app = {
    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = APP_SHORT_NAME,
    .applicationVersion = 0,
    .pEngineName        = APP_SHORT_NAME,
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo inst_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = NULL,
    .flags                   = (portabilityEnumerationActive ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0),
    .pApplicationInfo        = &app,
    .enabledLayerCount       = 0, //demo->enabled_layer_count,
    .ppEnabledLayerNames     = NULL, //(const char *const *)instance_validation_layers,
    .enabledExtensionCount   = 0,//demo->enabled_extension_count,
    .ppEnabledExtensionNames = NULL //(const char *const *)demo->extension_names,
  };

  err = vkCreateInstance(&inst_info, NULL, &vkinst);

  if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
    ERR_EXIT(
             "Cannot find a compatible Vulkan installable client driver (ICD).\n\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
  } else if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
    ERR_EXIT(
             "Cannot find a specified extension library.\n"
             "Make sure your layers path is set appropriately.\n",
             "vkCreateInstance Failure");
  } else if (err) {
    ERR_EXIT("vkCreateInstance failed.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
  }

  inst        = calloc(1, sizeof(*inst));
  inst->_priv = vkinst;

  return inst;
}

GPU_HIDE
void
vk_initInstance(GPUApiInstance *api) {
  api->createInstance = vk_createInstance;
}
