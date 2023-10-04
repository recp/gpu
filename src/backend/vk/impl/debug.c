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
#include <signal.h>

VKAPI_ATTR
VkBool32
VKAPI_CALL
vk__debug_messengercb(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                      VkDebugUtilsMessageTypeFlagsEXT             messageType,
                      const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                      void                                       *pUserData) {
  GPUInstance   *inst;
  GPUInitParams *initParams;
  char           tmp_message[500];
  char           prefix[64] = "";
  char          *message;

  if ((inst = pUserData) || (initParams = inst->initParams)) {
    return false;
  }

  assert((message = malloc(strlen(pCallbackData->pMessage) + 5000)));

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

  sprintf(message, "%s - Message Id Number: %d | Message Id Name: %s\n\t%s\n",
          prefix,
          pCallbackData->messageIdNumber,
          pCallbackData->pMessageIdName == NULL ? "" : pCallbackData->pMessageIdName,
          pCallbackData->pMessage);

  if (pCallbackData->objectCount > 0) {
    tmp_message[0] = '\0';

    sprintf(tmp_message, "\n\tObjects - %d\n", pCallbackData->objectCount);
    strcat(message, tmp_message);
    for (uint32_t object = 0; object < pCallbackData->objectCount; ++object) {
      sprintf(tmp_message, "\t\tObject[%d] - %s",
              object,
              string_VkObjectType(pCallbackData->pObjects[object].objectType));
      strcat(message, tmp_message);

      VkObjectType t = pCallbackData->pObjects[object].objectType;
      if (t == VK_OBJECT_TYPE_INSTANCE
          || t == VK_OBJECT_TYPE_PHYSICAL_DEVICE
          || t == VK_OBJECT_TYPE_DEVICE
          || t == VK_OBJECT_TYPE_COMMAND_BUFFER
          || t == VK_OBJECT_TYPE_QUEUE) {
        sprintf(tmp_message, ", Handle %p", (void *)(uintptr_t)(pCallbackData->pObjects[object].objectHandle));
        strcat(message, tmp_message);
      } else {
        sprintf(tmp_message, ", Handle Ox%" PRIx64, (pCallbackData->pObjects[object].objectHandle));
        strcat(message, tmp_message);
      }

      if (pCallbackData->pObjects[object].pObjectName
          && strlen(pCallbackData->pObjects[object].pObjectName) > 0) {
        sprintf(tmp_message, ", Name \"%s\"", pCallbackData->pObjects[object].pObjectName);
        strcat(message, tmp_message);
      }
      sprintf(tmp_message, "\n");
      strcat(message, tmp_message);
    }
  }

  if (pCallbackData->cmdBufLabelCount > 0) {
    tmp_message[0] = '\0';

    sprintf(tmp_message, "\n\tCommand Buffer Labels - %d\n", pCallbackData->cmdBufLabelCount);
    strcat(message, tmp_message);
    for (uint32_t cmd_buf_label = 0; cmd_buf_label < pCallbackData->cmdBufLabelCount; ++cmd_buf_label) {
      sprintf(tmp_message, "\t\tLabel[%d] - %s { %f, %f, %f, %f}\n",
              cmd_buf_label,
              pCallbackData->pCmdBufLabels[cmd_buf_label].pLabelName,
              pCallbackData->pCmdBufLabels[cmd_buf_label].color[0],
              pCallbackData->pCmdBufLabels[cmd_buf_label].color[1],
              pCallbackData->pCmdBufLabels[cmd_buf_label].color[2],
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
