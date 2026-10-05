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

#include "api/device_internal.h"
#include "api/library_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  SampleEntryCount    = 5u,
  SampleParamCount    = 10u,
  SampleResourceCount = 8u
};

static const GPUTextureSampleType sample_types[5] = {
  GPU_TEXTURE_SAMPLE_TYPE_FLOAT,
  GPU_TEXTURE_SAMPLE_TYPE_FLOAT,
  GPU_TEXTURE_SAMPLE_TYPE_FLOAT,
  GPU_TEXTURE_SAMPLE_TYPE_UINT,
  GPU_TEXTURE_SAMPLE_TYPE_SINT
};

static const uint32_t buffer_strides[3] = {16u, 16u, 16u};

static GPUShaderLibrary*
new_library(GPUDevice  *device,
            const char *source,
            uint64_t    sourceSize,
            uint32_t    compileFlags) {
  (void)device;
  (void)compileFlags;

  if (!source || sourceSize == 0u) {
    return NULL;
  }

  return calloc(1u, sizeof(GPUShaderLibrary));
}

static void
destroy_library(GPUShaderLibrary *library) {
  free(library);
}

static const GPUShaderResourceReflection*
find_resource(const GPUShaderReflection *reflection,
              uint32_t                   group,
              uint32_t                   binding) {
  const GPUShaderResourceReflection *resource;
  uint32_t                           i;

  if (!reflection) {
    return NULL;
  }

  for (i = 0u; i < reflection->resourceCount; i++) {

    resource = &reflection->pResources[i];

    if (resource->groupIndex == group && resource->binding == binding) {
      return resource;
    }
  }

  return NULL;
}

static int
validate_entry(const GPUShaderPTXInfo *info,
               uint32_t                entryIndex,
               uint32_t                textureBinding,
               uint32_t                outputBinding) {
  const GPUShaderPTXEntryInfo *entry;
  const GPUShaderPTXParamInfo *output;
  const GPUShaderPTXParamInfo *texture;

  if (!info || entryIndex >= info->entryCount) {
    return 0;
  }

  entry = &info->entries[entryIndex];

  if (entry->paramCount != 2u || entry->paramDataSize != 16u
      || entry->paramStart > info->paramCount
      || entry->paramCount > info->paramCount - entry->paramStart) {
    return 0;
  }

  output  = &info->params[entry->paramStart];
  texture = output + 1;
  return output->kind == GPUShaderPTXParamBuffer
         && output->bindingType == GPU_BINDING_STORAGE_BUFFER
         && output->groupIndex == 1u && output->binding == outputBinding
         && output->arrayIndex == 0u && output->dataOffset == 0u
         && texture->kind == GPUShaderPTXParamTexture
         && texture->bindingType == GPU_BINDING_SAMPLED_TEXTURE
         && texture->groupIndex == 0u && texture->binding == textureBinding
         && texture->arrayIndex == 0u && texture->dataOffset == 8u
         && texture->samplerGroupIndex == UINT32_MAX
         && texture->samplerBinding == UINT32_MAX
         && texture->samplerArrayIndex == UINT32_MAX
         && texture->staticSamplerId == UINT32_MAX
         && texture->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_reflection(const GPUShaderReflection *reflection) {
  const GPUShaderResourceReflection *texture;
  const GPUShaderResourceReflection *buffer;
  uint32_t                           textureIndex;
  uint32_t                           bufferIndex;

  if (!reflection || reflection->resourceCount != SampleResourceCount) {
    return 0;
  }

  for (textureIndex = 0u; textureIndex < 5u; textureIndex++) {

    texture = find_resource(reflection, 0u, textureIndex);

    if (!texture || texture->bindingType != GPU_BINDING_SAMPLED_TEXTURE
        || texture->sampledTexture.viewType != GPU_TEXTURE_VIEW_2D
        || texture->sampledTexture.sampleType != sample_types[textureIndex]
        || texture->sampledTexture.multisampled || texture->arrayCount != 1u) {
      return 0;
    }
  }

  for (bufferIndex = 0u; bufferIndex < 3u; bufferIndex++) {

    buffer = find_resource(reflection, 1u, bufferIndex);

    if (!buffer || buffer->bindingType != GPU_BINDING_STORAGE_BUFFER
        || buffer->buffer.strideBytes != buffer_strides[bufferIndex] || buffer->arrayCount != 1u) {
      return 0;
    }
  }

  return 1;
}

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize) {
  GPUDevice         device;
  GPUApi            api;
  GPUShaderLibrary *library;
  GPUResult         result;
  int               valid;

  memset(&device, 0, sizeof(device));
  memset(&api, 0, sizeof(api));
  api.backend                      = GPU_BACKEND_CUDA;
  api.library.newLibraryWithSource = new_library;
  api.library.destroyLibrary       = destroy_library;
  device._api                      = &api;
  device.enabledFeatureMask        = UINT64_C(1) << GPU_FEATURE_COMPUTE;
  device.uslTargetArchitecture     = 89u;

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);

  if (result != GPU_OK || !library || !library->_ptxInfo) {
    fprintf(stderr,
            "CUDA PTX sampled-format metadata creation failed (%d)\n",
            result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = library->_ptxInfo->entryCount == SampleEntryCount
          && library->_ptxInfo->paramCount == SampleParamCount
          && validate_entry(library->_ptxInfo, 0u, 0u, 0u)
          && validate_entry(library->_ptxInfo, 1u, 1u, 0u)
          && validate_entry(library->_ptxInfo, 2u, 2u, 0u)
          && validate_entry(library->_ptxInfo, 3u, 3u, 1u)
          && validate_entry(library->_ptxInfo, 4u, 4u, 2u)
          && validate_reflection(&library->_reflection);

  if (!valid) {
    fprintf(stderr, "CUDA PTX sampled-format metadata mismatch\n");
  }

  GPUDestroyShaderLibrary(library);

  return valid;
}
