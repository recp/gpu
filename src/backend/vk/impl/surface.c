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
                        GPUPhysicalDeviceVk * __restrict phyDevice,
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
  err           = vkGetPhysicalDeviceDisplayPropertiesKHR(phyDevice->phyDevice,
                                                          &display_count,
                                                          &display_props);
  assert(!err || (err == VK_INCOMPLETE));

  display = display_props.display;

  // Get the first mode of the display
  err = vkGetDisplayModePropertiesKHR(phyDevice->phyDevice, display, &mode_count, NULL);
  assert(!err);

  if (mode_count == 0) {
    printf("Cannot find any mode for the display!\n");
    fflush(stdout);
    exit(1);
  }

  mode_count = 1;
  err = vkGetDisplayModePropertiesKHR(phyDevice->phyDevice, display, &mode_count, &mode_props);
  assert(!err || (err == VK_INCOMPLETE));

  // Get the list of planes
  err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(phyDevice->phyDevice, &plane_count, NULL);
  assert(!err);

  if (plane_count == 0) {
    printf("Cannot find any plane!\n");
    fflush(stdout);
    exit(1);
  }

  plane_props = malloc(sizeof(VkDisplayPlanePropertiesKHR) * plane_count);
  assert(plane_props);

  err = vkGetPhysicalDeviceDisplayPlanePropertiesKHR(phyDevice->phyDevice, 
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

    err = vkGetDisplayPlaneSupportedDisplaysKHR(phyDevice->phyDevice, 
                                                plane_index,
                                                &supported_count,
                                                NULL);
    assert(!err);

    if (supported_count == 0) {
      continue;
    }

    supported_displays = malloc(sizeof(VkDisplayKHR) * supported_count);
    assert(supported_displays);

    err = vkGetDisplayPlaneSupportedDisplaysKHR(phyDevice->phyDevice, 
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
  vkGetDisplayPlaneCapabilitiesKHR(phyDevice->phyDevice, 
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
                 GPUPhysicalDevice * __restrict phyDevice,
                 void              * __restrict nativeHandle,
                 GPUSurfaceType                 type,
                 float                          scale) {
  GPUInstanceVk         *instVk;
  GPUPhysicalDeviceVk   *phyDeviceVk;
  GPUSurface            *gpuSurface;
  GPUSurfaceVk          *surface;
  VkResult U_ASSERT_ONLY err;

  instVk            = inst->_priv;
  phyDeviceVk       = phyDevice->_priv;
  gpuSurface        = calloc(1, sizeof(*gpuSurface));
  gpuSurface->type  = type;
  gpuSurface->scale = scale;
  surface           = calloc(1, sizeof(*surface));

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
  err = vk_createDisplaySurface(instVk, phyDeviceVk, surface->surface);
#elif defined(VK_USE_PLATFORM_METAL_EXT)
  VkMetalSurfaceCreateInfoEXT createInfo = {0};
  createInfo.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  createInfo.pNext  = NULL;
  createInfo.flags  = 0;
  createInfo.pLayer = nativeHandle;

  err = vkCreateMetalSurfaceEXT(instVk->inst, &createInfo, NULL, &surface->surface);
#endif

  assert(!err);

  gpuSurface->_priv = surface;

  return gpuSurface;
}

GPU_HIDE
void
vk_initSurface(GPUApiSurface * apiDevice) {
  apiDevice->createSurface = vk_createSurface;
}
