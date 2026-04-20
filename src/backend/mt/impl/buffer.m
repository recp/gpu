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

GPU_HIDE
GPUBuffer*
mt_newBuffer(GPUDevice * __restrict device,
             size_t                 len,
             GPUResourceOptions     options) {
  GPUDeviceMT *deviceMT;
  MTLResourceOptions mtOptions;
  
  deviceMT = device->_priv;
  mtOptions = (MTLResourceOptions)options;

  return (GPUBuffer *)[deviceMT->device newBufferWithLength:len options:mtOptions];
}

GPU_HIDE
size_t
mt_bufferLength(GPUBuffer * __restrict buff) {
  return [(id<MTLBuffer>)buff length];
}

GPU_HIDE
GPUBuffer*
mt_bufferContents(GPUBuffer * __restrict buff) {
  return (GPUBuffer *)[(id<MTLBuffer>)buff contents];
}

GPU_HIDE
void
mt_initBuff(GPUApiBuffer *api) {
  api->newBuffer = mt_newBuffer;
  api->length    = mt_bufferLength;
  api->contents  = mt_bufferContents;
}
