/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/pass.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPURenderPassDesc*
gpuPassNew() {
  return mtPassCreate();;
}
