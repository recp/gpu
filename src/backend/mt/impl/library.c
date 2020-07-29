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

#include "../common.h"

GPU_EXPORT
GPULibrary*
mt_defaultLibrary(GPUDevice *device) {
  GPULibrary *library;
  MtLibrary  *mtLibrary;

  mtLibrary = mtNewDefaultLibrary(device->priv);
  library   = calloc(1, sizeof(*library));

  library->priv = mtLibrary;

  return library;
}

GPU_EXPORT
GPUFunction*
mt_newFunction(GPULibrary *lib, const char *name) {
  GPUFunction *func;
  MtFunction  *mtFunc;

  mtFunc = mtNewFunctionWithName(lib->priv, name);
  func   = calloc(1, sizeof(*func));

  func->priv = mtFunc;

  return func;
}

GPU_HIDE
void
mt_initLibrary(GPUApiLibrary *api) {
  api->defaultLibrary = mt_defaultLibrary;
  api->newFunction    = mt_newFunction;
}
