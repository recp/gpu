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
#include <math.h>

#if !defined(_WIN32) && !defined(WIN32)
#  include <time.h>
#endif

static bool
frameClockStart(uint64_t *ticks, uint64_t *frequency) {
#if defined(_WIN32) || defined(WIN32)
  LARGE_INTEGER counter;
  LARGE_INTEGER timerFrequency;

  if (!QueryPerformanceFrequency(&timerFrequency)
      || !QueryPerformanceCounter(&counter)
      || timerFrequency.QuadPart <= 0 || counter.QuadPart < 0) {
    return false;
  }

  *ticks     = (uint64_t)counter.QuadPart;
  *frequency = (uint64_t)timerFrequency.QuadPart;
#else
  struct timespec now;

  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return false;
  }

  *ticks     = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
  *frequency = 1000000000ull;
#endif

  return true;
}

static double
frameClockElapsedMs(const GPUFrame *frame) {
  uint64_t frequency;
  uint64_t start;
  uint64_t end;

  if (!frame || frame->cpuEncodeFrequency == 0u
      || !frameClockStart(&end, &frequency)
      || frequency != frame->cpuEncodeFrequency) {
    return 0.0;
  }

  start = frame->cpuEncodeStartTicks;

  if (end < start) {
    return 0.0;
  }

  return (double)(end - start) * 1000.0 / (double)frequency;
}

static GPUFrame*
frameBegin(GPUSwapchain *swapchain, void *drawable) {
  GPUApi    *api;
  GPUDevice *device;
  GPUFrame  *frame;
  uint32_t   frameIndex;

  if (!swapchain)
    return NULL;

  device = swapchain->device;

  if (!(api = deviceApi(device)))
    return NULL;

  if (!api->frame.beginFrame)
    return NULL;

  if (drawable && !api->frame.beginDrawable)
    return NULL;

  if (devicePrepareFrameSlotTimeout(device, &frameIndex, drawable ? 0u : UINT64_MAX) != GPU_OK)
    return NULL;

  frame = drawable && api->frame.beginDrawable
            ? api->frame.beginDrawable(api, swapchain, drawable)
            : api->frame.beginFrame(api, swapchain);

  if (frame) {
    deviceActivateFrame(device, frameIndex);

    frame->device = device;

    if (frame->target) {
      frame->target->device = frame->device;
    }

    if (frame->targetView && frame->target) {
      frame->targetView->_texture = frame->target;
    }

    frame->transientFrameIndex  = device->transientFrameIndex;
    frame->transientFrameActive = device->transientConfigured;

    if (!device->runtimeConfig.enableStats
        || !frameClockStart(&frame->cpuEncodeStartTicks,
                            &frame->cpuEncodeFrequency)) {
      frame->cpuEncodeStartTicks = 0u;
      frame->cpuEncodeFrequency  = 0u;
    }
  }

  return frame;
}

GPU_HIDE
GPUFrame*
frameBeginDrawable(GPUSwapchain *swapchain, void *drawable) {
  return drawable ? frameBegin(swapchain, drawable) : NULL;
}

GPU_EXPORT
GPUFrame*
GPUBeginFrame(GPUSwapchain *swapchain) {
  GPUApi *api;

  if (!swapchain)
    return NULL;

  if (swapchain->pacer) {
    api = deviceApi(swapchain->device);
    if (!api || !api->frame.prepareWithPacer
        || !api->frame.prepareWithPacer(swapchain->pacer))
      return NULL;
  }

  return frameBegin(swapchain, NULL);
}

GPU_EXPORT
GPUResult
GPUCreateFramePacer(GPUDevice                     *device,
                    const GPUFramePacerCreateInfo *info,
                    GPUFramePacer                **outPacer) {
  GPUFramePacer *pacer;
  GPUApi        *api;
  GPUResult      result;

  if (outPacer)
    *outPacer = NULL;

  if (!device || !info || !outPacer || !info->swapchain || !info->callback
      || info->swapchain->device != device || info->swapchain->pacer
      || info->chain.sType != GPU_STRUCTURE_TYPE_FRAME_PACER_CREATE_INFO
      || info->chain.structSize < sizeof(*info) || info->chain.pNext
      || info->latencyFrames > 2u
      || !isfinite(info->minRate) || !isfinite(info->maxRate) || !isfinite(info->preferredRate)
      || info->minRate < 0.0f || info->maxRate < 0.0f || info->preferredRate < 0.0f
      || info->minRate > info->maxRate || info->preferredRate > info->maxRate
      || (info->preferredRate > 0.0f && info->preferredRate < info->minRate))
    return GPU_ERROR_INVALID_ARGUMENT;

  api = deviceApi(device);
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_FRAME_PACER) || !api
      || !api->frame.createPacer || !api->frame.beginDrawable)
    return GPU_ERROR_UNSUPPORTED;

  if (!(pacer = calloc(1u, sizeof(*pacer))))
    return GPU_ERROR_OUT_OF_MEMORY;

  pacer->device    = device;
  pacer->swapchain = info->swapchain;
  pacer->callback  = info->callback;
  pacer->user      = info->user;
  result = api->frame.createPacer(pacer, info);

  if (result != GPU_OK) {
    free(pacer);
    return result;
  }

  info->swapchain->pacer = pacer;
  *outPacer = pacer;
  return GPU_OK;
}

GPU_EXPORT
void
GPUSetFramePacerPaused(GPUFramePacer *pacer, bool paused) {
  GPUApi *api;

  api = pacer ? deviceApi(pacer->device) : NULL;
  if (api && api->frame.pausePacer)
    api->frame.pausePacer(pacer, paused);
}

GPU_EXPORT
void
GPUDestroyFramePacer(GPUFramePacer *pacer) {
  GPUApi *api;

  if (!pacer)
    return;

  api = deviceApi(pacer->device);
  if (api && api->frame.destroyPacer)
    api->frame.destroyPacer(pacer);

  pacer->swapchain->pacer = NULL;
  free(pacer);
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
GPUEndFrame(GPUFrame *frame) {
  GPUApi *api;

  if (!frame)
    return;

  if (!(api = deviceApi(frame->device)))
    return;

  if (!api->frame.endFrame)
    return;

  frame->transientFrameActive = false;

  if (frame->device->runtimeConfig.enableStats) {
    frame->device->currentFrameStats.cpuEncodeMs = frameClockElapsedMs(frame);
  }

  deviceEndFrame(frame->device);
  api->frame.endFrame(api, frame);
}

GPU_EXPORT
GPUResult
GPUFinishFrame(GPUQueue         *__restrict cmdq,
               GPUCommandBuffer *__restrict cmdb,
               GPUFrame         *__restrict frame) {
  GPUQueueSubmitInfo submitInfo = {0};
  GPUCommandBuffer  *buffers[1];
  GPUResult          result;

  if (!cmdq || !cmdb || !frame)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (cmdb->_submitted || cmdb->_activeEncoder || cmdb->_queue != cmdq
      || commandQueueDevice(cmdq) != frame->device) {
    GPUEndFrame(frame);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!schedulePresent(cmdb, frame)) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  buffers[0] = cmdb;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1;
  submitInfo.ppCommandBuffers   = buffers;

  result = GPUQueueSubmit(cmdq, &submitInfo);

  GPUEndFrame(frame);
  return result;
}
