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
#include "../../../include/gpu/cmdqueue.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPUBuffer*
gpuBufferNew(GPUDevice * __restrict device,
             size_t                 len,
             GPUResourceOptions     options) {
  GPUBuffer       *cq;
  MtCommandBuffer *mcq;

  mcq      = mtBufferCreate(device, len, (MtResourceOptions)options);
  cq       = calloc(1, sizeof(*cq));
  cq->priv = mcq;

  return cq;
}

GPU_EXPORT
void
gpuPresent(GPUCommandBuffer *cmdb, void *drawable) {
  mtPresent(cmdb->priv, drawable);
}
