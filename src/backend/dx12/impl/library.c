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
dx12__newLibrary(GPUDevice *device,
                 const void *source,
                 uint64_t sourceSize,
                 bool binary) {
  GPUShaderLibraryDX12 *native;
  GPUShaderLibrary     *library;

  if (!device || !source || sourceSize == 0u ||
      sourceSize > (uint64_t)SIZE_MAX - (binary ? 0u : 1u)) {
    return NULL;
  }

  library = calloc(1, sizeof(*library));
  native  = calloc(1, sizeof(*native));
  if (!library || !native) {
    free(native);
    free(library);
    return NULL;
  }

  native->source = malloc((size_t)sourceSize + (binary ? 0u : 1u));
  if (!native->source) {
    free(native);
    free(library);
    return NULL;
  }

  memcpy(native->source, source, (size_t)sourceSize);
  if (!binary) {
    native->source[sourceSize] = '\0';
  }
  native->sourceSize         = sourceSize;
  native->binary             = binary;
  InitializeSRWLock(&native->cacheLock);
  library->_priv             = native;
  return library;
}

GPU_HIDE
GPUShaderLibrary*
dx12_newLibraryWithSource(GPUDevice *device,
                          const char *source,
                          uint64_t sourceSize) {
  return dx12__newLibrary(device, source, sourceSize, false);
}

GPU_HIDE
GPUShaderLibrary*
dx12_newLibraryWithBinary(GPUDevice *device,
                          const void *source,
                          uint64_t sourceSize) {
  return dx12__newLibrary(device, source, sourceSize, true);
}

GPU_HIDE
void
dx12_destroyLibrary(GPUShaderLibrary *library) {
  DX12ShaderCacheEntry *entry;
  DX12ShaderCacheEntry *next;
  GPUShaderLibraryDX12 *native;

  if (!library) {
    return;
  }

  native = library->_priv;
  if (native) {
    for (entry = native->cache; entry; entry = next) {
      next = entry->next;
      free(entry->data);
      free(entry->entry);
      free(entry);
    }
    free(native->source);
    free(native);
  }
  free(library);
}

GPU_HIDE
void
dx12_initLibrary(GPUApiLibrary *api) {
  api->newLibraryWithSource = dx12_newLibraryWithSource;
  api->newLibraryWithBinary = dx12_newLibraryWithBinary;
  api->destroyLibrary       = dx12_destroyLibrary;
}
