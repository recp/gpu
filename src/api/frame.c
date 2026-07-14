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
#include "cmdqueue_internal.h"
#include "device_internal.h"
#include "frame_internal.h"
#include "swapchain_internal.h"
#include "texture_internal.h"

#if !defined(_WIN32) && !defined(WIN32)
#  include <time.h>
#endif

static bool
gpu_frameClockStart(uint64_t *ticks, uint64_t *frequency) {
#if defined(_WIN32) || defined(WIN32)
  LARGE_INTEGER counter;
  LARGE_INTEGER timerFrequency;

  if (!QueryPerformanceFrequency(&timerFrequency) ||
      !QueryPerformanceCounter(&counter) ||
      timerFrequency.QuadPart <= 0 || counter.QuadPart < 0) {
    return false;
  }

  *ticks     = (uint64_t)counter.QuadPart;
  *frequency = (uint64_t)timerFrequency.QuadPart;
#else
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return false;
  }

  *ticks     = (uint64_t)now.tv_sec * 1000000000ull +
               (uint64_t)now.tv_nsec;
  *frequency = 1000000000ull;
#endif
  return true;
}

static double
gpu_frameClockElapsedMs(const GPUFrame *frame) {
  uint64_t frequency;
  uint64_t start;
  uint64_t end;

  if (!frame || frame->cpuEncodeFrequency == 0u ||
      !gpu_frameClockStart(&end, &frequency) ||
      frequency != frame->cpuEncodeFrequency) {
    return 0.0;
  }

  start = frame->cpuEncodeStartTicks;
  if (end < start) {
    return 0.0;
  }
  return (double)(end - start) * 1000.0 / (double)frequency;
}

GPU_EXPORT
GPUFrame*
GPUBeginFrame(GPUSwapchain* swapchain) {
  GPUApi *api;
  GPUDevice *device;
  GPUFrame *frame;

  if (!swapchain)
    return NULL;
  device = swapchain->device;
  if (!(api = gpuDeviceApi(device)))
    return NULL;
  if (!api->frame.beginFrame)
    return NULL;
  if (gpuDeviceBeginFrame(device) != GPU_OK)
    return NULL;

  frame = api->frame.beginFrame(api, swapchain);
  if (frame) {
    frame->device = device;
    if (frame->target) {
      frame->target->device = frame->device;
    }
    frame->transientFrameIndex  = device->transientFrameIndex;
    frame->transientFrameActive = device->transientConfigured;
    if (!device->runtimeConfig.enableStats ||
        !gpu_frameClockStart(&frame->cpuEncodeStartTicks,
                             &frame->cpuEncodeFrequency)) {
      frame->cpuEncodeStartTicks = 0u;
      frame->cpuEncodeFrequency  = 0u;
    }
  }

  return frame;
}

GPU_EXPORT
GPUTexture*
GPUFrameGetTarget(GPUFrame *frame) {
  return frame ? frame->target : NULL;
}

GPU_EXPORT
GPUTextureView*
GPUFrameGetTargetView(GPUFrame *frame) {
  return frame ? frame->targetView : NULL;
}

GPU_EXPORT
void
GPUEndFrame(GPUFrame* frame) {
  GPUApi *api;

  if (!frame)
    return;
  if (!(api = gpuDeviceApi(frame->device)))
    return;
  if (!api->frame.endFrame)
    return;

  frame->transientFrameActive = false;
  if (frame->device->runtimeConfig.enableStats) {
    frame->device->currentFrameStats.cpuEncodeMs =
      gpu_frameClockElapsedMs(frame);
  }
  gpuDeviceEndFrame(frame->device);
  api->frame.endFrame(api, frame);
}

GPU_EXPORT
GPUResult
GPUFinishFrame(GPUQueue         * __restrict cmdq,
               GPUCommandBuffer * __restrict cmdb,
               GPUFrame         * __restrict frame) {
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUResult result;

  if (!cmdq || !cmdb || !frame)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (cmdb->_submitted || cmdb->_activeEncoder || cmdb->_queue != cmdq ||
      gpuCommandQueueDevice(cmdq) != frame->device) {
    GPUEndFrame(frame);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  GPUSchedulePresent(cmdb, frame);

  buffers[0] = cmdb;
  submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1;
  submitInfo.ppCommandBuffers = buffers;
  result = GPUQueueSubmit(cmdq, &submitInfo);

  GPUEndFrame(frame);
  return result;
}
