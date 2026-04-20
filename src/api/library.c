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
#include <us/compiler.h>

#define GPU_USL_EMBEDDED_BLOB_MAGIC 0x55534C45u
#define GPU_USL_EMBEDDED_BLOB_VERSION 1u
#define GPU_USL_EMBEDDED_ENCODING_TEXT 1u
#define GPU_USL_BACKEND_METAL 2u

typedef struct {
  uint32_t backend;
  uint32_t encoding;
  uint32_t data_offset;
  uint32_t data_size;
  uint32_t flags;
} GPUUSLEmbeddedBlobRecord;

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t record_count;
  uint32_t records_offset;
} GPUUSLEmbeddedBlobFooter;

static int
gpu_getEmbeddedMetalFromUSLBytecode(const void *bytecodeData,
                                    uint64_t bytecodeSize,
                                    const char **outMetal,
                                    uint64_t *outMetalSize) {
  const uint8_t *data;
  const GPUUSLEmbeddedBlobFooter *diskFooter;
  const GPUUSLEmbeddedBlobRecord *records;
  GPUUSLEmbeddedBlobFooter footer;

  if (outMetal) *outMetal = NULL;
  if (outMetalSize) *outMetalSize = 0;

  if (!bytecodeData || bytecodeSize < sizeof(GPUUSLEmbeddedBlobFooter)) {
    return 0;
  }

  data = (const uint8_t *)bytecodeData;
  diskFooter = (const GPUUSLEmbeddedBlobFooter *)(data + bytecodeSize - sizeof(*diskFooter));
  footer.magic = diskFooter->magic;
  footer.version = diskFooter->version;
  footer.record_count = diskFooter->record_count;
  footer.records_offset = diskFooter->records_offset;

  if (footer.magic != GPU_USL_EMBEDDED_BLOB_MAGIC ||
      footer.version != GPU_USL_EMBEDDED_BLOB_VERSION) {
    return 0;
  }

  if ((uint64_t)footer.records_offset +
      (uint64_t)footer.record_count * sizeof(GPUUSLEmbeddedBlobRecord) >
      bytecodeSize - sizeof(GPUUSLEmbeddedBlobFooter)) {
    return 0;
  }

  records = (const GPUUSLEmbeddedBlobRecord *)(data + footer.records_offset);
  for (uint32_t i = 0; i < footer.record_count; i++) {
    uint32_t backend = records[i].backend;
    uint32_t encoding = records[i].encoding;
    uint32_t offset = records[i].data_offset;
    uint32_t size = records[i].data_size;

    if (backend != GPU_USL_BACKEND_METAL || encoding != GPU_USL_EMBEDDED_ENCODING_TEXT) {
      continue;
    }

    if ((uint64_t)offset + (uint64_t)size > bytecodeSize - sizeof(GPUUSLEmbeddedBlobFooter)) {
      return 0;
    }

    if (outMetal) *outMetal = (const char *)(data + offset);
    if (outMetalSize) *outMetalSize = size;
    return 1;
  }

  return 0;
}

//GPU_EXPORT
//USLibrary*
//GPUDefaultShaderLibrary(GPUDevice *device) {
//  GPUApi *api;
//
//  if (!(api = gpuActiveGPUApi()))
//    return NULL;
//
//  return NULL;
//}

GPU_EXPORT
GPULibrary*
GPUDefaultLibrary(GPUDevice *device) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->library.defaultLibrary(device);
}

GPU_EXPORT
GPUFunction*
GPUShaderFunction(GPULibrary *lib, const char *name) {
  GPUApi *api;

  if (!(api = gpuActiveGPUApi()))
    return NULL;

  return api->library.newFunction(lib, name);
}

GPU_EXPORT
int
GPUCreateShaderLibrary(GPUDevice *device,
                       const GPUShaderLibraryCreateInfo *info,
                       GPUShaderLibrary **outLibrary) {
  GPUApi *api;
  char *source;

  if (!device || !info || !outLibrary || !info->sourceData) {
    return -1;
  }

  if (!(api = gpuActiveGPUApi()))
    return -2;

  if (info->sourceKind != GPU_SHADER_SOURCE_MSL_TEXT ||
      !api->library.newLibraryWithSource) {
    return -3;
  }

  source = calloc(1, (size_t)info->sourceSize + 1u);
  if (!source) {
    return -4;
  }

  memcpy(source, info->sourceData, (size_t)info->sourceSize);
  source[info->sourceSize] = '\0';

  *outLibrary = api->library.newLibraryWithSource(device, source, info->sourceSize);
  free(source);

  return *outLibrary ? 0 : -5;
}

GPU_EXPORT
int
GPUCreateShaderLibraryFromUSLBytecode(GPUDevice *device,
                                      const void *bytecodeData,
                                      uint64_t bytecodeSize,
                                      GPUShaderLibrary **outLibrary) {
  GPUShaderLibraryCreateInfo info = {0};
  const char *metal = NULL;
  uint64_t metalSize = 0;

  if (!device || !bytecodeData || !outLibrary) {
    return -1;
  }

  if (!gpu_getEmbeddedMetalFromUSLBytecode(bytecodeData, bytecodeSize, &metal, &metalSize)) {
    char *generated = usl_compile_backend_from_bytecode(bytecodeData,
                                                        (size_t)bytecodeSize,
                                                        USL_BACKEND_METAL);
    if (!generated) {
      return -2;
    }
    metal = generated;
    metalSize = (uint64_t)strlen(generated);

    info.label = "compiled-usl-metal";
    info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
    info.sourceData = metal;
    info.sourceSize = metalSize;
    info.sourcePathHint = NULL;
    int rc = GPUCreateShaderLibrary(device, &info, outLibrary);
    usl_free_backend_code(generated);
    return rc;
  }

  info.label = "embedded-usl-metal";
  info.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  info.sourceData = metal;
  info.sourceSize = metalSize;
  info.sourcePathHint = NULL;
  return GPUCreateShaderLibrary(device, &info, outLibrary);
}

GPU_EXPORT
void
GPUDestroyShaderLibrary(GPUShaderLibrary *library) {
  GPUApi *api;

  if (!library)
    return;

  if (!(api = gpuActiveGPUApi()))
    return;

  if (api->library.destroyLibrary) {
    api->library.destroyLibrary(library);
  } else {
    free(library);
  }
}
