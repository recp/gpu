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

typedef struct GPUCallback {
  void                        *sender;
  void                        *param;
  GPUCommandBufferOnCompleteFn onComplete;
} GPUCallback;

static
GPU_HIDE
void
gpu_cmdoncomplete(void * __restrict sender, MtCommandBuffer *cmdb);

GPU_EXPORT
GPUCommandQueue*
gpuCmdQueNew(GPUDevice * __restrict device) {
  GPUCommandQueue *cq;
  MtCommandQueue  *mcq;
  
  mcq      = mtCommandQueueCreate(device->priv);
  cq       = calloc(1, sizeof(*cq));
  cq->priv = mcq;
  
  return cq;
}

GPU_EXPORT
GPUCommandBuffer*
gpuCmdBufNew(GPUCommandQueue  * __restrict cmdb,
               void             * __restrict sender,
               GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCommandBuffer *cb;
  MtCommandBuffer  *mcb;
  
  mcb      = mtCommandBufferCreate(cmdb);
  cb       = calloc(1, sizeof(*cb));
  cb->priv = mcb;
  
  if (oncomplete)
    gpuCmdBufOnComplete(cb, sender, oncomplete);
  
  return cb;
}

GPU_EXPORT
void
gpuCmdBufOnComplete(GPUCommandBuffer * __restrict cmdb,
                      void             * __restrict sender,
                      GPUCommandBufferOnCompleteFn  oncomplete) {
  GPUCallback cb;
  cb.sender     = sender;
  cb.param      = cmdb;
  cb.onComplete = oncomplete;
  
  mtCommandBufferOnComplete(cmdb->priv, &cb, gpu_cmdoncomplete);
}

GPU_EXPORT
void
gpuCommit(GPUCommandBuffer * __restrict cmdb) {
  mtCommit(cmdb->priv);
}

static
GPU_HIDE
void
gpu_cmdoncomplete(void * __restrict sender, MtCommandBuffer *cmdb) {
  GPUCallback *cb;
  
  cb = sender;
  cb->onComplete(cb->sender, cb->param);
}
