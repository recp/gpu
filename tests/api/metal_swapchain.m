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

#include "../../src/backend/mt/common.h"
#import <QuartzCore/CATransaction.h>
#include <float.h>
#include <math.h>
#include <pthread.h>

#define CHECK(expr) do { \
  if (!(expr)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #expr); \
    ok = false; \
  } \
} while (0)

static bool
present(GPUDevice *device, GPUSwapchain *swapchain, uint32_t index,
        id<CAMetalDrawable> *outDrawable) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      info  = {0};
  GPUTransientBufferSlice      slice = {0};
  GPUCommandBuffer           *command;
  GPURenderPassEncoder       *pass;
  GPUSwapchainMetal          *native;
  GPUFrame                   *frame;
  bool                        ok = true;

  native = swapchain->_priv;
  if (!(frame = GPUBeginFrame(swapchain)))
    return false;

  CHECK(frame->target->width == (uint32_t)native->layer.drawableSize.width
       && frame->target->height == (uint32_t)native->layer.drawableSize.height
       && GPUResizeSwapchain(swapchain, 96u, 80u) == GPU_ERROR_INVALID_ARGUMENT
       && GPUSetSwapchainScale(swapchain, 3.0f)
          == (GPUIsFeatureEnabled(device, GPU_FEATURE_SWAPCHAIN_SCALE)
                ? GPU_ERROR_INVALID_ARGUMENT : GPU_ERROR_UNSUPPORTED));
  if (device->transientConfigured) {
    CHECK(GPUAllocateTransientBuffer(device, GPU_BUFFER_USAGE_UNIFORM,
                                     256u, 256u, &slice) == GPU_OK);
    if (slice.cpuPtr)
      memset(slice.cpuPtr, (int)index, 256u);
    else
      CHECK(false);
  }

  if (GPUAcquireCommandBuffer(GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u),
                              "multiwindow clear", &command) != GPU_OK) {
    GPUEndFrame(frame);
    return false;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = (float)(index % 3u) / 2.0f;
  color.clearColor.float32[1] = (float)((index + 1u) % 3u) / 2.0f;
  color.clearColor.float32[3] = 1.0f;
  info.pColorAttachments      = &color;
  info.colorAttachmentCount  = 1u;
  pass = GPUBeginRenderPass(command, &info);
  if (!pass) {
    GPUDiscardCommandBuffer(command);
    GPUEndFrame(frame);
    return false;
  }

  GPUEndRenderPass(pass);
  if (outDrawable)
    *outDrawable = [(id<CAMetalDrawable>)frame->drawable retain];

  return GPUFinishFrame(GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u), command, frame) == GPU_OK && ok;
}

static void*
reject_resize(void *user) {
  GPUSwapchain      *swapchain;
  GPUSwapchainMetal *native;

  @autoreleasepool {
    swapchain = user;
    native    = swapchain->_priv;
    return (void *)(uintptr_t)(GPUSetSwapchainScale(swapchain, 2.0f) == GPU_ERROR_INVALID_ARGUMENT
                               && GPUResizeSwapchain(swapchain, 64u, 64u) == GPU_ERROR_INVALID_ARGUMENT
                               && (!native->layer.presentsWithTransaction || !GPUBeginFrame(swapchain)));
  }
}

static bool
check_options(GPUAdapter *adapter, GPUSurface *surface) {
  GPUDeviceCreateInfo    deviceInfo = {0};
  GPUSwapchainCreateInfo info       = {0};
  GPUSwapchainOptions    options    = {0};
  GPUDevice            *device;
  GPUSwapchain         *swapchain = NULL;
  bool                  ok = true;

  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK)
    return false;

  info.surface          = surface;
  info.width            = 96u;
  info.height           = 80u;
  info.format           = GPU_FORMAT_BGRA8_UNORM;
  info.chain.pNext      = &options;
  options.chain.sType      = GPU_STRUCTURE_TYPE_SWAPCHAIN_OPTIONS;
  options.chain.structSize = sizeof(options);
  options.colorSpace       = GPU_COLOR_SPACE_SRGB;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_UNSUPPORTED && !swapchain);
  options.colorSpace = GPU_COLOR_SPACE_DEFAULT;
  options.presentsWithTransaction = true;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_UNSUPPORTED && !swapchain);
  options.presentsWithTransaction = false;
  options.colorSpace = (GPUColorSpace)99;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_INVALID_ARGUMENT && !swapchain);
  options.colorSpace = GPU_COLOR_SPACE_DEFAULT;
  options.chain.structSize = sizeof(GPUChainedStruct);
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_INVALID_ARGUMENT && !swapchain);
  options.chain.structSize = sizeof(options);
  options.chain.pNext = &options;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_INVALID_ARGUMENT && !swapchain);
  options.chain.pNext = NULL;
  options.chain.sType = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_ERROR_UNSUPPORTED && !swapchain);
  info.chain.pNext = NULL;
  CHECK(GPUCreateSwapchain(device, &info, &swapchain) == GPU_OK && swapchain);
  if (swapchain) {
    CHECK(GPUSetSwapchainScale(swapchain, 2.0f) == GPU_ERROR_UNSUPPORTED);
    CHECK(!((GPUSwapchainMetal *)swapchain->_priv)->layer.presentsWithTransaction);
    CHECK(present(device, swapchain, 0u, NULL));
  }

  GPUDestroySwapchain(swapchain);
  GPUDestroyDevice(device);
  printf("options: default, disabled features, malformed chains=%s\n", ok ? "pass" : "fail");
  return ok;
}

