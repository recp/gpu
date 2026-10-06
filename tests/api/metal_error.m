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

#include "test.h"
#include "../../src/backend/mt/common.h"

typedef struct MetalErrorCase {
  NSInteger           code;
  GPUDeviceErrorType  type;
  GPUDeviceLostReason reason;
  GPUResult           result;
  uint8_t             domain;
} MetalErrorCase;

typedef struct MetalErrorCapture {
  GPUDevice          *device;
  GPUDeviceErrorType  type;
  GPUDeviceLostReason reason;
  GPUResult           result;
  uint32_t            count;
  char                message[128];
} MetalErrorCapture;

@interface GPUUnavailableLayer: CAMetalLayer
@end

/* legacy removal errors still need correct classification on older targets. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static const MetalErrorCase error_cases[] = {
  {MTLCommandBufferErrorInternal, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 0},
  {MTLCommandBufferErrorTimeout, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 0},
  {MTLCommandBufferErrorPageFault, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 0},
  {MTLCommandBufferErrorNotPermitted, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 0},
  {MTLCommandBufferErrorOutOfMemory, GPU_DEVICE_ERROR_OUT_OF_MEMORY,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_OUT_OF_MEMORY, 0},
  {MTLCommandBufferErrorAccessRevoked, GPU_DEVICE_ERROR_LOST,
   GPU_DEVICE_LOST_REASON_DRIVER_ERROR, GPU_ERROR_BACKEND_FAILURE, 0},
#if TARGET_OS_OSX
  {MTLCommandBufferErrorDeviceRemoved, GPU_DEVICE_ERROR_LOST,
   GPU_DEVICE_LOST_REASON_REMOVED, GPU_ERROR_BACKEND_FAILURE, 0},
#endif
#if MT_HAS_METAL4
  {MTL4CommandQueueErrorInternal, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 1},
  {MTL4CommandQueueErrorTimeout, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 1},
  {MTL4CommandQueueErrorNotPermitted, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 1},
  {MTL4CommandQueueErrorOutOfMemory, GPU_DEVICE_ERROR_OUT_OF_MEMORY,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_OUT_OF_MEMORY, 1},
  {MTL4CommandQueueErrorAccessRevoked, GPU_DEVICE_ERROR_LOST,
   GPU_DEVICE_LOST_REASON_DRIVER_ERROR, GPU_ERROR_BACKEND_FAILURE, 1},
  {MTL4CommandQueueErrorDeviceRemoved, GPU_DEVICE_ERROR_LOST,
   GPU_DEVICE_LOST_REASON_REMOVED, GPU_ERROR_BACKEND_FAILURE, 1},
#endif
  {MTLCommandBufferErrorOutOfMemory, GPU_DEVICE_ERROR_BACKEND,
   GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 2},
  {0, GPU_DEVICE_ERROR_BACKEND, GPU_DEVICE_LOST_REASON_UNKNOWN, GPU_ERROR_BACKEND_FAILURE, 3}
};
#pragma clang diagnostic pop

@implementation GPUUnavailableLayer
- (id<CAMetalDrawable>)nextDrawable {
  return nil;
}
@end

static void
capture_error(GPUDevice                *device,
              const GPUDeviceErrorInfo *error,
              void                     *userData) {
  MetalErrorCapture *capture;

  capture         = userData;
  capture->device = device;
  capture->type   = error->type;
  capture->reason = error->lostReason;
  capture->result = error->result;
  capture->count++;
  snprintf(capture->message, sizeof(capture->message), "%s", error->message);
}

static int
check_native_errors(void) {
  GPUDevice         devices[GPU_ARRAY_LEN(error_cases)] = {0};

  GPUDevice          concurrent = {0};
  GPUQueue           queue      = {0};
  GPUCommandBuffer   cmdb       = {0};
  MetalErrorCapture  capture    = {0};
  GPUCommandBuffer  *command;
  NSError           *error;
  NSString          *domain;
  uint32_t           i;
  bool               modern;

  modern = false;
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    modern = true;
  }
#endif

  command = &cmdb;
  mt_reportCommandBufferError(NULL, nil);
  mt_reportCommandBufferError(command, nil);

  for (i = 0u; i < GPU_ARRAY_LEN(error_cases); i++) {
    if (error_cases[i].domain == 1u && !modern) {
      continue;
    }

    memset(&capture, 0, sizeof(capture));
    queue._device = &devices[i];
    cmdb._queue   = &queue;
    GPUSetDeviceErrorCallback(&devices[i], capture_error, &capture);

    domain = MTLCommandBufferErrorDomain;
#if MT_HAS_METAL4
    if (error_cases[i].domain == 1u) {
      if (@available(macOS 26.0, iOS 26.0, *)) {
        domain = MTL4CommandQueueErrorDomain;
      }
    }
#endif
    if (error_cases[i].domain == 2u) {
      domain = @"gpu.test";
    }

    error = nil;

    if (error_cases[i].domain != 3u) {
      error = [NSError errorWithDomain:domain
                                 code:error_cases[i].code
                             userInfo:@{NSLocalizedDescriptionKey: @"injected"}];
    }

    mt_reportCommandBufferError(command, error);

    if (capture.count != 1u || capture.device != &devices[i]
        || capture.type != error_cases[i].type
        || capture.reason != error_cases[i].reason
        || capture.result != error_cases[i].result
        || strcmp(capture.message,
                  error ? "Metal command buffer failed: injected" : "Metal command buffer failed") != 0) {
      fprintf(stderr, "Metal error classification failed at case %u\n", i);
      return 0;
    }
  }

  memset(&capture, 0, sizeof(capture));
  queue._device = &concurrent;
  GPUSetDeviceErrorCallback(&concurrent, capture_error, &capture);
  error = [NSError errorWithDomain:MTLCommandBufferErrorDomain
                             code:MTLCommandBufferErrorAccessRevoked
                         userInfo:@{NSLocalizedDescriptionKey: @"concurrent loss"}];
  dispatch_apply(32u, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0u), ^(size_t index) {
    (void)index;
    mt_reportCommandBufferError(command, error);
  });

  if (capture.count != 1u || capture.device != &concurrent
      || capture.type != GPU_DEVICE_ERROR_LOST
      || capture.reason != GPU_DEVICE_LOST_REASON_DRIVER_ERROR) {
    fprintf(stderr, "concurrent device loss was not reported once\n");
    return 0;
  }

  return 1;
}

static int
check_surface_status(GPUDevice *device) {
  GPUSwapchainMetal    native    = {0};
  GPUSwapchain         swapchain = {0};
  GPUUnavailableLayer *layer = nil;
  uint64_t             offset;
  uint64_t             binds;
  uint32_t             frame;
  int                  ok = 0;

  swapchain.device             = device;
  swapchain.width              = 16u;
  swapchain.height             = 16u;
  swapchain.backingScaleFactor = 1.0f;

  offset = device->transientFrameOffset;
  binds  = device->currentFrameStats.requestedBindCalls;
  frame  = device->transientFrameIndex;

  if (GPUBeginFrame(&swapchain)
      || GPUGetSwapchainStatus(&swapchain) != GPU_SWAPCHAIN_STATUS_SURFACE_LOST
      || GPUResizeSwapchain(&swapchain, 32u, 16u) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "missing Metal swapchain did not report surface loss\n");
    goto cleanup;
  }

  swapchain._priv = &native;

  if (GPUBeginFrame(&swapchain)
      || GPUResizeSwapchain(&swapchain, 32u, 16u) != GPU_ERROR_BACKEND_FAILURE
      || GPUGetSwapchainStatus(&swapchain) != GPU_SWAPCHAIN_STATUS_SURFACE_LOST
      || swapchain.width != 16u || swapchain.height != 16u) {
    fprintf(stderr, "missing Metal layer cleared surface loss\n");
    goto cleanup;
  }

  layer        = [[GPUUnavailableLayer alloc] init];
  native.layer = layer;

  if (GPUBeginFrame(&swapchain)
      || GPUGetSwapchainStatus(&swapchain) != GPU_SWAPCHAIN_STATUS_SURFACE_LOST
      || GPUResizeSwapchain(&swapchain, 32u, 16u) != GPU_OK
      || GPUGetSwapchainStatus(&swapchain) != GPU_SWAPCHAIN_STATUS_READY
      || swapchain.width != 32u || swapchain.height != 16u) {
    fprintf(stderr, "Metal surface recovery status mismatch\n");
    goto cleanup;
  }

  if (GPUBeginFrame(&swapchain)
      || GPUGetSwapchainStatus(&swapchain) != GPU_SWAPCHAIN_STATUS_UNAVAILABLE
      || native.frameActive
      || device->transientFrameOffset != offset
      || device->transientFrameIndex != frame
      || device->currentFrameStats.requestedBindCalls != binds) {
    fprintf(stderr, "unavailable drawable changed frame state\n");
    goto cleanup;
  }

  ok = 1;

cleanup:
  [layer release];
  return ok;
}

int
gpu_test_metal_errors(GPUDevice *device) {
  if (gpuDeviceApi(device)->backend != GPU_BACKEND_METAL) {
    return 1;
  }

  @autoreleasepool {
    return check_native_errors() && check_surface_status(device);
  }
}
