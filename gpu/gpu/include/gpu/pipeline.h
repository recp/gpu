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

GPU_EXPORT
GPUPipeline*
gpu_pipeline_new(GPUPixelFormat pixelFormat);

GPU_EXPORT
GPURenderState*
gpu_renderstate_new(GPUDevice   * __restrict device,
                    GPUPipeline * __restrict pipeline);

GPU_EXPORT
void
gpu_function(GPUPipeline * __restrict pipline,
             GPUFunction * __restrict func,
             GPUFunctionType          functype);

GPU_EXPORT
void
gpu_color_format(GPUPipeline * __restrict pipline,
                 uint32_t                 index,
                 GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpu_depth_format(GPUPipeline * __restrict pipline,
                 GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpu_stencil_format(GPUPipeline * __restrict pipline,
                   GPUPixelFormat           pixelFormat);

GPU_EXPORT
void
gpu_samplecount(GPUPipeline * __restrict pipline,
                uint32_t                 sampleCount);

#endif /* gpu_pipeline_h */
