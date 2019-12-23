/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#include "../../../include/gpu/device.h"
#include "../../../include/gpu/library.h"
#include <cmt/cmt.h>

GPU_EXPORT
GPULibrary*
gpu_library_default(GPUDevice *device) {
  GPULibrary *library;
  MtLibrary  *mtLibrary;

  mtLibrary = mtDefaultLibrary(device->priv);
  library   = calloc(1, sizeof(*library));

  library->priv = mtLibrary;

  return library;
}

GPU_EXPORT
GPUFunction*
gpu_function_new(GPULibrary *lib, const char *name) {
  GPUFunction *func;
  MtFunction  *mtFunc;

  mtFunc = mtCreateFunc(lib->priv, name);
  func   = calloc(1, sizeof(*func));

  func->priv = mtFunc;

  return func;
}
