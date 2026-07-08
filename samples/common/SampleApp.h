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

static inline BOOL
GPUSampleCreateDefaultSurfaceGPU(NSWindow *window,
                                 NSView *view,
                                 GPUPhysicalDevice **outPhysicalDevice,
                                 GPUDevice **outDevice,
                                 GPUCommandQueue **outQueue,
                                 GPUSurface **outSurface,
                                 GPUSwapchain **outSwapchain) {
  GPUPhysicalDevice *physicalDevice;
  GPUDevice *device;
  GPUCommandQueue *queue;
  GPUSurface *surface;
  GPUSwapchain *swapchain;

  if (!window || !view || !outPhysicalDevice || !outDevice || !outQueue ||
      !outSurface || !outSwapchain) {
    return NO;
  }

  physicalDevice = GPUGetAutoSelectedPhysicalDevice(NULL);
  if (!physicalDevice) {
    NSLog(@"GPU: failed to get physical device");
    return NO;
  }

  device = GPUCreateDeviceWithDefaultQueues(physicalDevice);
  if (!device) {
    NSLog(@"GPU: failed to create device");
    return NO;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue) {
    NSLog(@"GPU: failed to get command queue");
    return NO;
  }

  surface = GPUCreateSurface(NULL,
                             physicalDevice,
                             (__bridge void *)view,
                             GPU_SURFACE_APPLE_NSVIEW,
                             window.backingScaleFactor ?: 1.0f);
  if (!surface) {
    NSLog(@"GPU: failed to create surface");
    return NO;
  }

  swapchain = GPUCreateSwapchainDefault(device,
                                        surface,
                                        (uint32_t)view.bounds.size.width,
                                        (uint32_t)view.bounds.size.height);
  if (!swapchain) {
    NSLog(@"GPU: failed to create swapchain");
    return NO;
  }

  *outPhysicalDevice = physicalDevice;
  *outDevice = device;
  *outQueue = queue;
  *outSurface = surface;
  *outSwapchain = swapchain;
  return YES;
}

#endif
