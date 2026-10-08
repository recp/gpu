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

#include "../../../samples/common/apple.h"
#include "../../../samples/common/SampleApp.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static GPUDevice       *device;
static GPUQueue        *queue;
static GPUSwapchain    *swapchain;
static NSWindow        *window;
static NSTimer         *driver;
static id               terminateObserver;
static NSTimeInterval   started, pausedAt;
static atomic_uint      completed, errors;
static uint32_t         submitted, pausedFrames, phase;
static bool             paused, resumed, terminated;

static void
fail(const char *message) {
  fprintf(stderr, "native host lifecycle failed: %s\n", message);
  _Exit(1);
}

static void
finish(void) {
  if (phase != 3u || !terminated || !paused || !resumed
      || [(NSObject *)NSApp.delegate valueForKey:@"timer"]
      || !GPUSampleWaitForGPU(device, queue)
      || atomic_load(&completed) != submitted
      || atomic_load(&errors) != 0u) {
    fail("close, pause/resume or queue completion contract");
  }

  fprintf(stdout,
          "native host lifecycle passed: paused at %u, resumed and closed at %u, completed %u\n",
          pausedFrames,
          submitted,
          atomic_load(&completed));
}

static void
frame_completed(void *sender, GPUCommandBuffer *cmdb) {
  (void)sender;
  (void)cmdb;
  atomic_fetch_add(&completed, 1u);
}

static void
device_error(GPUDevice                *source,
             const GPUDeviceErrorInfo *info,
             void                     *userData) {
  (void)source;
  (void)userData;
  atomic_fetch_add(&errors, 1u);
  fprintf(stderr, "native host device error: %s\n", info->message);
}

static void
render(void *userData) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      pass  = {0};
  GPURenderPassEncoder        *encoder;
  GPUCommandBuffer           *cmdb;
  GPUFrame                   *frame;

  (void)userData;

  if (!(frame = GPUBeginFrame(swapchain))) {
    return;
  }

  if (GPUAcquireCommandBuffer(queue, "native-host-lifecycle", &cmdb) != GPU_OK) {
    GPUEndFrame(frame);
    fail("command buffer acquisition");
  }

  GPUSetCommandBufferCompletionHandler(cmdb, NULL, frame_completed);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;

  pass.colorAttachmentCount = 1u;
  pass.pColorAttachments    = &color;

  if (!(encoder = GPUBeginRenderPass(cmdb, &pass))) {
    GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    fail("render pass creation");
  }

  GPUEndRenderPass(encoder);

  if (GPUFinishFrame(queue, cmdb, frame) != GPU_OK) {
    fail("frame submission");
  }

  submitted++;
}

static void
ready(GPUResult   result,
      GPUAdapter *adapter,
      GPUDevice  *selected,
      void       *userData) {
  (void)adapter;
  (void)userData;

  if (result != GPU_OK || !selected) {
    fail("device creation");
  }

  device    = selected;
  queue     = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  swapchain = gpu_apple_sample_create_swapchain(device, NULL, 0u, 0u);

  if (!queue || !swapchain) {
    fail("sample queue or swapchain");
  }

  GPUSetDeviceErrorCallback(device, device_error, NULL);
  gpu_apple_set_main_loop(render, NULL, 120, true);
}

static void
tick(void) {
  NSTimer       *timer;
  NSTimeInterval now;

  now   = NSDate.timeIntervalSinceReferenceDate;
  timer = [(NSObject *)NSApp.delegate valueForKey:@"timer"];

  if (now - started > 10.0) {
    fprintf(stderr,
            "phase %u, active %d, submitted %u, completed %u, timer %d\n",
            phase,
            NSApp.active,
            submitted,
            atomic_load(&completed),
            timer != nil);
    fail("application lifecycle timeout");
  }

  if (phase == 0u && NSApp.active && submitted >= 3u) {
    window = NSApp.mainWindow;

    if (!window || !timer) {
      fail("active sample window or render timer");
    }

    phase = 1u;

    fprintf(stdout, "hide after %u frames\n", submitted);
    [NSApp hide:nil];
  } else if (phase == 1u && !NSApp.active && NSApp.hidden) {
    if (timer) {
      fail("render timer remains active after deactivation");
    }

    if (!paused) {
      paused       = true;
      pausedFrames = submitted;
      pausedAt     = now;
    }

    if (submitted != pausedFrames) {
      fail("frame submitted while application is inactive");
    }

    if (now - pausedAt >= 0.25) {
      phase = 2u;

      fprintf(stdout, "unhide after pause at %u frames\n", submitted);
      [NSApp unhide:nil];

      /* force activation to exercise the host's resume notification. */
      [NSApp activateIgnoringOtherApps:YES];
    }
  } else if (phase == 2u && NSApp.active && submitted >= pausedFrames + 3u) {
    if (!timer) {
      fail("render timer missing after activation");
    }

    resumed = true;
    phase   = 3u;

    [driver invalidate];
    driver = nil;
    [window performClose:nil];
  }
}

int
gpu_apple_sample_start(void) {
  WebGPURequest         request = {0};
  NSNotificationCenter *center;
  GPUInstance          *instance;

  atexit(finish);

  if (gpu_apple_sample_create_instance(NULL, &instance) != GPU_OK
      || request_webgpu_device(instance, &request, ready, NULL) != GPU_OK) {
    fail("sample initialization");
  }

  center = NSNotificationCenter.defaultCenter;

  terminateObserver = [center addObserverForName:NSApplicationWillTerminateNotification
                                         object:NSApp
                                          queue:nil
                                     usingBlock:^(NSNotification *notification) {
    (void)notification;
    terminated = true;
  }];

  started = NSDate.timeIntervalSinceReferenceDate;
  driver  = [NSTimer scheduledTimerWithTimeInterval:0.01
                                           repeats:YES
                                             block:^(NSTimer *timer) {
    (void)timer;
    tick();
  }];

  return 0;
}
