/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#ifndef gpu_pipeline_h
#define gpu_pipeline_h

#include "common.h"
#include "pixelformat.h"
#include "library.h"

typedef struct GPUPipeline {
  void *priv;
} GPUPipeline;

typedef struct GPURenderState {
  void *priv;
} GPURenderState;

typedef enum GPUFunctionType {
  GPU_FUNCTION_VERT = 1,
  GPU_FUNCTION_FRAG = 2
} GPUFunctionType;

GPU_EXPORT
GPUPipeline*
gpuPipelineNew(GPUPixelFormat pixelFormat);

GPU_EXPORT
GPURenderState*
gpuRenderStateNew(GPUDevice   * __restrict device,
                  GPUPipeline * __restrict pipeline);

GPU_EXPORT
void
gpuFunction(GPUPipeline * __restrict pipline,
            GPUFunction * __restrict func,
            GPUFunctionType          functype);

GPU_EXPORT
void
gpuColorFormat(GPUPipeline * __restrict pipline,
               uint32_t                 index,
               GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuDepthFormat(GPUPipeline * __restrict pipline,
               GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuStencilFormat(GPUPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpuSampleCount(GPUPipeline * __restrict pipline,
               uint32_t                 sampleCount);

#endif /* gpu_pipeline_h */
