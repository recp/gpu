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

GPU_HIDE
GPUShaderLibrary*
dx12_newLibraryWithSource(GPUDevice *device,
                          const char *source,
                          uint64_t    sourceSize) {
  GPUShaderLibraryDX12 *native;
  GPUShaderLibrary     *library;

  if (!device || !source || sourceSize == 0u ||
      sourceSize > (uint64_t)SIZE_MAX - 1u) {
    return NULL;
  }

  library = calloc(1, sizeof(*library));
  native  = calloc(1, sizeof(*native));
  if (!library || !native) {
    free(native);
    free(library);
    return NULL;
  }

  native->source = malloc((size_t)sourceSize + 1u);
  if (!native->source) {
    free(native);
    free(library);
    return NULL;
  }

  memcpy(native->source, source, (size_t)sourceSize);
  native->source[sourceSize] = '\0';
  native->sourceSize         = sourceSize;
  library->_priv             = native;
  return library;
}

GPU_HIDE
void
dx12_destroyLibrary(GPUShaderLibrary *library) {
  GPUShaderLibraryDX12 *native;

  if (!library) {
    return;
  }

  native = library->_priv;
  if (native) {
    free(native->source);
    free(native);
  }
  free(library);
}

GPU_HIDE
void
dx12_initLibrary(GPUApiLibrary *api) {
  api->newLibraryWithSource = dx12_newLibraryWithSource;
  api->destroyLibrary       = dx12_destroyLibrary;
}