static bool
check_windows(GPUAdapter *adapter, GPUSurface **surfaces, bool *onscreen) {
  const GPUFeature features[] = {GPU_FEATURE_SWAPCHAIN_SCALE, GPU_FEATURE_SWAPCHAIN_COLOR_SPACE,
                                 GPU_FEATURE_SWAPCHAIN_TRANSACTIONS};
  const float scales[] = {1.0f, 2.0f, 1.5f, 0.5f};
  const float invalid[] = {0.0f, -1.0f, NAN, INFINITY, FLT_MAX, FLT_MIN};
  GPUDeviceCreateInfo          deviceInfo = {0};
  GPUTransientAllocatorConfig transient  = {0};
  GPUSwapchainCreateInfo       info      = {0};
  GPUSwapchainOptions          options   = {0};
  GPUSwapchain               *swapchains[2] = {NULL, NULL};
  id<CAMetalDrawable>          drawables[2]  = {nil, nil};
  GPUDevice                  *device;
  GPUSwapchainMetal          *native;
  CGColorSpaceRef             expected;
  NSDate                     *deadline;
  pthread_t                   worker;
  void                       *workerResult;
  uint32_t                    window, epoch, value;
  bool                        ok = true;

  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = GPU_ARRAY_LEN(features);
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK)
    return false;

  transient.chain.sType      = GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG;
  transient.chain.structSize = sizeof(transient);
  transient.ringBytesPerFrame = 4096u;
  transient.chunkBytes        = 4096u;
  transient.framesInFlight    = 3u;
  CHECK(GPUConfigureTransientAllocator(device, &transient) == GPU_OK);
  info.chain.pNext          = &options;
  info.width                = 96u;
  info.height               = 80u;
  info.format               = GPU_FORMAT_BGRA8_UNORM;
  options.chain.sType       = GPU_STRUCTURE_TYPE_SWAPCHAIN_OPTIONS;
  options.chain.structSize  = sizeof(options);

  for (window = 0u; window < 2u; window++) {
    info.surface       = surfaces[window];
    options.colorSpace = window ? GPU_COLOR_SPACE_DISPLAY_P3 : GPU_COLOR_SPACE_SRGB;
    options.presentsWithTransaction = window != 0u;
    if (GPUCreateSwapchain(device, &info, &swapchains[window]) != GPU_OK) {
      CHECK(false);
      goto cleanup;
    }

    native = swapchains[window]->_priv;
    expected = CGColorSpaceCreateWithName(window ? kCGColorSpaceDisplayP3 : kCGColorSpaceSRGB);
    CHECK(expected && CFEqual(native->layer.colorspace, expected)
         && native->layer.presentsWithTransaction == (window != 0u));
    if (expected)
      CGColorSpaceRelease(expected);

    for (value = 0u; value < GPU_ARRAY_LEN(invalid); value++) {
      CHECK(GPUSetSwapchainScale(swapchains[window], invalid[value]) == GPU_ERROR_INVALID_ARGUMENT
           && swapchains[window]->backingScaleFactor == 1.0f);
    }

    CHECK(GPUResizeSwapchain(swapchains[window], 0u, 80u) == GPU_ERROR_INVALID_ARGUMENT
         && GPUResizeSwapchain(swapchains[window], UINT32_MAX, 80u) != GPU_OK
         && GPUSetSwapchainScale(swapchains[window], 4096.0f) == GPU_ERROR_UNSUPPORTED
         && swapchains[window]->backingScaleFactor == 1.0f
         && native->layer.contentsScale == 1.0f
         && native->layer.drawableSize.width == 96.0);
    if (pthread_create(&worker, NULL, reject_resize, swapchains[window]) != 0) {
      CHECK(false);
      goto cleanup;
    }

    pthread_join(worker, &workerResult);
    CHECK((uintptr_t)workerResult == 1u && swapchains[window]->backingScaleFactor == 1.0f);
  }

  for (epoch = 0u; epoch < GPU_ARRAY_LEN(scales); epoch++) {
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (window = 0u; window < 2u; window++) {
      native = swapchains[window]->_priv;
      CHECK(GPUSetSwapchainScale(swapchains[window], scales[epoch]) == GPU_OK
           && GPUResizeSwapchain(swapchains[window], 96u, 80u) == GPU_OK
           && native->layer.contentsScale == scales[epoch]
           && native->layer.drawableSize.width == 96.0 * scales[epoch]
           && native->layer.drawableSize.height == 80.0 * scales[epoch]
           && surfaces[window]->scale == 1.0f);
      /* exercise the observer after changing the scale, without the API path. */
      ((NSView *)surfaces[window]->_priv).layer.bounds = CGRectMake(0, 0, 100 + epoch, 84 + epoch);
      CHECK(native->layer.drawableSize.width == ceil((100.0 + epoch) * scales[epoch])
           && native->layer.drawableSize.height == ceil((84.0 + epoch) * scales[epoch]));
      CHECK(GPUResizeSwapchain(swapchains[window], 96u, 80u) == GPU_OK);
      [drawables[window] release];
      drawables[window] = nil;
      CHECK(present(device, swapchains[window], epoch + window, &drawables[window]));
    }
    [CATransaction commit];
  }

  [CATransaction flush];
  [NSApp updateWindows];
  CHECK(deviceApi(device)->device.waitIdle(device) == GPU_OK);
  *onscreen = (((NSView *)surfaces[0]->_priv).window.occlusionState & NSWindowOcclusionStateVisible)
              && (((NSView *)surfaces[1]->_priv).window.occlusionState & NSWindowOcclusionStateVisible);
  deadline = [NSDate dateWithTimeIntervalSinceNow:2.0];
  while (*onscreen && (drawables[0].presentedTime == 0.0 || drawables[1].presentedTime == 0.0)
         && [deadline timeIntervalSinceNow] > 0.0) {
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                           beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.002]];
  }
  if (*onscreen)
    CHECK(drawables[0].presentedTime > 0.0 && drawables[1].presentedTime > 0.0);
  else
    printf("onscreen timestamps unavailable: test windows are occluded\n");

