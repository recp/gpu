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
gpuRenderCommandEncoder(GPUCommandBuffer  *cmdb,
                        GPURenderPassDesc *passDesc) {
  return mtRenderCommandEncoder(cmdb, passDesc);
}

GPU_EXPORT
void
gpuFrontFace(GPUCommandBuffer *cmdb,
             GPUWinding        winding) {

}

GPU_EXPORT
void
gpuCullMode(GPUCommandBuffer *cmdb,
            GPUCullMode       mode) {
  
}

GPU_EXPORT
void
gpuSetRenderPipeline(GPUCommandBuffer *cmdb,
                     GPUPipeline      *rs) {
  
}

GPU_EXPORT
void
gpuSetDepthStencil(GPUCommandBuffer *cmdb,
                   GPUDepthStencil  *ds) {
  
}
