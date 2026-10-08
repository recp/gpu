/*
 * Copyright (C) 2026 Recep Aslantas
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

static GPUShaderLibrary*
cuda_newLibraryWithSource(GPUDevice  *device,
                          const char *source,
                          uint64_t    sourceSize,
                          uint32_t    compileFlags) {
  ShaderLibraryCuda    *native;
  GPUShaderLibrary     *library;

  (void)compileFlags;

  if (!device || !source || sourceSize == 0u) {
    return NULL;
  }

  library = calloc(1, sizeof(*library));
  native  = calloc(1, sizeof(*native));

  if (!library || !native) {
    free(native);
    free(library);
    return NULL;
  }

  if (!(native->module = cuda_createModule(device, source, sourceSize))) {
    free(native);
    free(library);
    return NULL;
  }

  library->_priv = native;

  return library;
}

static void
cuda_destroyLibrary(GPUShaderLibrary *library) {
  ShaderLibraryCuda    *native;

  native = library ? library->_priv : NULL;

  if (native) {
    cuda_releaseModule(native->module);
    free(native);
  }

  free(library);
}

void
cuda_initLibrary(ApiLibrary    *api) {
  api->newLibraryWithSource = cuda_newLibraryWithSource;
  api->destroyLibrary       = cuda_destroyLibrary;
}
