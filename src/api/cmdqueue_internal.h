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

#ifndef gpu_cmdqueue_internal_h
#define gpu_cmdqueue_internal_h

#include "../common.h"
#include "device_internal.h"

struct GPUQueue {
  void            *_priv;
  GPUDevice       *_device;
  GPUQueueFlagBits bits;
};

struct GPUSemaphore {
  void      *_priv;
  GPUDevice *_device;
};

struct GPUCommandBuffer {
  void                         *_priv;
  GPUQueue                     *_queue;
  GPUFence                     *_submitFence;
  GPUFence                     *_transientFence;
  GPUQuerySet                  *_pipelineStatsQuery;
  void                         *_onCompleteSender;
  GPUCommandBufferCompletionFn  _onComplete;
  uint32_t                      _pipelineStatsQueryIndex;
  uint32_t                      _transientFrameIndex;
  bool                          _submitted;
  bool                          _activeEncoder;
  bool                          _transientFrameTagged;
  bool                          _recordsGPUFrameTime;
};

static inline GPUDevice *
gpuCommandQueueDevice(const GPUQueue *queue) {
  return queue ? queue->_device : NULL;
}

static inline GPUApi *
gpuCommandQueueApi(const GPUQueue *queue) {
  return gpuDeviceApi(gpuCommandQueueDevice(queue));
}

static inline GPUDevice *
gpuCommandBufferDevice(const GPUCommandBuffer *cmdb) {
  return cmdb ? gpuCommandQueueDevice(cmdb->_queue) : NULL;
}

static inline GPUApi *
gpuCommandBufferApi(const GPUCommandBuffer *cmdb) {
  return gpuDeviceApi(gpuCommandBufferDevice(cmdb));
}

typedef void (*GPUCommandBufferRecycleFn)(GPUCommandBuffer *cmdb);

GPU_HIDE
void
gpuFinishCommandBuffer(GPUCommandBuffer          *cmdb,
                       GPUCommandBufferRecycleFn  recycle);

#endif /* gpu_cmdqueue_internal_h */
