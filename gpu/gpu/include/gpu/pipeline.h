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
gpu_pipeline_create(GPUPixelFormat pixelFormat);

GPU_EXPORT
GPURenderState*
gpu_renderstate_create(GPUDevice   * __restrict device,
                       GPUPipeline * __restrict pipeline);

GPU_EXPORT
void
gpu_function_set(GPUPipeline    *pipline,
                 GPUFunction    *func,
                 GPUFunctionType functype);

#endif /* gpu_pipeline_h */
