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
  MtCommandBuffer *mcq;
  
  mcq = mtDeviceNewBufferWithLength(device->_priv, len, (MtResourceOptions)options);
  
  return mcq;
}

GPU_HIDE
size_t
mt_bufferLength(GPUBuffer * __restrict buff) {
  return mtBufferLength(buff);
}

GPU_HIDE
GPUBuffer*
mt_bufferContents(GPUBuffer * __restrict buff) {
  return mtBufferContents(buff);
}

GPU_HIDE
void
mt_initBuff(GPUApiBuffer *api) {
  api->newBuffer = mt_newBuffer;
  api->length    = mt_bufferLength;
  api->contents  = mt_bufferContents;
}
