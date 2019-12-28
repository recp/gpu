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
#include "../../../include/gpu/cmdqueue.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPUCommandQueue*
gpu_cmdqueue_new(GPUDevice * __restrict device) {
  GPUCommandQueue *cq;
  MtCommandQueue  *mcq;
  
  mcq      = mtCommandQueueCreate(device->priv);
  cq       = calloc(1, sizeof(*cq));
  cq->priv = mcq;
  
  return cq;
}

GPU_EXPORT
GPUCommandBuffer*
gpu_cmdbuff_new(GPUCommandQueue * __restrict cmdq) {
  GPUCommandBuffer *cb;
  MtCommandBuffer  *mcb;
  
  mcb      = mtCommandBufferCreate(cmdq);
  cb       = calloc(1, sizeof(*cb));
  cb->priv = mcb;
  
  return cb;
}

GPU_EXPORT
void
gpu_cmdbuff_oncomplete(GPUCommandQueue * __restrict cmdb,
                       GPUCommandBufferOnCompleteFn oncomplete) {
  
}
