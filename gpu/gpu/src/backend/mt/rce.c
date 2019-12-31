/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/cmdqueue.h"
#include "../../../include/gpu/cmd-enc.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPURenderCommandEncoder*
gpuRenderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  return mtRenderCommandEncoder(cmdb, pass);
}

GPU_EXPORT
void
gpuFrontFace(GPURenderCommandEncoder *rce, GPUWinding winding) {
  mtFrontFace(rce, (MtWinding)winding);
}

GPU_EXPORT
void
gpuCullMode(GPURenderCommandEncoder *rce, GPUCullMode mode) {
  mtCullMode(rce, (MtCullMode)mode);
}

GPU_EXPORT
void
gpuSetRenderPipeline(GPURenderCommandEncoder *rce, GPUPipeline *pipline) {
  mtSetRenderState(rce, pipline);
}

GPU_EXPORT
void
gpuSetDepthStencil(GPURenderCommandEncoder *rce, GPUDepthStencil *ds) {
  mtSetDepthStencil(rce, ds->priv);
}
