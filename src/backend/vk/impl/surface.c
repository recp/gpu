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

static void
vk_appendSurfaceFormat(GPUSurfaceVk *surface, uint32_t *count, GPUFormat format) {
  uint32_t i;

  if (!surface || !count || format <= GPU_FORMAT_UNDEFINED || format >= GPU_FORMAT_COUNT
      || *count >= GPU_FORMAT_COUNT) {
    return;
  }

  for (i = 0u; i < *count; i++) {
    if (surface->formats[i] == (uint32_t)format) {
      return;
    }
  }

  surface->formats[(*count)++] = (uint32_t)format;
}

static void
vk_appendPresentMode(GPUSurfaceVk *surface, uint32_t *count, VkPresentModeKHR mode) {
  GPUPresentMode gpuMode;
  uint32_t       i;

  if (!surface || !count || *count >= GPU_ARRAY_LEN(surface->presentModes)) {
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

  for (i = 0u; i < *count; i++) {
    if (surface->presentModes[i] == (uint32_t)gpuMode) {
      return;
    }
  }

  surface->presentModes[(*count)++] = (uint32_t)gpuMode;
}

static GPUResult
vk_getSurfaceCapabilities(const GPUAdapter       *__restrict adapter,
                          GPUSurface             *__restrict gpuSurface,
                          GPUSurfaceCapabilities *__restrict outCaps) {
  VkSurfaceCapabilitiesKHR caps;
  GPUFormatCapabilities    formatCaps;
  GPUAdapterVk            *adapterVk;
  GPUSurfaceVk            *surface;
  VkSurfaceFormatKHR      *formats;
  VkPresentModeKHR        *presentModes;
  uint32_t                 formatCount;
  uint32_t                 gpuFormatCount;
  uint32_t                 presentModeCount;
  uint32_t                 gpuPresentModeCount;
  GPUFormat                format;
  VkFormat                 nativeFormat;
  uint32_t                 formatIndex;
  uint32_t                 modeIndex;

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
                                                &caps) != VK_SUCCESS
      || vkGetPhysicalDeviceSurfaceFormatsKHR(adapterVk->physicalDevice,
                                              surface->surface,
                                              &formatCount,
                                              NULL) != VK_SUCCESS
      || formatCount == 0u
      || vkGetPhysicalDeviceSurfacePresentModesKHR(adapterVk->physicalDevice,
                                                   surface->surface,
                                                   &presentModeCount,
                                                   NULL) != VK_SUCCESS
      || presentModeCount == 0u) {
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
                                           formats) != VK_SUCCESS
      || vkGetPhysicalDeviceSurfacePresentModesKHR(adapterVk->physicalDevice,
                                                   surface->surface,
                                                   &presentModeCount,
                                                   presentModes) != VK_SUCCESS) {
    free(presentModes);
    free(formats);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (formatCount == 1u && formats[0].format == VK_FORMAT_UNDEFINED) {
    for (format = GPU_FORMAT_R8_UNORM; format < GPU_FORMAT_COUNT; format = (GPUFormat)(format + 1)) {
      if (vk_formatFromGPU(format,
                           &nativeFormat)
          && GPUGetFormatCapabilities(adapter, format, &formatCaps) == GPU_OK
          && formatCaps.colorAttachment) {
        vk_appendSurfaceFormat(surface,
                               &gpuFormatCount,
                               format);
      }
    }
  } else {
    for (formatIndex = 0u; formatIndex < formatCount; formatIndex++) {
      vk_appendSurfaceFormat(surface,
                             &gpuFormatCount,
                             vk_formatToGPU(formats[formatIndex].format));
    }
  }

  for (modeIndex = 0u; modeIndex < presentModeCount; modeIndex++) {
    vk_appendPresentMode(surface,
                         &gpuPresentModeCount,
                         presentModes[modeIndex]);
  }

  free(presentModes);
  free(formats);

  if (gpuFormatCount == 0u || gpuPresentModeCount == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outCaps->pFormats         = surface->formats;
  outCaps->pPresentModes    = surface->presentModes;
  outCaps->minImageCount    = caps.minImageCount;
  outCaps->maxImageCount    = caps.maxImageCount > 0u ? caps.maxImageCount : UINT32_MAX;
  outCaps->formatCount      = gpuFormatCount;
  outCaps->presentModeCount = gpuPresentModeCount;

  return GPU_OK;
}

GPU_HIDE
GPUSurface*
vk_createSurface(GPUApi *__restrict api, GPUInstance *__restrict inst, const GPUSurfaceNativeInfo *__restrict info) {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
  VkWin32SurfaceCreateInfoKHR   winInfo;
#endif
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
  VkAndroidSurfaceCreateInfoKHR androidInfo;
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
  VkXlibSurfaceCreateInfoKHR    xlibInfo;
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
  VkWaylandSurfaceCreateInfoKHR waylandInfo;
#endif
  GPUInstanceVk                *instVk;
  GPUAdapter                   *adapter;
  GPUSurface                   *gpuSurface;
  GPUSurfaceVk                 *surface;
  VkResult                      err;

  GPU__UNUSED(api);

  if (!info || !info->adapter) {
    return NULL;
  }

  adapter = info->adapter;

  if (!inst) {
    inst = adapter->inst;
  }

  if (!inst || !inst->_priv || !adapter->_priv) {
    return NULL;
  }

  instVk = inst->_priv;

  if (!(gpuSurface = calloc(1, sizeof(*gpuSurface)))) {
    return NULL;
  }

  gpuSurface->type  = info->type;
  gpuSurface->scale = info->scale;

  if (!(surface = calloc(1, sizeof(*surface)))) {
    free(gpuSurface);
    return NULL;
  }

  surface->inst = instVk->inst;
  err           = VK_ERROR_EXTENSION_NOT_PRESENT;

  switch (info->type) {
#if defined(VK_USE_PLATFORM_WIN32_KHR)
    case GPU_SURFACE_WINDOWS_HWND: {
      winInfo = (VkWin32SurfaceCreateInfoKHR){0};

      if (!info->nativeHandle) {
        break;
      }

      winInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
      winInfo.hinstance = GetModuleHandleW(NULL);
      winInfo.hwnd      = info->nativeHandle;
      err               = vkCreateWin32SurfaceKHR(instVk->inst,
                                                  &winInfo,
                                                  NULL,
                                                  &surface->surface);
      break;
    }
#endif
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
    case GPU_SURFACE_ANDROID_NATIVE_WINDOW: {
      androidInfo = (VkAndroidSurfaceCreateInfoKHR){0};

      if (!info->nativeHandle) {
        break;
      }

      androidInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
      androidInfo.window = (struct ANativeWindow *)info->nativeHandle;
      err                = vkCreateAndroidSurfaceKHR(instVk->inst,
                                                     &androidInfo,
                                                     NULL,
                                                     &surface->surface);
      break;
    }
#endif
#if defined(VK_USE_PLATFORM_METAL_EXT)
    case GPU_SURFACE_APPLE_NSVIEW:
    case GPU_SURFACE_APPLE_UIVIEW: {
      VkMetalSurfaceCreateInfoEXT metalInfo = {0};

      if (!info->nativeHandle) {
        break;
      }

      surface->metalLayer = gpuCreateMetalLayer(info->nativeHandle,
                                                info->type,
                                                info->scale);

      if (!surface->metalLayer) {
        break;
      }

      metalInfo.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
      metalInfo.pLayer = surface->metalLayer;
      err              = vkCreateMetalSurfaceEXT(instVk->inst,
                                                 &metalInfo,
                                                 NULL,
                                                 &surface->surface);
      break;
    }
#endif
#if defined(VK_USE_PLATFORM_XLIB_KHR)
    case GPU_SURFACE_XLIB_WINDOW: {
      xlibInfo = (VkXlibSurfaceCreateInfoKHR){0};

      if (!info->display || info->nativeWindow == 0u) {
        break;
      }

      xlibInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
      xlibInfo.dpy    = (Display *)info->display;
      xlibInfo.window = (Window)info->nativeWindow;
      err             = vkCreateXlibSurfaceKHR(instVk->inst,
                                               &xlibInfo,
                                               NULL,
                                               &surface->surface);
      break;
    }
#endif
#if defined(VK_USE_PLATFORM_WAYLAND_KHR)
    case GPU_SURFACE_WAYLAND_SURFACE: {
      waylandInfo = (VkWaylandSurfaceCreateInfoKHR){0};

      if (!info->display || !info->nativeHandle) {
        break;
      }

      waylandInfo.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
      waylandInfo.display = (struct wl_display *)info->display;
      waylandInfo.surface = (struct wl_surface *)info->nativeHandle;
      err                 = vkCreateWaylandSurfaceKHR(instVk->inst,
                                                      &waylandInfo,
                                                      NULL,
                                                      &surface->surface);
      break;
    }
#endif
    default:
      break;
  }

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

GPU_HIDE
void
vk_destroySurface(GPUSurface *__restrict surface) {
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
vk_initSurface(GPUApiSurface *apiDevice) {
  apiDevice->createSurface   = vk_createSurface;
  apiDevice->getCapabilities = vk_getSurfaceCapabilities;
  apiDevice->destroySurface  = vk_destroySurface;
}
