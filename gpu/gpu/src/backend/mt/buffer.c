/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/device.h"
#include "../../../include/gpu/library.h"
#include "../../../include/gpu/vertex.h"
#include "../../../include/gpu/pipeline.h"
#include "../../../include/gpu/depthstencil.h"
#include "../../../include/gpu/buffer.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPUBuffer*
gpu_buffer_new(GPUDevice * __restrict device,
               size_t                 len,
               GPUResourceOptions     options) {
  GPUBuffer       *cq;
  MtCommandBuffer *mcq;

  mcq      = mtBufferCreate(device, len, (MtResourceOptions)options);
  cq       = calloc(1, sizeof(*cq));
  cq->priv = mcq;

  return cq;
}
