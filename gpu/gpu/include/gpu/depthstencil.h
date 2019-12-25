/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef depthstencil_h
#define depthstencil_h

#include "common.h"
#include "pipeline.h"

typedef struct GPUDepthStencil {
  void *priv;
} GPUDepthStencil;

GPU_EXPORT
GPUDepthStencil*
gpu_depthstencil_new(void);

#endif /* depthstencil_h */
