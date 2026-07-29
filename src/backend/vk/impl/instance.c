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
#if GPU_BUILD_WITH_VALIDATION
#  include "debug.h"
#endif

#if GPU_BUILD_WITH_VALIDATION
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
#endif

static bool
vk__hasExtension(const VkExtensionProperties *extensions,
                 uint32_t                     extensionCount,
                 const char                  *name) {
  for (uint32_t i = 0u; i < extensionCount; i++) {
    if (strcmp(extensions[i].extensionName, name) == 0) {
      return true;
    }
  }
  return false;
}

static GPUInstance *
vk__instanceFail(GPUInstance   *gpuInst,
                 GPUInstanceVk *gpuInstVk,
                 VkResult       result,
                 const char    *message) {
  if (message) {
    fprintf(stderr, "%s (VkResult %d)\n", message, result);
  }
  if (gpuInstVk) {
#if GPU_BUILD_WITH_VALIDATION
    if (gpuInstVk->DestroyDebugUtilsMessengerEXT &&
        gpuInstVk->dbg_messenger) {
      gpuInstVk->DestroyDebugUtilsMessengerEXT(gpuInstVk->inst,
                                               gpuInstVk->dbg_messenger,
                                               NULL);
    }
#endif
    if (gpuInstVk->inst) {
      vkDestroyInstance(gpuInstVk->inst, NULL);
    }
  }
  free(gpuInstVk);
  free(gpuInst);
  return NULL;
}

static uint32_t
vk__apiVersion(void) {
  PFN_vkEnumerateInstanceVersion enumerateVersion;
  uint32_t                       version;

  version = VK_API_VERSION_1_0;
  enumerateVersion = (PFN_vkEnumerateInstanceVersion)
    vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion");
  if (enumerateVersion && enumerateVersion(&version) != VK_SUCCESS) {
    version = VK_API_VERSION_1_0;
  }
  return version > VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : version;
}

GPU_HIDE
GPUInstance*
vk_createInstance(GPUApi * __restrict api,
                  const GPUInstanceCreateInfo * __restrict info) {
  GPUInstance           *gpuInst;
  GPUInstanceVk         *gpuInstVk;
  const char            *enabledExtensions[16] = {0};
#if GPU_BUILD_WITH_VALIDATION
  char                  *validationLayers[] = {"VK_LAYER_KHRONOS_validation"};
#endif
  VkExtensionProperties *instanceExtensions;
#if GPU_BUILD_WITH_VALIDATION
  VkLayerProperties     *instanceLayers;
#endif
  VkInstance             inst;
  VkResult               err;
  uint32_t               nEnabledExtensions, nEnabledLayers;
#if GPU_BUILD_WITH_VALIDATION
  uint32_t               nInstanceExtensions, nInstanceLayers;
#else
  uint32_t               nInstanceExtensions;
#endif
  uint32_t               apiVersion;
#if GPU_BUILD_WITH_VALIDATION
  VkBool32               validationFound;
  bool                   validate;
#endif
  bool                   portabilityEnum;

  GPU__UNUSED(api);

#if GPU_BUILD_WITH_VALIDATION
  validate                = info ? info->enableValidation : false;
#endif

  portabilityEnum         = false;
  nInstanceExtensions     = 0;
#if GPU_BUILD_WITH_VALIDATION
  nInstanceLayers         = 0;
  validationFound         = 0;
#endif
  nEnabledExtensions      = 0;
  nEnabledLayers          = 0;
  apiVersion              = vk__apiVersion();

  gpuInst        = calloc(1, sizeof(*gpuInst));
  gpuInstVk      = calloc(1, sizeof(*gpuInstVk));
  if (!gpuInst || !gpuInstVk) {
    free(gpuInst);
    free(gpuInstVk);
    return NULL;
  }
  gpuInst->_priv = gpuInstVk;
  if (info) {
    gpuInst->createInfo = *info;
  }

#if GPU_BUILD_WITH_VALIDATION
  /* Look for validation layers */
  if (validate) {
    err = vkEnumerateInstanceLayerProperties(&nInstanceLayers, NULL);
    if (err != VK_SUCCESS) {
      return vk__instanceFail(gpuInst,
                              gpuInstVk,
                              err,
                              "Vulkan validation-layer enumeration failed");
    }

    if (nInstanceLayers > 0) {
      instanceLayers  = malloc(sizeof(*instanceLayers) * nInstanceLayers);
      if (!instanceLayers) {
        return vk__instanceFail(gpuInst,
                                gpuInstVk,
                                VK_ERROR_OUT_OF_HOST_MEMORY,
                                "Vulkan validation-layer allocation failed");
      }
      err             = vkEnumerateInstanceLayerProperties(&nInstanceLayers, instanceLayers);
      if (err != VK_SUCCESS && err != VK_INCOMPLETE) {
        free(instanceLayers);
        return vk__instanceFail(gpuInst,
                                gpuInstVk,
                                err,
                                "Vulkan validation-layer enumeration failed");
      }

      validationFound = vk__checkLayers(GPU_ARRAY_LEN(validationLayers), 
                                        validationLayers,
                                        nInstanceLayers, 
                                        instanceLayers);
      if (validationFound) {
        nEnabledLayers = GPU_ARRAY_LEN(validationLayers);
      }
      free(instanceLayers);
    }

    if (!validationFound) {
      return vk__instanceFail(gpuInst,
                              gpuInstVk,
                              VK_ERROR_LAYER_NOT_PRESENT,
                              "Vulkan validation layer is unavailable");
    }
  }
#endif

  err = vkEnumerateInstanceExtensionProperties(NULL, &nInstanceExtensions, NULL);
  if (err != VK_SUCCESS) {
    return vk__instanceFail(gpuInst,
                            gpuInstVk,
                            err,
                            "Vulkan instance-extension enumeration failed");
  }

  if (nInstanceExtensions > 0) {
    instanceExtensions = malloc(sizeof(*instanceExtensions) * nInstanceExtensions);
    if (!instanceExtensions) {
      return vk__instanceFail(gpuInst,
                              gpuInstVk,
                              VK_ERROR_OUT_OF_HOST_MEMORY,
                              "Vulkan extension allocation failed");
    }
    err                = vkEnumerateInstanceExtensionProperties(NULL, 
                                                                &nInstanceExtensions,
                                                                instanceExtensions);
    if (err != VK_SUCCESS && err != VK_INCOMPLETE) {
      free(instanceExtensions);
      return vk__instanceFail(gpuInst,
                              gpuInstVk,
                              err,
                              "Vulkan instance-extension enumeration failed");
    }

    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_KHR_SURFACE_EXTENSION_NAME)) {
      enabledExtensions[nEnabledExtensions++] = VK_KHR_SURFACE_EXTENSION_NAME;
    }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_KHR_WIN32_SURFACE_EXTENSION_NAME)) {
      enabledExtensions[nEnabledExtensions++] =
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
    }
