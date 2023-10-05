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

#ifndef vk_common_h
#define vk_common_h

#include "../../common.h"
#include <stdarg.h>
#include <assert.h>
#include <inttypes.h>

/* MoltenVK */
#ifdef __APPLE__
#  include <Availability.h>
#  define VK_USE_PLATFORM_METAL_EXT        1
#  define VK_ENABLE_BETA_EXTENSIONS        1    // VK_KHR_portability_subset
#  ifdef __IPHONE_OS_VERSION_MAX_ALLOWED
#    define VK_USE_PLATFORM_IOS_MVK        1
#  endif
#  ifdef __MAC_OS_X_VERSION_MAX_ALLOWED
#    define VK_USE_PLATFORM_MACOS_MVK      1
#  endif
#endif

#ifdef ANDROID
#  include "vulkan_wrapper.h"
#else
#  include <vulkan/vulkan.h>
#endif

#include "object_type_string_helper.h"

#define APP_SHORT_NAME "libgpu"
#define APP_LONG_NAME  "libgpu"

#if defined(NDEBUG) && defined(__GNUC__)
#  define U_ASSERT_ONLY __attribute__((unused))
#else
#  define U_ASSERT_ONLY
#endif

#if defined(__GNUC__)
#  define UNUSED __attribute__((unused))
#else
#  define UNUSED
#endif

#ifdef _WIN32
bool in_callback = false;
#define ERR_EXIT(err_msg, err_class)                                             \
    do {                                                                         \
        if (!demo->suppress_popups) MessageBox(NULL, err_msg, err_class, MB_OK); \
        exit(1);                                                                 \
    } while (0)
void DbgMsg(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    fflush(stdout);
}

#elif defined __ANDROID__
#include <android/log.h>
#define ERR_EXIT(err_msg, err_class)                                           \
    do {                                                                       \
        ((void)__android_log_print(ANDROID_LOG_INFO, "Vulkan Cube", err_msg)); \
        exit(1);                                                               \
    } while (0)
#ifdef VARARGS_WORKS_ON_ANDROID
void DbgMsg(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    __android_log_print(ANDROID_LOG_INFO, "Vulkan Cube", fmt, va);
    va_end(va);
}
#else  // VARARGS_WORKS_ON_ANDROID
#define DbgMsg(fmt, ...)                                                                  \
    do {                                                                                  \
        ((void)__android_log_print(ANDROID_LOG_INFO, "Vulkan Cube", fmt, ##__VA_ARGS__)); \
    } while (0)
#endif  // VARARGS_WORKS_ON_ANDROID
#else
#define ERR_EXIT(err_msg, err_class) \
    do {                             \
        printf("%s\n", err_msg);     \
        fflush(stdout);              \
        exit(1);                     \
    } while (0)
GPU_INLINE void DbgMsg(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    fflush(stdout);
}
#endif

#define GET_INSTANCE_PROC_ADDR(gpuInstVk, entrypoint)                         \
  gpuInstVk->fp##entrypoint = (PFN_vk##entrypoint)                            \
      vkGetInstanceProcAddr(gpuInstVk->inst, "vk" #entrypoint);               \

static PFN_vkGetDeviceProcAddr g_gdpa = NULL;

#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                                                                    \
    {                                                                                                            \
        if (!g_gdpa) g_gdpa = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(demo->inst, "vkGetDeviceProcAddr"); \
        demo->fp##entrypoint = (PFN_vk##entrypoint)g_gdpa(dev, "vk" #entrypoint);                                \
        if (demo->fp##entrypoint == NULL) {                                                                      \
            ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint, "vkGetDeviceProcAddr Failure");        \
        }                                                                                                        \
    }

typedef struct GPUInstanceVk {
  char       *extensionNames[64];
  char       *enabledLayers[64];
  VkInstance  inst;
  bool        invalid_gpu_selection;
  int32_t     gpu_number;
  uint32_t    nEnabledExtensions;
  uint32_t    nEnabledLayers;

  PFN_vkGetPhysicalDeviceSurfaceSupportKHR      fpGetPhysicalDeviceSurfaceSupportKHR;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR      fpGetPhysicalDeviceSurfaceFormatsKHR;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
  PFN_vkCreateSwapchainKHR                      fpCreateSwapchainKHR;
  PFN_vkDestroySwapchainKHR                     fpDestroySwapchainKHR;
  PFN_vkGetSwapchainImagesKHR                   fpGetSwapchainImagesKHR;
  PFN_vkAcquireNextImageKHR                     fpAcquireNextImageKHR;
  PFN_vkQueuePresentKHR                         fpQueuePresentKHR;
  PFN_vkGetRefreshCycleDurationGOOGLE           fpGetRefreshCycleDurationGOOGLE;
  PFN_vkGetPastPresentationTimingGOOGLE         fpGetPastPresentationTimingGOOGLE;

  /* DEBUG helpers */
  PFN_vkCreateDebugUtilsMessengerEXT            CreateDebugUtilsMessengerEXT;
  PFN_vkDestroyDebugUtilsMessengerEXT           DestroyDebugUtilsMessengerEXT;
  PFN_vkSubmitDebugUtilsMessageEXT              SubmitDebugUtilsMessageEXT;
  PFN_vkCmdBeginDebugUtilsLabelEXT              CmdBeginDebugUtilsLabelEXT;
  PFN_vkCmdEndDebugUtilsLabelEXT                CmdEndDebugUtilsLabelEXT;
  PFN_vkCmdInsertDebugUtilsLabelEXT             CmdInsertDebugUtilsLabelEXT;
  PFN_vkSetDebugUtilsObjectNameEXT              SetDebugUtilsObjectNameEXT;
  VkDebugUtilsMessengerEXT                      dbg_messenger;
} GPUInstanceVk;

typedef struct GPUPhysicalDeviceVk {
  char                      *extensionNames[64];
  VkQueueFamilyProperties   *queueFamilyProps;
  VkPhysicalDevice           phyDevice;
  uint32_t                   queueFamilyCount;
  uint32_t                   nEnabledExtensions;
  VkPhysicalDeviceProperties props;
  VkPhysicalDeviceFeatures   physDevFeatures;
} GPUPhysicalDeviceVk;

#endif /* vk_common_h */
