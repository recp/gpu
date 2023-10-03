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
#include <signal.h>

VKAPI_ATTR VkBool32 VKAPI_CALL 
debug_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                         VkDebugUtilsMessageTypeFlagsEXT messageType,
                         const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                         void *pUserData) {
  char prefix[64] = "";
  char *message = (char *)malloc(strlen(pCallbackData->pMessage) + 5000);
  assert(message);
  GPUInstance *inst = (struct GPUInstance *)pUserData;
  GPUInitParams *initParams;

  if ((initParams = inst->initParams)) {
    return false;
  }

  if (initParams->validation_usebreak) {
#ifndef WIN32
    raise(SIGTRAP);
#else
    DebugBreak();
#endif
  }

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    strcat(prefix, "VERBOSE : ");
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    strcat(prefix, "INFO : ");
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    strcat(prefix, "WARNING : ");
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    strcat(prefix, "ERROR : ");
  }

  if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
    strcat(prefix, "GENERAL");
  } else {
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
      strcat(prefix, "VALIDATION");
      inst->validationError = 1;
    }
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
      if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
        strcat(prefix, "|");
      }
      strcat(prefix, "PERFORMANCE");
    }
  }

  sprintf(message, "%s - Message Id Number: %d | Message Id Name: %s\n\t%s\n", prefix, pCallbackData->messageIdNumber,
          pCallbackData->pMessageIdName == NULL ? "" : pCallbackData->pMessageIdName, pCallbackData->pMessage);
  if (pCallbackData->objectCount > 0) {
    char tmp_message[500];
    sprintf(tmp_message, "\n\tObjects - %d\n", pCallbackData->objectCount);
    strcat(message, tmp_message);
    for (uint32_t object = 0; object < pCallbackData->objectCount; ++object) {
      sprintf(tmp_message, "\t\tObject[%d] - %s", object, string_VkObjectType(pCallbackData->pObjects[object].objectType));
      strcat(message, tmp_message);

      VkObjectType t = pCallbackData->pObjects[object].objectType;
      if (t == VK_OBJECT_TYPE_INSTANCE || t == VK_OBJECT_TYPE_PHYSICAL_DEVICE || t == VK_OBJECT_TYPE_DEVICE ||
          t == VK_OBJECT_TYPE_COMMAND_BUFFER || t == VK_OBJECT_TYPE_QUEUE) {
        sprintf(tmp_message, ", Handle %p", (void *)(uintptr_t)(pCallbackData->pObjects[object].objectHandle));
        strcat(message, tmp_message);
      } else {
        sprintf(tmp_message, ", Handle Ox%" PRIx64, (pCallbackData->pObjects[object].objectHandle));
        strcat(message, tmp_message);
      }

      if (NULL != pCallbackData->pObjects[object].pObjectName && strlen(pCallbackData->pObjects[object].pObjectName) > 0) {
        sprintf(tmp_message, ", Name \"%s\"", pCallbackData->pObjects[object].pObjectName);
        strcat(message, tmp_message);
      }
      sprintf(tmp_message, "\n");
      strcat(message, tmp_message);
    }
  }
  if (pCallbackData->cmdBufLabelCount > 0) {
    char tmp_message[500];
    sprintf(tmp_message, "\n\tCommand Buffer Labels - %d\n", pCallbackData->cmdBufLabelCount);
    strcat(message, tmp_message);
    for (uint32_t cmd_buf_label = 0; cmd_buf_label < pCallbackData->cmdBufLabelCount; ++cmd_buf_label) {
      sprintf(tmp_message, "\t\tLabel[%d] - %s { %f, %f, %f, %f}\n", cmd_buf_label,
              pCallbackData->pCmdBufLabels[cmd_buf_label].pLabelName, pCallbackData->pCmdBufLabels[cmd_buf_label].color[0],
              pCallbackData->pCmdBufLabels[cmd_buf_label].color[1], pCallbackData->pCmdBufLabels[cmd_buf_label].color[2],
              pCallbackData->pCmdBufLabels[cmd_buf_label].color[3]);
      strcat(message, tmp_message);
    }
  }

