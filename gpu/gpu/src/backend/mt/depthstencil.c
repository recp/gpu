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
#include <cmt/cmt.h>

GPU_EXPORT
GPUDepthStencil*
gpu_depthstencil_new(GPUCompareFunction depthCompareFunc,
                     bool               depthWriteEnabled) {
  GPUDepthStencil *ds;
  MtDepthStencil  *mds;
  
  mds = mtDepthStencil(depthCompareFunc, depthWriteEnabled);
  ds  = calloc(1, sizeof(*ds));

  ds->priv = mds;

  return ds;
}
