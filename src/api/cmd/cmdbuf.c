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

#include "../../common.h"
#include "../cmdqueue_internal.h"
#include "../frame_internal.h"

GPU_HIDE
bool
gpuSchedulePresent(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  GPUApi *api;

  if (!cmdb || cmdb->_submitted || !frame || !frame->drawable ||
      gpuCommandBufferDevice(cmdb) != frame->device) {
    return false;
  }

  if (!(api = gpuCommandBufferApi(cmdb)))
    return false;
  if (!api->cmdbuf.presentDrawable)
    return false;

  if (!api->cmdbuf.presentDrawable(cmdb, frame)) {
    return false;
  }
  cmdb->_recordsGPUFrameTime = frame->device->runtimeConfig.enableStats;
  if (frame->transientFrameActive) {
    cmdb->_transientFrameIndex  = frame->transientFrameIndex;
    cmdb->_transientFrameTagged = true;
  }
  return true;
}

GPU_EXPORT
void
GPUSchedulePresent(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  (void)gpuSchedulePresent(cmdb, frame);
}

GPU_EXPORT
void
GPUPresent(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  GPUSchedulePresent(cmdb, frame);
}
