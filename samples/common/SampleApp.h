#ifndef gpu_sample_app_h
#define gpu_sample_app_h

#import <AppKit/AppKit.h>

#import "../../include/gpu/gpu.h"

static inline BOOL
GPUSampleCreateWindow(NSString *title,
                      id<NSWindowDelegate> delegate,
                      NSWindow *__strong *outWindow,
                      NSView *__strong *outView) {
  NSRect frame;
  NSWindow *window;
  NSView *view;

  if (!title || !outWindow || !outView) {
    return NO;
  }

  frame = NSMakeRect(0, 0, 960, 640);
  window = [[NSWindow alloc] initWithContentRect:frame
                                      styleMask:(NSWindowStyleMaskTitled |
                                                 NSWindowStyleMaskClosable |
                                                 NSWindowStyleMaskResizable)
                                        backing:NSBackingStoreBuffered
                                          defer:NO];
  if (!window) {
    return NO;
  }

  window.title = title;
  window.delegate = delegate;

  view = [[NSView alloc] initWithFrame:frame];
  if (!view) {
    return NO;
  }

  [window setContentView:view];
  [window center];
  [window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];

  *outWindow = window;
  *outView = view;
  return YES;
}

static inline GPUAdapter *
GPUSampleSelectAdapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  uint32_t    adapterCount;
  GPUResult   result;

  adapter      = NULL;
  adapterCount = 1;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

static inline BOOL
GPUSampleCreateDefaultSurfaceGPU(NSWindow        *window,
                                 NSView          *view,
                                 GPUInstance    **outInstance,
                                 GPUAdapter     **outAdapter,
                                 GPUDevice      **outDevice,
                                 GPUCommandQueue **outQueue,
                                 GPUSurface     **outSurface,
                                 GPUSwapchain   **outSwapchain) {
  GPUInstance     *instance;
  GPUAdapter      *adapter;
  GPUDevice       *device;
  GPUCommandQueue *queue;
  GPUSurface      *surface;
  GPUSwapchain    *swapchain;

  if (!window || !view || !outInstance || !outAdapter || !outDevice ||
      !outQueue || !outSurface || !outSwapchain) {
    return NO;
  }

  instance = NULL;
  if (GPUCreateInstance(NULL, &instance) != GPU_OK || !instance) {
    NSLog(@"GPU: failed to create instance");
    return NO;
  }

  adapter = GPUSampleSelectAdapter(instance);
  if (!adapter) {
    NSLog(@"GPU: failed to get adapter");
    GPUDestroyInstance(instance);
    return NO;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    NSLog(@"GPU: failed to create device");
    GPUDestroyInstance(instance);
    return NO;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue) {
    NSLog(@"GPU: failed to get command queue");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return NO;
  }

  surface = GPUCreateSurfaceFromNative(instance,
                                       adapter,
                                       (__bridge void *)view,
                                       GPU_SURFACE_APPLE_NSVIEW,
                                       window.backingScaleFactor ?: 1.0f);
  if (!surface) {
    NSLog(@"GPU: failed to create surface");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return NO;
  }

  swapchain = GPUCreateSwapchainDefault(device,
                                        surface,
                                        (uint32_t)view.bounds.size.width,
                                        (uint32_t)view.bounds.size.height);
  if (!swapchain) {
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

#endif
