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

#include "debug.h"

static const char *
vk_debugSeverityName(VkDebugUtilsMessageSeverityFlagBitsEXT severity) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    return "ERROR";
  }
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    return "WARNING";
  }
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    return "INFO";
  }
  return "VERBOSE";
}

static const char *
vk_debugTypeName(VkDebugUtilsMessageTypeFlagsEXT type) {
  if ((type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) &&
      (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) {
    return "VALIDATION|PERFORMANCE";
  }
  if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    return "VALIDATION";
  }
  if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    return "PERFORMANCE";
  }
  return "GENERAL";
}

static void
vk_debugLog(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            const char                                *format,
            ...) {
  va_list args;

  va_start(args, format);
#if defined(__ANDROID__)
  int priority;

  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    priority = ANDROID_LOG_ERROR;
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    priority = ANDROID_LOG_WARN;
  } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    priority = ANDROID_LOG_INFO;
  } else {
    priority = ANDROID_LOG_VERBOSE;
  }
  __android_log_vprint(priority, APP_SHORT_NAME, format, args);
#else
  vfprintf(stderr, format, args);
  fflush(stderr);
#endif
  va_end(args);
}

VKAPI_ATTR
VkBool32
VKAPI_CALL
vk__debug_messengercb(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                      VkDebugUtilsMessageTypeFlagsEXT             messageType,
                      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                      void                                       *pUserData) {
  GPUInstance *inst;

  if (!(inst = pUserData) || !pCallbackData) {
    return false;
  }

  if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    inst->validationError = 1;
  }

  vk_debugLog(messageSeverity,
              "%s : %s - Message Id Number: %d | Message Id Name: %s\n"
              "\t%s\n",
              vk_debugSeverityName(messageSeverity),
              vk_debugTypeName(messageType),
              pCallbackData->messageIdNumber,
              pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "",
              pCallbackData->pMessage ? pCallbackData->pMessage : "");

  if (pCallbackData->objectCount > 0) {
    vk_debugLog(messageSeverity,
                "\n\tObjects - %u\n",
                pCallbackData->objectCount);
    for (uint32_t object = 0; object < pCallbackData->objectCount; ++object) {
      const VkDebugUtilsObjectNameInfoEXT *info;
      VkObjectType                         type;

      info = &pCallbackData->pObjects[object];
      type = info->objectType;
      vk_debugLog(messageSeverity,
                  "\t\tObject[%u] - %s",
                  object,
                  string_VkObjectType(type));
      if (type == VK_OBJECT_TYPE_INSTANCE ||
          type == VK_OBJECT_TYPE_PHYSICAL_DEVICE ||
          type == VK_OBJECT_TYPE_DEVICE ||
          type == VK_OBJECT_TYPE_COMMAND_BUFFER ||
          type == VK_OBJECT_TYPE_QUEUE) {
        vk_debugLog(messageSeverity,
                    ", Handle %p",
                    (void *)(uintptr_t)info->objectHandle);
      } else {
        vk_debugLog(messageSeverity,
                    ", Handle 0x%" PRIx64,
                    info->objectHandle);
      }
      if (info->pObjectName && info->pObjectName[0] != '\0') {
        vk_debugLog(messageSeverity, ", Name \"%s\"", info->pObjectName);
      }
      vk_debugLog(messageSeverity, "\n");
    }
  }

  if (pCallbackData->cmdBufLabelCount > 0) {
    vk_debugLog(messageSeverity,
                "\n\tCommand Buffer Labels - %u\n",
                pCallbackData->cmdBufLabelCount);
    for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; ++i) {
      const VkDebugUtilsLabelEXT *label;

      label = &pCallbackData->pCmdBufLabels[i];
      vk_debugLog(messageSeverity,
                  "\t\tLabel[%u] - %s { %f, %f, %f, %f}\n",
                  i,
                  label->pLabelName ? label->pLabelName : "",
                  label->color[0],
                  label->color[1],
                  label->color[2],
                  label->color[3]);
    }
  }
  return false;
}