#endif
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_KHR_ANDROID_SURFACE_EXTENSION_NAME)) {
      enabledExtensions[nEnabledExtensions++] =
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
    }
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_EXT_METAL_SURFACE_EXTENSION_NAME)) {
      enabledExtensions[nEnabledExtensions++] =
        VK_EXT_METAL_SURFACE_EXTENSION_NAME;
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_KHR_XLIB_SURFACE_EXTENSION_NAME)) {
      enabledExtensions[nEnabledExtensions++] =
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME)) {
      enabledExtensions[nEnabledExtensions++] =
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
    }
#endif

    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
      enabledExtensions[nEnabledExtensions++] =
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    }

    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
#if GPU_BUILD_WITH_DEBUG_MARKERS
      enabledExtensions[nEnabledExtensions++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
      gpuInstVk->debugUtilsEnabled = true;
#elif GPU_BUILD_WITH_VALIDATION
      if (validate) {
        enabledExtensions[nEnabledExtensions++] =
          VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        gpuInstVk->debugUtilsEnabled = true;
      }
#endif
    }

    if (vk__hasExtension(instanceExtensions,
                         nInstanceExtensions,
                         VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
      portabilityEnum = true;
      enabledExtensions[nEnabledExtensions++] =
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    }

    free(instanceExtensions);
  }

#if GPU_BUILD_WITH_VALIDATION
  VkDebugUtilsMessengerCreateInfoEXT debugCI = {
    .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .pNext           = NULL,
    .flags           = 0,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
    .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
    .pfnUserCallback = vk__debug_messengercb,
    .pUserData       = gpuInst
  };
#endif
  VkInstanceCreateInfo instCI = {
    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = (portabilityEnum ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0),
    .pApplicationInfo        = &(VkApplicationInfo){
      .sType                 = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext                 = NULL,
      .pApplicationName      = GPU_VK_APP_NAME,
      .applicationVersion    = 0,
      .pEngineName           = GPU_VK_APP_NAME,
      .engineVersion         = 0,
      .apiVersion            = apiVersion,
    },
    .enabledLayerCount       = nEnabledLayers,
#if GPU_BUILD_WITH_VALIDATION
    .ppEnabledLayerNames     = (const char *const *)validationLayers,
#else
    .ppEnabledLayerNames     = NULL,
#endif
    .enabledExtensionCount   = nEnabledExtensions,
    .ppEnabledExtensionNames = enabledExtensions,
  };

#if GPU_BUILD_WITH_VALIDATION
  /*
   * This is info for a temp callback to use during CreateInstance.
   * After the instance is created, we use the instance-based
   * function to register the final callback.
  */
  if (validate) {
    instCI.pNext = &debugCI;
  }
