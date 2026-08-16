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

static GPUShaderLibrary *
new_library(GPUDevice *device, const char *source, uint64_t sourceSize) {
  (void)device;
  if (!source || sourceSize == 0u) {
    return NULL;
  }
  return calloc(1u, sizeof(GPUShaderLibrary));
}

static void
destroy_library(GPUShaderLibrary *library) {
  free(library);
}

static const GPUShaderResourceReflection *
find_resource(const GPUShaderReflection *reflection,
              uint32_t                   group,
              uint32_t                   binding) {
  if (!reflection) {
    return NULL;
  }
  for (uint32_t i = 0u; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;

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
  if (entry->paramCount != 2u || entry->paramDataSize != 16u ||
      entry->paramStart > info->paramCount ||
      entry->paramCount > info->paramCount - entry->paramStart) {
    return 0;
  }

  output  = &info->params[entry->paramStart];
  texture = output + 1;
  return output->kind == GPUShaderPTXParamBuffer &&
         output->bindingType == GPU_BINDING_STORAGE_BUFFER &&
         output->groupIndex == 1u && output->binding == outputBinding &&
         output->arrayIndex == 0u && output->dataOffset == 0u &&
         texture->kind == GPUShaderPTXParamTexture &&
         texture->bindingType == GPU_BINDING_SAMPLED_TEXTURE &&
         texture->groupIndex == 0u && texture->binding == textureBinding &&
         texture->arrayIndex == 0u && texture->dataOffset == 8u &&
         texture->samplerGroupIndex == UINT32_MAX &&
         texture->samplerBinding == UINT32_MAX &&
         texture->samplerArrayIndex == UINT32_MAX &&
         texture->staticSamplerId == UINT32_MAX &&
         texture->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_reflection(const GPUShaderReflection *reflection) {
  static const GPUTextureSampleType sampleTypes[5] = {
    GPU_TEXTURE_SAMPLE_TYPE_FLOAT,
    GPU_TEXTURE_SAMPLE_TYPE_FLOAT,
    GPU_TEXTURE_SAMPLE_TYPE_FLOAT,
    GPU_TEXTURE_SAMPLE_TYPE_UINT,
    GPU_TEXTURE_SAMPLE_TYPE_SINT
  };
  static const uint32_t strides[3] = {16u, 16u, 16u};

  if (!reflection || reflection->resourceCount != SampleResourceCount) {
    return 0;
  }
  for (uint32_t i = 0u; i < 5u; i++) {
    const GPUShaderResourceReflection *texture;

    texture = find_resource(reflection, 0u, i);
    if (!texture || texture->bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
        texture->sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
        texture->sampledTexture.sampleType != sampleTypes[i] ||
        texture->sampledTexture.multisampled || texture->arrayCount != 1u) {
      return 0;
    }
  }
  for (uint32_t i = 0u; i < 3u; i++) {
    const GPUShaderResourceReflection *buffer;

    buffer = find_resource(reflection, 1u, i);
    if (!buffer || buffer->bindingType != GPU_BINDING_STORAGE_BUFFER ||
        buffer->buffer.strideBytes != strides[i] || buffer->arrayCount != 1u) {
      return 0;
    }
  }
  return 1;
}

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize) {
  GPUShaderLibrary *library;
  GPUDevice          device;
  GPUApi             api;
  GPUResult          result;
  int                valid;

  memset(&device, 0, sizeof(device));
  memset(&api, 0, sizeof(api));
  api.backend                       = GPU_BACKEND_CUDA;
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

  valid = library->_ptxInfo->entryCount == SampleEntryCount &&
          library->_ptxInfo->paramCount == SampleParamCount &&
          validate_entry(library->_ptxInfo, 0u, 0u, 0u) &&
          validate_entry(library->_ptxInfo, 1u, 1u, 0u) &&
          validate_entry(library->_ptxInfo, 2u, 2u, 0u) &&
          validate_entry(library->_ptxInfo, 3u, 3u, 1u) &&
          validate_entry(library->_ptxInfo, 4u, 4u, 2u) &&
          validate_reflection(&library->_reflection);
  if (!valid) {
    fprintf(stderr, "CUDA PTX sampled-format metadata mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
