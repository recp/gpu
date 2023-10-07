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
vk_createSurface(struct GPUApi      * __restrict api,
                 struct GPUInstance * __restrict inst,
                 void               * __restrict nativeHandle,
                 GPUSurfaceType                  type,
                 float                           scale) {
  GPUInstanceVk         *instVk;
  GPUSurface            *gpuSurface;
  GPUSurfaceVk          *surface;
  VkResult U_ASSERT_ONLY err;

  instVk            = inst->_priv;
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
  err = demo_create_display_surface(demo);
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
