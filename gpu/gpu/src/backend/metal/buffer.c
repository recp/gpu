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

#include "../../../include/gpu/device.h"
#include "../../../include/gpu/library.h"
#include "../../../include/gpu/vertex.h"
#include "../../../include/gpu/pipeline.h"
#include "../../../include/gpu/depthstencil.h"
#include "../../../include/gpu/buffer.h"
#include "../../../include/gpu/cmdqueue.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPUBuffer*
gpuNewBuffer(GPUDevice * __restrict device,
             size_t                 len,
             GPUResourceOptions     options) {
  MtCommandBuffer *mcq;

  mcq = mtDeviceNewBufferWithLength(device->priv, len, (MtResourceOptions)options);

  return mcq;
}

GPU_EXPORT
size_t
gpuBufferLength(GPUBuffer * __restrict buff) {
  return mtBufferLength(buff);
}

GPU_EXPORT
GPUBuffer*
gpuBufferContents(GPUBuffer * __restrict buff) {
  return mtBufferContents(buff);
}

GPU_EXPORT
void
gpuPresent(GPUCommandBuffer *cmdb, void *drawable) {
  mtCommandBufferPresentDrawable(cmdb->priv, drawable);
}