#ifdef _WIN32

  in_callback = true;
  if (!demo->suppress_popups) MessageBox(NULL, message, "Alert", MB_OK);
  in_callback = false;

#elif defined(ANDROID)

  if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    __android_log_print(ANDROID_LOG_INFO, APP_SHORT_NAME, "%s", message);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    __android_log_print(ANDROID_LOG_WARN, APP_SHORT_NAME, "%s", message);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    __android_log_print(ANDROID_LOG_ERROR, APP_SHORT_NAME, "%s", message);
  } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    __android_log_print(ANDROID_LOG_VERBOSE, APP_SHORT_NAME, "%s", message);
  } else {
    __android_log_print(ANDROID_LOG_INFO, APP_SHORT_NAME, "%s", message);
  }

#else

  printf("%s\n", message);
  fflush(stdout);

#endif

  free(message);

  // Don't bail out, but keep going.
  return false;
}

/*
 * Return 1 (true) if all layer names specified in check_names
 * can be found in given layer properties.
 */
static VkBool32 demo_check_layers(uint32_t check_count, char **check_names, uint32_t layer_count, VkLayerProperties *layers) {
  for (uint32_t i = 0; i < check_count; i++) {
    VkBool32 found = 0;
    for (uint32_t j = 0; j < layer_count; j++) {
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
  GPUInstance *inst;
  char        *extension_names[64];
  char        *enabled_layers[64];
  char        *instance_validation_layers[] = {"VK_LAYER_KHRONOS_validation"};
  VkInstance   vkinst;
  VkResult     err;
  uint32_t     enabled_extension_count, enabled_layer_count;
  uint32_t     instance_extension_count, instance_layer_count;
  VkBool32     surfaceExtFound, platformSurfaceExtFound, validationFound;
  bool         surfaceExtRequired, swapchainExtRequired, portabilityEnum, validate;

  if (!params) { params = &gpu__defaultInitParams; }

  validate                 = params->validation;
  surfaceExtRequired       = (params->requiredFeatures & GPU_FEATURE_SURFACE)   != 0;
  swapchainExtRequired     = (params->requiredFeatures & GPU_FEATURE_SWAPCHAIN) != 0;

  portabilityEnum          = false;
  surfaceExtFound          = 0;
  platformSurfaceExtFound  = 0;
  instance_extension_count = 0;
  instance_layer_count     = 0;
  validationFound          = 0;
  enabled_extension_count  = 0;
  enabled_layer_count      = 0;

  inst        = calloc(1, sizeof(*inst));

  /* Look for validation layers */
  if (validate) {
    err = vkEnumerateInstanceLayerProperties(&instance_layer_count, NULL);
    assert(!err);

    if (instance_layer_count > 0) {
      VkLayerProperties *instance_layers = malloc(sizeof(VkLayerProperties) * instance_layer_count);
      err = vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers);
      assert(!err);

      validationFound = demo_check_layers(GPU_ARRAY_LEN(instance_validation_layers), instance_validation_layers,
                                           instance_layer_count, instance_layers);
      if (validationFound) {
        enabled_layer_count = GPU_ARRAY_LEN(instance_validation_layers);
        enabled_layers[0]   = "VK_LAYER_KHRONOS_validation";
      }
      free(instance_layers);
    }

    if (!validationFound) {
      ERR_EXIT(
               "vkEnumerateInstanceLayerProperties failed to find required validation layer.\n\n"
               "Please look at the Getting Started guide for additional information.\n",
               "vkCreateInstance Failure");
    }
  }

  /* Look for instance extensions */
  memset(extension_names, 0, sizeof(extension_names));

  err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, NULL);
  assert(!err);

  if (instance_extension_count > 0) {
    VkExtensionProperties *instance_extensions = malloc(sizeof(VkExtensionProperties) * instance_extension_count);
    err = vkEnumerateInstanceExtensionProperties(NULL, &instance_extension_count, instance_extensions);
    assert(!err);
    for (uint32_t i = 0; i < instance_extension_count; i++) {
      if (!strcmp(VK_KHR_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        surfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_KHR_SURFACE_EXTENSION_NAME;
      }
#if defined(VK_USE_PLATFORM_WIN32_KHR)
      if (!strcmp(VK_KHR_WIN32_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_KHR_WIN32_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
      if (!strcmp(VK_KHR_XLIB_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_KHR_XLIB_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_XCB_KHR)
      if (!strcmp(VK_KHR_XCB_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_KHR_XCB_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
      if (!strcmp(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
      if (!strcmp(VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
      if (!strcmp(VK_KHR_DISPLAY_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_KHR_DISPLAY_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
      if (!strcmp(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME;
      }
#elif defined(VK_USE_PLATFORM_METAL_EXT)
      if (!strcmp(VK_EXT_METAL_SURFACE_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        platformSurfaceExtFound = 1;
        extension_names[enabled_extension_count++] = VK_EXT_METAL_SURFACE_EXTENSION_NAME;
      }
#endif
      if (!strcmp(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        extension_names[enabled_extension_count++] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
      }

      if (!strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        if (validate) {
          extension_names[enabled_extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        }
      }
      // We want cube to be able to enumerate drivers that support the portability_subset extension, so we have to enable the
      // portability enumeration extension.
      if (!strcmp(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME, instance_extensions[i].extensionName)) {
        portabilityEnum = true;
        extension_names[enabled_extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
      }
      assert(enabled_extension_count < 64);
    }

    free(instance_extensions);
  }

  if (!surfaceExtFound) {
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
  }

  if (!platformSurfaceExtFound) {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_WIN32_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_METAL_EXT)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_EXT_METAL_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_XCB_KHR)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_XCB_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_DISPLAY_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_KHR_XLIB_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
    ERR_EXIT("vkEnumerateInstanceExtensionProperties failed to find the " VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME
             " extension.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
#endif
  }
  const VkApplicationInfo app = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext = NULL,
    .pApplicationName = APP_SHORT_NAME,
    .applicationVersion = 0,
    .pEngineName = APP_SHORT_NAME,
    .engineVersion = 0,
    .apiVersion = VK_API_VERSION_1_0,
  };
  VkInstanceCreateInfo inst_info = {
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext = NULL,
    .flags = (portabilityEnum ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0),
    .pApplicationInfo = &app,
    .enabledLayerCount = enabled_layer_count,
    .ppEnabledLayerNames = (const char *const *)instance_validation_layers,
    .enabledExtensionCount = enabled_extension_count,
    .ppEnabledExtensionNames = (const char *const *)extension_names,
  };


  /*
   * This is info for a temp callback to use during CreateInstance.
   * After the instance is created, we use the instance-based
   * function to register the final callback.
   */
  VkDebugUtilsMessengerCreateInfoEXT dbg_messenger_create_info;
  if (validate) {
    // VK_EXT_debug_utils style
    dbg_messenger_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    dbg_messenger_create_info.pNext = NULL;
    dbg_messenger_create_info.flags = 0;
    dbg_messenger_create_info.messageSeverity =
    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    dbg_messenger_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    dbg_messenger_create_info.pfnUserCallback = debug_messenger_callback;
    dbg_messenger_create_info.pUserData       = inst;
    inst_info.pNext = &dbg_messenger_create_info;
  }

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
    ERR_EXIT(
             "vkCreateInstance failed.\n\n"
             "Do you have a compatible Vulkan installable client driver (ICD) installed?\n"
             "Please look at the Getting Started guide for additional information.\n",
             "vkCreateInstance Failure");
  }

  inst->_priv = vkinst;

  return inst;
}

GPU_HIDE
void
vk_initInstance(GPUApiInstance *api) {
  api->createInstance = vk_createInstance;
}
