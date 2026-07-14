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

/* TODO: cube.c, improve it if needed */
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
GPU_HIDE
static
VkResult
vk_createDisplaySurface(GPUInstanceVk       * __restrict inst,
                        GPUAdapterVk        * __restrict adapter,
                        GPUSurfaceVk        * __restrict surface) {
  VkDisplayPlanePropertiesKHR  *plane_props;
  VkDisplayPropertiesKHR        display_props;
  VkDisplayKHR                  display;
  VkDisplayModePropertiesKHR    mode_props;
  VkBool32                      found_plane = VK_FALSE;
  VkExtent2D                    image_extent;
  VkDisplaySurfaceCreateInfoKHR surfaceCI;
  VkResult U_ASSERT_ONLY        err;
  uint32_t                      display_count, mode_count, plane_count, plane_index;

  // Get the first display
  display_count = 1;
  err           = vkGetPhysicalDeviceDisplayPropertiesKHR(adapter->physicalDevice,
                                                          &display_count,
                                                          &display_props);
  assert(!err || (err == VK_INCOMPLETE));

  display = display_props.display;

  // Get the first mode of the display
  err = vkGetDisplayModePropertiesKHR(adapter->physicalDevice, display, &mode_count, NULL);
  assert(!err);

  if (mode_count == 0) {
    printf("Cannot find any mode for the display!\n");
    fflush(stdout);
    exit(1);
  }

  mode_count = 1;
  err = vkGetDisplayModePropertiesKHR(adapter->physicalDevice, display, &mode_count, &mode_props);
  assert(!err || (err == VK_INCOMPLETE));

  // Get the list of planes
  err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(adapter->physicalDevice, &plane_count, NULL);
  assert(!err);

  if (plane_count == 0) {
    printf("Cannot find any plane!\n");
    fflush(stdout);
    exit(1);
  }

  plane_props = malloc(sizeof(VkDisplayPlanePropertiesKHR) * plane_count);
  assert(plane_props);

  err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(adapter->physicalDevice,
                                                     &plane_count,
                                                     plane_props);
  assert(!err);

  // Find a plane compatible with the display
  for (plane_index = 0; plane_index < plane_count; plane_index++) {
    uint32_t supported_count;
    VkDisplayKHR *supported_displays;

    // Disqualify planes that are bound to a different display
    if ((plane_props[plane_index].currentDisplay != VK_NULL_HANDLE) 
        && (plane_props[plane_index].currentDisplay != display)) {
      continue;
    }

    err = vkGetDisplayPlaneSupportedDisplaysKHR(adapter->physicalDevice,
                                                plane_index,
                                                &supported_count,
                                                NULL);
    assert(!err);

    if (supported_count == 0) {
      continue;
    }

    supported_displays = malloc(sizeof(VkDisplayKHR) * supported_count);
    assert(supported_displays);

    err = vkGetDisplayPlaneSupportedDisplaysKHR(adapter->physicalDevice,
                                                plane_index,
                                                &supported_count,
                                                supported_displays);
    assert(!err);

    for (uint32_t i = 0; i < supported_count; i++) {
      if (supported_displays[i] == display) {
        found_plane = VK_TRUE;
        break;
      }
    }

    free(supported_displays);

    if (found_plane) {
      break;
    }
  }

  if (!found_plane) {
    printf("Cannot find a plane compatible with the display!\n");
    fflush(stdout);
    exit(1);
  }

  free(plane_props);

  VkDisplayPlaneCapabilitiesKHR planeCaps;
  vkGetDisplayPlaneCapabilitiesKHR(adapter->physicalDevice,
                                   mode_props.displayMode,
                                   plane_index,
                                   &planeCaps);
  // Find a supported alpha mode
  VkDisplayPlaneAlphaFlagBitsKHR alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
  VkDisplayPlaneAlphaFlagBitsKHR alphaModes[4] = {
    VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
    VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR,
    VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR,
    VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR,
  };

  for (uint32_t i = 0; i < sizeof(alphaModes); i++) {
    if (planeCaps.supportedAlpha & alphaModes[i]) {
      alphaMode = alphaModes[i];
      break;
    }
  }

  image_extent.width  = mode_props.parameters.visibleRegion.width;
  image_extent.height = mode_props.parameters.visibleRegion.height;

  surfaceCI.sType           = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
  surfaceCI.pNext           = NULL;
  surfaceCI.flags           = 0;
  surfaceCI.displayMode     = mode_props.displayMode;
  surfaceCI.planeIndex      = plane_index;
  surfaceCI.planeStackIndex = plane_props[plane_index].currentStackIndex;
  surfaceCI.transform       = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  surfaceCI.alphaMode       = alphaMode;
  surfaceCI.globalAlpha     = 1.0f;
  surfaceCI.imageExtent     = image_extent;

  return vkCreateDisplayPlaneSurfaceKHR(inst->inst, &surfaceCI, NULL, &surface->surface);
}
#endif