cleanup:
  for (window = 0u; window < 2u; window++) {
    [drawables[window] release];
    GPUDestroySwapchain(swapchains[window]);
  }
  GPUDestroyDevice(device);
  printf("multiwindow: sRGB/P3, scale/KVO, transaction, thread rules, transient ring=%s\n", ok ? "pass" : "fail");
  return ok;
}

int
main(void) {
  @autoreleasepool {
    GPUInstance *instance = NULL;
    GPUSurface  *surfaces[2] = {NULL, NULL};
    NSWindow    *windows[2]  = {nil, nil};
    GPUAdapter  *adapter;
    uint32_t     i;
    bool         ok = false;
    bool         onscreen = false;

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance)))
      return 1;

    for (i = 0u; i < 2u; i++) {
      windows[i] = [[NSWindow alloc] initWithContentRect:NSMakeRect(40 + i * 128, 40, 96, 80)
                                              styleMask:NSWindowStyleMaskTitled
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
      [windows[i] setReleasedWhenClosed:NO];
      [windows[i] orderFrontRegardless];
      surfaces[i] = GPUCreateSurfaceFromNative(instance, adapter, windows[i].contentView,
                                               GPU_SURFACE_APPLE_NSVIEW, 1.0f);
      if (!surfaces[i])
        goto cleanup;
    }

    ok = check_options(adapter, surfaces[0]) && check_windows(adapter, surfaces, &onscreen);

cleanup:
    for (i = 0u; i < 2u; i++) {
      GPUDestroySurface(surfaces[i]);
      [windows[i] close];
      [windows[i] release];
    }
    GPUDestroyInstance(instance);
    if (ok && getenv("GPU_SWAPCHAIN_ONSCREEN") && !onscreen)
      return 77;

    return ok ? 0 : 1;
  }
}
