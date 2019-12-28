/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/device.h"
#include "../../../include/gpu/pipeline.h"
#include "../../../include/gpu/pixelformat.h"
#include "../../../include/gpu/library.h"
#include "../../../include/gpu/gpu.h"
#include <stdio.h>
#include <cmt/cmt.h>

GPU_EXPORT
GPUPipeline*
gpu_pipeline_new(GPUPixelFormat pixelFormat) {
  GPUPipeline  *pipeline;
  MtRenderDesc *renderDesc;

  renderDesc = mtRenderPipelineCreate((MtPixelFormat)pixelFormat);
  pipeline   = calloc(1, sizeof(*pipeline));

  pipeline->priv = renderDesc;

  return pipeline;
}

GPU_EXPORT
GPURenderState*
gpu_renderstate_new(GPUDevice   * __restrict device,
                    GPUPipeline * __restrict pipeline) {
  GPUPipeline      *rederPipline;
  MtRenderPipeline *mtRederPipline;

  mtRederPipline = mtRenderStateCreate(device->priv, pipeline->priv);
  rederPipline   = calloc(1, sizeof(*rederPipline));

  rederPipline->priv = mtRederPipline;

  return NULL;
}

GPU_EXPORT
void
gpu_function(GPUPipeline    * __restrict pipline,
             GPUFunction    * __restrict func,
             GPUFunctionType             functype) {
  mtSetFunc(pipline->priv, func->priv, (MtFuncType)functype);
}

GPU_EXPORT
void
gpu_colorfm(GPUPipeline * __restrict pipline,
            uint32_t                 index,
            GPUPixelFormat           pixelFormat) {
  mtColorPixelFormat(pipline->priv, index, (MtPixelFormat)pixelFormat);
}

GPU_EXPORT
void
gpu_depthfm(GPUPipeline * __restrict pipline,
            GPUPixelFormat           pixelFormat) {
  mtDepthPixelFormat(pipline->priv, (MtPixelFormat)pixelFormat);
}

GPU_EXPORT
void
gpu_stencfm(GPUPipeline * __restrict pipline,
            GPUPixelFormat           pixelFormat) {
  mtStencilPixelFormat(pipline->priv, (MtPixelFormat)pixelFormat);
}

GPU_EXPORT
void
gpu_samplco(GPUPipeline * __restrict pipline,
            uint32_t                 sampleCount) {
  mtSampleCount(pipline->priv, sampleCount);
}
