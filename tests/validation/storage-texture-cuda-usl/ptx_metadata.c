#include "api/device_internal.h"
#include "api/library_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  StorageEntryCount = 10u,
  StorageParamCount = 15u
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

static int
validate_param(const GPUShaderPTXParamInfo *param,
               GPUShaderPTXParamKind        kind,
               GPUBindingType               bindingType,
               uint32_t                     binding,
               uint32_t                     dataOffset) {
  return param && param->kind == kind &&
         param->bindingType == bindingType && param->groupIndex == 0u &&
         param->binding == binding && param->arrayIndex == 0u &&
         param->dataOffset == dataOffset &&
         param->samplerGroupIndex == UINT32_MAX &&
         param->samplerBinding == UINT32_MAX &&
         param->samplerArrayIndex == UINT32_MAX &&
         param->staticSamplerId == UINT32_MAX &&
         param->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_entry(const GPUShaderPTXInfo *info,
               uint32_t                entryIndex,
               uint32_t                imageBinding,
               uint32_t                bufferBinding,
               int                     hasBuffer) {
  const GPUShaderPTXEntryInfo *entry;
  const GPUShaderPTXParamInfo *params;
  uint32_t                     expectedCount;

  if (!info || entryIndex >= info->entryCount) {
    return 0;
  }
  entry         = &info->entries[entryIndex];
  expectedCount = hasBuffer ? 2u : 1u;
  if (entry->paramCount != expectedCount ||
      entry->paramDataSize != expectedCount * 8u ||
      entry->paramStart > info->paramCount ||
      entry->paramCount > info->paramCount - entry->paramStart) {
    return 0;
  }

  params = &info->params[entry->paramStart];
  if (!validate_param(&params[0],
                      GPUShaderPTXParamSurface,
                      GPU_BINDING_STORAGE_TEXTURE,
                      imageBinding,
                      0u)) {
    return 0;
  }
  return !hasBuffer ||
         validate_param(&params[1],
                        GPUShaderPTXParamBuffer,
                        GPU_BINDING_STORAGE_BUFFER,
                        bufferBinding,
                        8u);
}

static const GPUShaderResourceReflection *
find_resource(const GPUShaderReflection *reflection, uint32_t binding) {
  if (!reflection) {
    return NULL;
  }
  for (uint32_t i = 0u; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;

    resource = &reflection->pResources[i];
    if (resource->groupIndex == 0u && resource->binding == binding) {
      return resource;
    }
  }
  return NULL;
}

static int
validate_reflection(const GPUShaderReflection *reflection) {
  static const GPUFormat formats[5] = {
    GPU_FORMAT_RGBA32_FLOAT,
    GPU_FORMAT_RGBA8_UNORM,
    GPU_FORMAT_RGBA8_SNORM,
    GPU_FORMAT_RGBA8_UINT,
    GPU_FORMAT_RGBA8_SINT
  };

  if (!reflection || reflection->resourceCount != 10u) {
    return 0;
  }
  for (uint32_t i = 0u; i < 5u; i++) {
    const GPUShaderResourceReflection *buffer;
    const GPUShaderResourceReflection *image;

    image  = find_resource(reflection, i * 2u);
    buffer = find_resource(reflection, i * 2u + 1u);
    if (!image || image->bindingType != GPU_BINDING_STORAGE_TEXTURE ||
        image->storageTexture.viewType != GPU_TEXTURE_VIEW_2D ||
        image->storageTexture.format != formats[i] ||
        image->storageTexture.access !=
          GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE ||
        !buffer || buffer->bindingType != GPU_BINDING_STORAGE_BUFFER ||
        buffer->buffer.strideBytes != 16u) {
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
    fprintf(stderr, "CUDA PTX storage metadata creation failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = library->_ptxInfo->entryCount == StorageEntryCount &&
          library->_ptxInfo->paramCount == StorageParamCount &&
          validate_entry(library->_ptxInfo, 0u, 0u, 0u, 0) &&
          validate_entry(library->_ptxInfo, 1u, 0u, 1u, 1) &&
          validate_entry(library->_ptxInfo, 2u, 2u, 0u, 0) &&
          validate_entry(library->_ptxInfo, 3u, 2u, 3u, 1) &&
          validate_entry(library->_ptxInfo, 4u, 4u, 0u, 0) &&
          validate_entry(library->_ptxInfo, 5u, 4u, 5u, 1) &&
          validate_entry(library->_ptxInfo, 6u, 6u, 0u, 0) &&
          validate_entry(library->_ptxInfo, 7u, 6u, 7u, 1) &&
          validate_entry(library->_ptxInfo, 8u, 8u, 0u, 0) &&
          validate_entry(library->_ptxInfo, 9u, 8u, 9u, 1) &&
          validate_reflection(&library->_reflection);
  if (!valid) {
    fprintf(stderr, "CUDA PTX storage metadata mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
