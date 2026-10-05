/*
 * Copyright (C) 2026 Recep Aslantas
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

#ifndef gpu_sample_app_h
#define gpu_sample_app_h

#import <AppKit/AppKit.h>

#import "../../include/gpu/gpu.h"

static inline BOOL
GPUSampleCreateWindow(NSString            *title,
                      id<NSWindowDelegate> delegate,
                      NSWindow  *__strong *outWindow,
                      NSView    *__strong *outView) {
  NSRect    frame;
  NSWindow *window;
  NSView   *view;

  if (!title || !outWindow || !outView) {
    return NO;
  }

  frame = NSMakeRect(0, 0, 960, 640);

  if (!(window = [[NSWindow alloc] initWithContentRect:frame
                                             styleMask:(NSWindowStyleMaskTitled |
                                                        NSWindowStyleMaskClosable |
                                                        NSWindowStyleMaskResizable)
                                               backing:NSBackingStoreBuffered
                                                 defer:NO])) {
    return NO;
  }

  window.title    = title;
  window.delegate = delegate;

  if (!(view = [[NSView alloc] initWithFrame:frame])) {
    return NO;
  }

  [window setContentView:view];
  [window center];
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  *outWindow = window;
  *outView   = view;

  return YES;
}

static inline GPUAdapter*
GPUSampleSelectAdapter(GPUInstance *instance) {
  return GPUGetAutoSelectedAdapter(instance);
}

static inline BOOL
GPUSampleCreateDefaultSurfaceGPU(NSWindow      *window,
                                 NSView        *view,
                                 GPUInstance  **outInstance,
                                 GPUAdapter   **outAdapter,
                                 GPUDevice    **outDevice,
                                 GPUQueue     **outQueue,
                                 GPUSurface   **outSurface,
                                 GPUSwapchain **outSwapchain) {
  GPUInstance  *instance;
  GPUAdapter   *adapter;
  GPUDevice    *device;
  GPUQueue     *queue;
  GPUSurface   *surface;
  GPUSwapchain *swapchain;

  if (!window || !view || !outInstance || !outAdapter || !outDevice
      || !outQueue || !outSurface || !outSwapchain) {
    return NO;
  }

  instance = NULL;

  if (GPUCreateInstance(NULL, &instance) != GPU_OK || !instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  if (!(adapter = GPUSampleSelectAdapter(instance))) {
    NSLog(@"GPU: failed to get adapter");
    GPUDestroyInstance(instance);
    return NO;
  }

  if (!(device = GPUCreateDeviceWithDefaultQueues(adapter))) {
    NSLog(@"GPU: failed to create device");
    GPUDestroyInstance(instance);
    return NO;
  }

  if (!(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0))) {
    NSLog(@"GPU: failed to get command queue");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return NO;
  }

  if (!(surface = GPUCreateSurfaceFromNative(instance,
                                             adapter,
                                             (__bridge void *)view,
                                             GPU_SURFACE_APPLE_NSVIEW,
                                             window.backingScaleFactor ?: 1.0f))) {
    NSLog(@"GPU: failed to create surface");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return NO;
  }

  if (!(swapchain = GPUCreateSwapchainDefault(device,
                                              surface,
                                              (uint32_t)view.bounds.size.width,
                                              (uint32_t)view.bounds.size.height))) {
    NSLog(@"GPU: failed to create swapchain");
    GPUDestroySurface(surface);
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return NO;
  }

  *outInstance  = instance;
  *outAdapter   = adapter;
  *outDevice    = device;
  *outQueue     = queue;
  *outSurface   = surface;
  *outSwapchain = swapchain;

  return YES;
}

static inline BOOL
GPUSampleRecoverSwapchain(GPUSwapchain *swapchain, NSView *view) {
  GPUSwapchainStatus status;
  uint32_t           width;
  uint32_t           height;

  if (!swapchain || !view) {
    return NO;
  }

  status = GPUGetSwapchainStatus(swapchain);

  switch (status) {
    case GPU_SWAPCHAIN_STATUS_READY:
    case GPU_SWAPCHAIN_STATUS_UNAVAILABLE:
      return YES;
    case GPU_SWAPCHAIN_STATUS_SUBOPTIMAL:
    case GPU_SWAPCHAIN_STATUS_OUT_OF_DATE:
      break;
    case GPU_SWAPCHAIN_STATUS_SURFACE_LOST:
    default:
      return NO;
  }

  width  = (uint32_t)view.bounds.size.width;
  height = (uint32_t)view.bounds.size.height;

  return width > 0u && height > 0u && GPUResizeSwapchain(swapchain, width, height) == GPU_OK;
}

#endif
