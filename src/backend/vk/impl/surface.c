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
GPUSurface*
vk_createSurface(GPUApi            * __restrict api,
                 GPUInstance       * __restrict inst,
                 GPUAdapter        * __restrict adapter,
                 void              * __restrict nativeHandle,
                 GPUSurfaceType                 type,
                 float                          scale) {
  GPUInstanceVk *instVk;
  GPUSurface    *gpuSurface;
  GPUSurfaceVk  *surface;
  VkResult       err;

  GPU__UNUSED(api);

  if (!adapter || !nativeHandle) {
    return NULL;
  }

  if (!inst) {
    inst = adapter->inst;
  }
  if (!inst || !inst->_priv || !adapter->_priv) {
    return NULL;
  }

  instVk     = inst->_priv;
  gpuSurface = calloc(1, sizeof(*gpuSurface));
  if (!gpuSurface) {
    return NULL;
  }

  gpuSurface->type  = type;
  gpuSurface->scale = scale;
  surface           = calloc(1, sizeof(*surface));
  if (!surface) {
    free(gpuSurface);
    return NULL;
  }

  surface->inst     = instVk->inst;
  err               = VK_ERROR_EXTENSION_NOT_PRESENT;

#if defined(VK_USE_PLATFORM_WIN32_KHR)
  VkWin32SurfaceCreateInfoKHR createInfo = {0};
  if (type != GPU_SURFACE_WINDOWS_HWND) {
    free(surface);
    free(gpuSurface);
    return NULL;
  }

  createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.pNext     = NULL;
  createInfo.flags     = 0;
  createInfo.hinstance = GetModuleHandleW(NULL);
  createInfo.hwnd      = nativeHandle;

  err = vkCreateWin32SurfaceKHR(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
  VkAndroidSurfaceCreateInfoKHR createInfo = {0};

  if (type != GPU_SURFACE_ANDROID_NATIVE_WINDOW) {
    free(surface);
    free(gpuSurface);
    return NULL;
  }

  createInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  createInfo.pNext  = NULL;
  createInfo.flags  = 0;
  createInfo.window = (struct ANativeWindow *)nativeHandle;

  err = vkCreateAndroidSurfaceKHR(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_METAL_EXT)
  VkMetalSurfaceCreateInfoEXT createInfo = {0};
  surface->metalLayer = gpuCreateMetalLayer(nativeHandle, type, scale);
  if (!surface->metalLayer) {
    free(surface);
    free(gpuSurface);
    return NULL;
  }

  createInfo.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  createInfo.pNext  = NULL;
  createInfo.flags  = 0;
  createInfo.pLayer = surface->metalLayer;

  err = vkCreateMetalSurfaceEXT(instVk->inst, &createInfo, NULL, &surface->surface);
#endif

  if (err != VK_SUCCESS) {
#if defined(__APPLE__)
    gpuDestroyMetalLayer(surface->metalLayer);
#endif
    free(surface);
    free(gpuSurface);
    return NULL;
  }

  gpuSurface->_priv = surface;

  return gpuSurface;
}

static void
vk_appendSurfaceFormat(GPUSurfaceVk *surface,
                       uint32_t     *count,
                       GPUFormat     format) {
  if (!surface || !count || format <= GPU_FORMAT_UNDEFINED ||
      format >= GPU_FORMAT_COUNT || *count >= GPU_FORMAT_COUNT) {
    return;
  }

  for (uint32_t i = 0u; i < *count; i++) {
    if (surface->formats[i] == (uint32_t)format) {
      return;
    }
  }

  surface->formats[(*count)++] = (uint32_t)format;
}

static void
vk_appendPresentMode(GPUSurfaceVk    *surface,
                     uint32_t        *count,
                     VkPresentModeKHR mode) {
  GPUPresentMode gpuMode;

  if (!surface || !count ||
      *count >= GPU_ARRAY_LEN(surface->presentModes)) {
    return;
  }

  switch (mode) {
    case VK_PRESENT_MODE_FIFO_KHR:
      gpuMode = GPU_PRESENT_MODE_FIFO;
      break;
    case VK_PRESENT_MODE_MAILBOX_KHR:
      gpuMode = GPU_PRESENT_MODE_MAILBOX;
      break;
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
      gpuMode = GPU_PRESENT_MODE_IMMEDIATE;
      break;
    default:
      return;
  }

  for (uint32_t i = 0u; i < *count; i++) {
    if (surface->presentModes[i] == (uint32_t)gpuMode) {
      return;
    }
  }
  surface->presentModes[(*count)++] = (uint32_t)gpuMode;
}

static GPUResult
vk_getSurfaceCapabilities(const GPUAdapter       * __restrict adapter,
                          GPUSurface             * __restrict gpuSurface,
                          GPUSurfaceCapabilities * __restrict outCaps) {
  GPUAdapterVk            *adapterVk;
  GPUSurfaceVk            *surface;
  VkSurfaceCapabilitiesKHR caps;
  VkSurfaceFormatKHR      *formats;
  VkPresentModeKHR        *presentModes;
  uint32_t                 formatCount;
  uint32_t                 gpuFormatCount;
  uint32_t                 presentModeCount;
  uint32_t                 gpuPresentModeCount;

  adapterVk = adapter ? adapter->_priv : NULL;
  surface   = gpuSurface ? gpuSurface->_priv : NULL;
  if (!adapterVk || !surface || !surface->surface || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  formats             = NULL;
  presentModes        = NULL;
  formatCount         = 0u;
  gpuFormatCount      = 0u;
  presentModeCount    = 0u;
  gpuPresentModeCount = 0u;
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(adapterVk->physicalDevice,
                                                 surface->surface,
                                                 &caps) != VK_SUCCESS ||
      vkGetPhysicalDeviceSurfaceFormatsKHR(adapterVk->physicalDevice,
                                           surface->surface,
                                           &formatCount,
                                           NULL) != VK_SUCCESS ||
      formatCount == 0u ||
      vkGetPhysicalDeviceSurfacePresentModesKHR(adapterVk->physicalDevice,
                                                surface->surface,
                                                &presentModeCount,
                                                NULL) != VK_SUCCESS ||
      presentModeCount == 0u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  formats      = malloc((size_t)formatCount * sizeof(*formats));
  presentModes = malloc((size_t)presentModeCount * sizeof(*presentModes));
  if (!formats || !presentModes) {
    free(presentModes);
    free(formats);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(adapterVk->physicalDevice,
                                           surface->surface,
                                           &formatCount,
                                           formats) != VK_SUCCESS ||
      vkGetPhysicalDeviceSurfacePresentModesKHR(adapterVk->physicalDevice,
                                                surface->surface,
                                                &presentModeCount,
                                                presentModes) != VK_SUCCESS) {
    free(presentModes);
    free(formats);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (formatCount == 1u && formats[0].format == VK_FORMAT_UNDEFINED) {
    for (GPUFormat format = GPU_FORMAT_R8_UNORM;
         format < GPU_FORMAT_COUNT;
         format = (GPUFormat)(format + 1)) {
      GPUFormatCapabilities formatCaps;
      VkFormat              nativeFormat;

      if (vk_formatFromGPU(format, &nativeFormat) &&
          GPUGetFormatCapabilities(adapter, format, &formatCaps) == GPU_OK &&
          formatCaps.colorAttachment) {
        vk_appendSurfaceFormat(surface, &gpuFormatCount, format);
      }
    }
  } else {
    for (uint32_t i = 0u; i < formatCount; i++) {
      vk_appendSurfaceFormat(surface,
                             &gpuFormatCount,
                             vk_formatToGPU(formats[i].format));
    }
  }
  for (uint32_t i = 0u; i < presentModeCount; i++) {
    vk_appendPresentMode(surface,
                         &gpuPresentModeCount,
                         presentModes[i]);
  }
  free(presentModes);
  free(formats);

  if (gpuFormatCount == 0u || gpuPresentModeCount == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outCaps->pFormats         = surface->formats;
  outCaps->pPresentModes    = surface->presentModes;
  outCaps->minImageCount    = caps.minImageCount;
  outCaps->maxImageCount    = caps.maxImageCount > 0u ?
    caps.maxImageCount : UINT32_MAX;
  outCaps->formatCount      = gpuFormatCount;
  outCaps->presentModeCount = gpuPresentModeCount;
  return GPU_OK;
}

GPU_HIDE
void
vk_destroySurface(GPUSurface * __restrict surface) {
  GPUSurfaceVk *surfaceVk;

  if (!surface) {
    return;
  }

  surfaceVk = surface->_priv;
  if (surfaceVk) {
    if (surfaceVk->inst && surfaceVk->surface) {
      vkDestroySurfaceKHR(surfaceVk->inst, surfaceVk->surface, NULL);
    }
#if defined(__APPLE__)
    gpuDestroyMetalLayer(surfaceVk->metalLayer);
#endif
    free(surfaceVk);
  }

  free(surface);
}

GPU_HIDE
void
vk_initSurface(GPUApiSurface * apiDevice) {
  apiDevice->createSurface   = vk_createSurface;
  apiDevice->getCapabilities = vk_getSurfaceCapabilities;
  apiDevice->destroySurface  = vk_destroySurface;
}
