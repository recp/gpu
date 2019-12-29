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
#include "../../../include/gpu/pass.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPURenderPassDesc*
gpu_pass_new() {
  GPURenderPassDesc *pass;
  MtRenderPassDesc  *mpass;
  
  mpass      = mtPassCreate();
  pass       = calloc(1, sizeof(*pass));
  pass->priv = mpass;

  return pass;
}
