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
gpu_pipeline_create(GPUPixelFormat pixelFormat) {
  GPUPipeline  *pipeline;
  MtRenderDesc *renderDesc;

  renderDesc = mtRenderPipelineCreate((MtPixelFormat)pixelFormat);
  pipeline   = calloc(1, sizeof(*pipeline));

  pipeline->priv = renderDesc;

  return pipeline;
}

GPU_EXPORT
GPURenderState*
gpu_renderstate_create(GPUDevice   * __restrict device,
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
gpu_func_set(GPUPipeline *pipline,
             GPUFunction *func,
             GPUFuncType  functype) {
  mtSetFunc(pipline->priv, func->priv, (MtFuncType)functype);
}

