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
#include "../../../include/gpu/pipeline.h"
#include "../../../include/gpu/pixelformat.h"
#include "../../../include/gpu/library.h"
#include "../../../include/gpu/gpu.h"
#include <stdio.h>
#include <cmt/cmt.h>

GPU_EXPORT
GPUPipeline*
gpuPipelineNew(GPUPixelFormat pixelFormat) {
  GPUPipeline  *pipeline;
  MtRenderDesc *renderDesc;
  
  renderDesc = mtRenderPipelineCreate((MtPixelFormat)pixelFormat);
  pipeline   = calloc(1, sizeof(*pipeline));
  
  pipeline->priv = renderDesc;
  
  return pipeline;
}

GPU_EXPORT
GPURenderState*
gpuRenderStateNew(GPUDevice   * __restrict device,
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
gpuFunction(GPUPipeline    * __restrict pipline,
            GPUFunction    * __restrict func,
            GPUFunctionType             functype) {
  mtSetFunc(pipline->priv, func->priv, (MtFuncType)functype);
}

GPU_EXPORT
void
gpuColorFormat(GPUPipeline * __restrict pipline,
               uint32_t                 index,
               GPUPixelFormat           pixelFormat) {
  mtColorPixelFormat(pipline->priv, index, (MtPixelFormat)pixelFormat);
}

GPU_EXPORT
void
gpuDepthFormat(GPUPipeline * __restrict pipline,
               GPUPixelFormat           pixelFormat) {
  mtDepthPixelFormat(pipline->priv, (MtPixelFormat)pixelFormat);
}

GPU_EXPORT
void
gpuStencilFormat(GPUPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat) {
  mtStencilPixelFormat(pipline->priv, (MtPixelFormat)pixelFormat);
}

GPU_EXPORT
void
gpuSampleCount(GPUPipeline * __restrict pipline,
               uint32_t                 sampleCount) {
  mtSampleCount(pipline->priv, sampleCount);
}
