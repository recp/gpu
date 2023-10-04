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
#include "debug.h"

/*
 * Return 1 (true) if all layer names specified in check_names
 * can be found in given layer properties.
 */
GPU_HIDE
static VkBool32
vk__checkLayers(uint32_t           check_count,
                char             **check_names,
                uint32_t           layer_count,
                VkLayerProperties *layers) {
  uint32_t i, j;
  VkBool32 found;

  for (i = 0; i < check_count; i++) {
    found = 0;
    for (j = 0; j < layer_count; j++) {
      if (!strcmp(check_names[i], layers[j].layerName)) {
        found = 1;
        break;
      }
    }

    if (!found) {
      fprintf(stderr, "Cannot find layer: %s\n", check_names[i]);
      return 0;
    }
  }

  return 1;
}

GPUInitParams gpu__defaultInitParams = {
  .requiredFeatures    = GPU_FEATURE_DEFAULT,
  .optionalFeatures    = 0,
  .validation          = false,
  .validation_usebreak = false
};

GPU_HIDE
GPUInstance*
vk_createInstance(GPUApi * __restrict api, GPUInitParams * __restrict params) {
  GPUInstance           *gpuInst;
  GPUInstanceVk         *gpuInstVk;
  char                  *extensionNames[64];
  char                  *enabledLayers[64];
  char                  *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
  VkExtensionProperties *instanceExtensions;
  VkLayerProperties     *instanceLayers;
  VkInstance             inst;
  VkResult               err;
  uint32_t               i, nEnabledExtensions, nEnabledLayers;
  uint32_t               nInstanceExtensions, nInstanceLayers;
  VkBool32               surfaceExtFound, platformSurfaceExtFound, validationFound;
  bool                   surfaceExtRequired, swapchainExtRequired, portabilityEnum, validate;

  if (!params) { params = &gpu__defaultInitParams; }

  validate                = params->validation;
  surfaceExtRequired      = (params->requiredFeatures & GPU_FEATURE_SURFACE)   != 0;
  swapchainExtRequired    = (params->requiredFeatures & GPU_FEATURE_SWAPCHAIN) != 0;

  portabilityEnum         = false;
  surfaceExtFound         = 0;
  platformSurfaceExtFound = 0;
  nInstanceExtensions     = 0;
  nInstanceLayers         = 0;
  validationFound         = 0;
  nEnabledExtensions      = 0;
  nEnabledLayers          = 0;

  gpuInst        = calloc(1, sizeof(*gpuInst));
  gpuInstVk      = calloc(1, sizeof(*gpuInstVk));
  gpuInst->_priv = gpuInstVk;

  /* Look for validation layers */
  if (validate) {
    err = vkEnumerateInstanceLayerProperties(&nInstanceLayers, NULL);
    assert(!err);

    if (nInstanceLayers > 0) {
      instanceLayers  = malloc(sizeof(*instanceLayers) * nInstanceLayers);
      err             = vkEnumerateInstanceLayerProperties(&nInstanceLayers, instanceLayers);
      assert(!err);

      validationFound = vk__checkLayers(GPU_ARRAY_LEN(validationLayers), 
                                        validationLayers,
                                        nInstanceLayers, 
                                        instanceLayers);
      if (validationFound) {
        nEnabledLayers = GPU_ARRAY_LEN(validationLayers);
        enabledLayers[0]   = "VK_LAYER_KHRONOS_validation";
      }
      free(instanceLayers);
    }

    if (!validationFound) {
      ERR_EXIT("vkEnumerateInstanceLayerProperties failed to find required validation layer.\n\n"
               "Please look at the Getting Started guide for additional information.\n",
               "vkCreateInstance Failure");
    }
  }

  /* Look for instance extensions */
  memset(extensionNames, 0, sizeof(extensionNames));

  err = vkEnumerateInstanceExtensionProperties(NULL, &nInstanceExtensions, NULL);
  assert(!err);

  if (nInstanceExtensions > 0) {
    instanceExtensions = malloc(sizeof(*instanceExtensions) * nInstanceExtensions);
    err                = vkEnumerateInstanceExtensionProperties(NULL, 
                                                                &nInstanceExtensions,
                                                                instanceExtensions);
    assert(!err);

    for (i = 0; i < nInstanceExtensions; i++) {
      if (!strcmp(VK_KHR_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        surfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_KHR_SURFACE_EXTENSION_NAME;
      }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
      if (!strcmp(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
      if (!strcmp(VK_KHR_XLIB_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_XCB_KHR)
      if (!strcmp(VK_KHR_XCB_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
      if (!strcmp(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
      if (!strcmp(VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
      if (!strcmp(VK_KHR_DISPLAY_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_KHR_DISPLAY_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
      if (!strcmp(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_METAL_EXT)
      if (!strcmp(VK_EXT_METAL_SURFACE_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extensionNames[nEnabledExtensions++] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
      }
#endif

      if (!strcmp(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        extensionNames[nEnabledExtensions++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
      }

      if (!strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        if (validate) {
          extensionNames[nEnabledExtensions++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }
      }

      // We want cube to be able to enumerate drivers that support the portability_subset extension, so we have to enable the
      // portability enumeration extension.
      if (!strcmp(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, instanceExtensions[i].extensionName)) {
        portabilityEnum = true;
        extensionNames[nEnabledExtensions++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
      }
      assert(nEnabledExtensions < 64);
    }

    free(instanceExtensions);
  }

  if (!surfaceExtFound) {
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_SURFACE_EXTENSION_NAME
             " extension.\n\n",
             "vkCreateInstance Failure");
  }

  if (!platformSurfaceExtFound) {
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the platform surface extension.\n\n",
             "vkCreateInstance Failure");
  }

  VkInstanceCreateInfo instCI = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = (portabilityEnum ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0),
    .pApplicationInfo        = &(VkApplicationInfo){
      .sType                 = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext                 = NULL,
      .pApplicationName      = APP_SHORT_NAME,
      .applicationVersion    = 0,
      .pEngineName           = APP_SHORT_NAME,
      .engineVersion         = 0,
      .apiVersion            = VK_API_VERSION_1_0,
    },
    .enabledLayerCount       = nEnabledLayers,
    .ppEnabledLayerNames     = (const char *const *)validationLayers,
    .enabledExtensionCount   = nEnabledExtensions,
    .ppEnabledExtensionNames = (const char *const *)extensionNames,
  };

  /*
   * This is info for a temp callback to use during CreateInstance.
   * After the instance is created, we use the instance-based
   * function to register the final callback.
   */
  if (validate) {
    // VK_EXT_debug_utils style
    instCI.pNext = &(VkDebugUtilsMessengerCreateInfoEXT) {
      .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .pNext           = NULL,
      .flags           = 0,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT 
                       | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = vk__debug_messengercb,
      .pUserData       = &inst
    };
  }

  err = vkCreateInstance(&instCI, NULL, &inst);
  if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
    ERR_EXIT("Cannot find a compatible Vulkan installable client driver (ICD).\n\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
  } else if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
    ERR_EXIT("Cannot find a specified extension library.\n"
             "Make sure your layers path is set appropriately.\n",
             "vkCreateInstance Failure");
  } else if (err) {
    ERR_EXIT("vkCreateInstance failed.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
  }

  gpuInstVk->inst = inst;

  return gpuInst;
}

GPU_HIDE
void
vk_initInstance(GPUApiInstance *api) {
  api->createInstance = vk_createInstance;
}