GPU_HIDE
GPUSurface*
vk_createSurface(GPUApi            * __restrict api,
                 GPUInstance       * __restrict inst,
                 GPUAdapter        * __restrict adapter,
                 void              * __restrict nativeHandle,
                 GPUSurfaceType                 type,
                 float                          scale) {
  GPUInstanceVk *instVk;
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
  GPUAdapterVk  *adapterVk;
#endif
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
#if defined(VK_USE_PLATFORM_DISPLAY_KHR)
  adapterVk = adapter->_priv;
#endif
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

  /* TODO: what if a platform supports multiple */

  // Create a WSI surface for the window:
#if defined(VK_USE_PLATFORM_WIN32_KHR)
  VkWin32SurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  createInfo.pNext     = NULL;
  createInfo.flags     = 0;
  createInfo.hinstance = instVk->connection;
  createInfo.hwnd      = instVk->window;

  err = vkCreateWin32SurfaceKHR(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
  VkWaylandSurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
  createInfo.pNext   = NULL;
  createInfo.flags   = 0;
  createInfo.display = instVk->display;
  createInfo.surface = instVk->window;

  err = vkCreateWaylandSurfaceKHR(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
  VkAndroidSurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
  createInfo.pNext  = NULL;
  createInfo.flags  = 0;
  createInfo.window = (struct ANativeWindow *)(instVk->window);

  err = vkCreateAndroidSurfaceKHR(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_XLIB_KHR)
  VkXlibSurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType  = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  createInfo.pNext  = NULL;
  createInfo.flags  = 0;
  createInfo.dpy    = instVk->display;
  createInfo.window = instVk->xlib_window;

  err = vkCreateXlibSurfaceKHR(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
  VkXcbSurfaceCreateInfoKHR createInfo = {0};
  createInfo.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  createInfo.pNext      = NULL;
  createInfo.flags      = 0;
  createInfo.connection = instVk->connection;
  createInfo.window     = instVk->xcb_window;

  err = vkCreateXcbSurfaceKHR(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
  VkDirectFBSurfaceCreateInfoEXT createInfo = {0};
  createInfo.sType   = VK_STRUCTURE_TYPE_DIRECTFB_SURFACE_CREATE_INFO_EXT;
  createInfo.pNext   = NULL;
  createInfo.flags   = 0;
  createInfo.dfb     = instVk->dfb;
  createInfo.surface = instVk->window;

  err = vkCreateDirectFBSurfaceEXT(instVk->inst, &createInfo, NULL, &surface->surface);
#elif defined(VK_USE_PLATFORM_DISPLAY_KHR)
  err = vk_createDisplaySurface(instVk, adapterVk, surface->surface);
#elif defined(VK_USE_PLATFORM_METAL_EXT)
  VkMetalSurfaceCreateInfoEXT createInfo = {0};
  surface->metalLayer = vk_createMetalLayer(nativeHandle, type, scale);
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
    vk_destroyMetalLayer(surface->metalLayer);
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

static GPUResult
vk_getSurfaceCapabilities(const GPUAdapter       * __restrict adapter,
                          GPUSurface             * __restrict gpuSurface,
                          GPUSurfaceCapabilities * __restrict outCaps) {
  GPUAdapterVk            *adapterVk;
  GPUSurfaceVk            *surface;
  VkSurfaceCapabilitiesKHR caps;
  VkSurfaceFormatKHR      *formats;
  uint32_t                 formatCount;
  uint32_t                 gpuFormatCount;

  adapterVk = adapter ? adapter->_priv : NULL;
  surface   = gpuSurface ? gpuSurface->_priv : NULL;
  if (!adapterVk || !surface || !surface->surface || !outCaps) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  formatCount    = 0u;
  gpuFormatCount = 0u;
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(adapterVk->physicalDevice,
                                                 surface->surface,
                                                 &caps) != VK_SUCCESS ||
      vkGetPhysicalDeviceSurfaceFormatsKHR(adapterVk->physicalDevice,
                                           surface->surface,
                                           &formatCount,
                                           NULL) != VK_SUCCESS ||
      formatCount == 0u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  formats = malloc((size_t)formatCount * sizeof(*formats));
  if (!formats) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(adapterVk->physicalDevice,
                                           surface->surface,
                                           &formatCount,
                                           formats) != VK_SUCCESS) {
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
  free(formats);

  if (gpuFormatCount == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outCaps->minImageCount = caps.minImageCount;
  outCaps->maxImageCount = caps.maxImageCount > 0u ?
    caps.maxImageCount : UINT32_MAX;
  outCaps->formatCount = gpuFormatCount;
  outCaps->pFormats    = surface->formats;
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
    vk_destroyMetalLayer(surfaceVk->metalLayer);
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
