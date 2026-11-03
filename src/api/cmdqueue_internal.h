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
  bool                          _orderedUploads;
};

typedef void (*GPUCommandBufferRecycleFn)(GPUCommandBuffer *cmdb);

GPU_HIDE
void
finishCommandBuffer(GPUCommandBuffer         *cmdb,
                    GPUCommandBufferRecycleFn recycle);

GPU_HIDE
void
discardCommandBufferState(GPUCommandBuffer         *cmdb,
                          GPUCommandBufferRecycleFn recycle);

GPU_HIDE
GPUResult
submitUploadCommandBuffer(GPUCommandBuffer *cmdb, GPUFence *fence);

GPU_HIDE
void
retainFence(GPUFence *fence);

static inline void
commandQueueLock(GPUQueue *queue, GPUApi *api) {
  if (api->cmdque.lock
      && (queue->_device->enabledFeatureMask & (UINT64_C(1) << GPU_FEATURE_THREADED_UPLOADS)))
    api->cmdque.lock(queue);
}

static inline void
commandQueueUnlock(GPUQueue *queue, GPUApi *api) {
  if (api->cmdque.unlock
      && (queue->_device->enabledFeatureMask & (UINT64_C(1) << GPU_FEATURE_THREADED_UPLOADS)))
    api->cmdque.unlock(queue);
}

static inline GPUDevice*
commandQueueDevice(const GPUQueue *queue) {
  return queue ? queue->_device : NULL;
}

static inline GPUApi*
commandQueueApi(const GPUQueue *queue) {
  return deviceApi(commandQueueDevice(queue));
}

static inline GPUDevice*
commandBufferDevice(const GPUCommandBuffer *cmdb) {
  return cmdb ? commandQueueDevice(cmdb->_queue) : NULL;
}

static inline GPUApi*
commandBufferApi(const GPUCommandBuffer *cmdb) {
  return deviceApi(commandBufferDevice(cmdb));
}

#endif /* gpu_cmdqueue_internal_h */