#endif

  err = vkCreateInstance(&instCI, NULL, &inst);
  if (err == VK_ERROR_INCOMPATIBLE_DRIVER) {
    return vk__instanceFail(gpuInst,
                            gpuInstVk,
                            err,
                            "No compatible Vulkan driver was found");
  } else if (err == VK_ERROR_EXTENSION_NOT_PRESENT) {
    return vk__instanceFail(gpuInst,
                            gpuInstVk,
                            err,
                            "A required Vulkan instance extension is unavailable");
  } else if (err) {
    return vk__instanceFail(gpuInst,
                            gpuInstVk,
                            err,
                            "Vulkan instance creation failed");
  }

  gpuInstVk->inst       = inst;
  gpuInstVk->apiVersion = apiVersion;
  
#if GPU_BUILD_WITH_VALIDATION
  if (validate) {
    /* Setup the validation messenger. */
    gpuInstVk->CreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(gpuInstVk->inst, "vkCreateDebugUtilsMessengerEXT");
    gpuInstVk->DestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)
        vkGetInstanceProcAddr(gpuInstVk->inst, "vkDestroyDebugUtilsMessengerEXT");
    gpuInstVk->SubmitDebugUtilsMessageEXT = (PFN_vkSubmitDebugUtilsMessageEXT)
        vkGetInstanceProcAddr(gpuInstVk->inst, "vkSubmitDebugUtilsMessageEXT");

    if (gpuInstVk->CreateDebugUtilsMessengerEXT == NULL
        || gpuInstVk->DestroyDebugUtilsMessengerEXT == NULL
        || gpuInstVk->SubmitDebugUtilsMessageEXT == NULL) {
      return vk__instanceFail(gpuInst,
                              gpuInstVk,
                              VK_ERROR_EXTENSION_NOT_PRESENT,
                              "Vulkan debug-utils entry points are unavailable");
    }

    err = gpuInstVk->CreateDebugUtilsMessengerEXT(gpuInstVk->inst, 
                                                  instCI.pNext,
                                                  NULL,
                                                  &gpuInstVk->dbg_messenger);
    switch (err) {
      case VK_SUCCESS:
        break;
      case VK_ERROR_OUT_OF_HOST_MEMORY:
        return vk__instanceFail(gpuInst,
                                gpuInstVk,
                                err,
                                "Vulkan debug-messenger allocation failed");
      default:
        return vk__instanceFail(gpuInst,
                                gpuInstVk,
                                err,
                                "Vulkan debug-messenger creation failed");
    }
  }
#endif

#if GPU_BUILD_WITH_DEBUG_MARKERS
  if (gpuInstVk->debugUtilsEnabled) {
    gpuInstVk->CmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)
      vkGetInstanceProcAddr(gpuInstVk->inst, "vkCmdBeginDebugUtilsLabelEXT");
    gpuInstVk->CmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)
      vkGetInstanceProcAddr(gpuInstVk->inst, "vkCmdEndDebugUtilsLabelEXT");
    gpuInstVk->CmdInsertDebugUtilsLabelEXT = (PFN_vkCmdInsertDebugUtilsLabelEXT)
      vkGetInstanceProcAddr(gpuInstVk->inst, "vkCmdInsertDebugUtilsLabelEXT");
    gpuInstVk->SetDebugUtilsObjectNameEXT = (PFN_vkSetDebugUtilsObjectNameEXT)
      vkGetInstanceProcAddr(gpuInstVk->inst, "vkSetDebugUtilsObjectNameEXT");
  }
#endif

  GET_INSTANCE_PROC_ADDR(gpuInstVk, GetPhysicalDeviceSurfaceSupportKHR);
  GET_INSTANCE_PROC_ADDR(gpuInstVk, GetPhysicalDeviceSurfaceCapabilitiesKHR);
  GET_INSTANCE_PROC_ADDR(gpuInstVk, GetPhysicalDeviceSurfaceFormatsKHR);
  GET_INSTANCE_PROC_ADDR(gpuInstVk, GetPhysicalDeviceSurfacePresentModesKHR);
  GET_INSTANCE_PROC_ADDR(gpuInstVk, GetSwapchainImagesKHR);

  return gpuInst;
}

GPU_HIDE
void
vk_destroyInstance(GPUApi * __restrict api, GPUInstance * __restrict inst) {
  GPUInstanceVk *instVk;

  GPU__UNUSED(api);

  if (!inst) {
    return;
  }

  instVk = inst->_priv;
  if (instVk) {
#if GPU_BUILD_WITH_VALIDATION
    if (instVk->DestroyDebugUtilsMessengerEXT && instVk->dbg_messenger) {
      instVk->DestroyDebugUtilsMessengerEXT(instVk->inst,
                                            instVk->dbg_messenger,
                                            NULL);
    }
#endif
    if (instVk->inst) {
      vkDestroyInstance(instVk->inst, NULL);
    }
    free(instVk);
  }
  free(inst);
}

GPU_HIDE
void
vk_initInstance(GPUApiInstance *api) {
  api->createInstance = vk_createInstance;
  api->destroyInstance = vk_destroyInstance;
}
