#include "api/device_internal.h"
#include "api/library_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PTXSource {
  char *text;
} PTXSource;

static GPUShaderLibrary *
new_library(GPUDevice *device, const char *source, uint64_t sourceSize) {
  GPUShaderLibrary *library;
  PTXSource        *ptx;

  (void)device;
  if (!source || sourceSize == 0u || sourceSize > SIZE_MAX - 1u) {
    return NULL;
  }
  library = calloc(1u, sizeof(*library));
  ptx     = calloc(1u, sizeof(*ptx));
  if (!library || !ptx || !(ptx->text = malloc((size_t)sourceSize + 1u))) {
    free(ptx);
    free(library);
    return NULL;
  }
  memcpy(ptx->text, source, (size_t)sourceSize);
  ptx->text[sourceSize] = '\0';
  library->_priv        = ptx;
  return library;
}

static void
destroy_library(GPUShaderLibrary *library) {
  PTXSource *ptx;

  ptx = library ? library->_priv : NULL;
  if (ptx) {
    free(ptx->text);
    free(ptx);
  }
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
validate_reflection(const GPUShaderReflection *reflection) {
  static const GPUTextureViewType ViewTypes[7] = {
    GPU_TEXTURE_VIEW_1D,
    GPU_TEXTURE_VIEW_1D_ARRAY,
    GPU_TEXTURE_VIEW_2D_ARRAY,
    GPU_TEXTURE_VIEW_3D,
    GPU_TEXTURE_VIEW_2D,
    GPU_TEXTURE_VIEW_2D,
    GPU_TEXTURE_VIEW_2D_ARRAY
  };
  const GPUShaderResourceReflection *resource;

  if (!reflection || reflection->resourceCount != 8u) {
    return 0;
  }
  for (uint32_t i = 0u; i < 7u; i++) {
    resource = find_resource(reflection, 0u, i);
    if (!resource ||
        resource->bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
        resource->sampledTexture.viewType != ViewTypes[i] ||
        resource->sampledTexture.sampleType != GPU_TEXTURE_SAMPLE_TYPE_FLOAT ||
        resource->sampledTexture.multisampled || resource->arrayCount != 1u) {
      return 0;
    }
  }
  resource = find_resource(reflection, 1u, 0u);
  return resource && resource->bindingType == GPU_BINDING_STORAGE_BUFFER &&
         resource->buffer.strideBytes == 16u && resource->arrayCount == 1u;
}

static int
validate_params(const GPUShaderPTXInfo *info) {
  uint32_t bindingMask;
  uint32_t outputCount;

  if (!info || info->entryCount != 1u || info->paramCount != 8u ||
      info->entries[0].paramStart != 0u ||
      info->entries[0].paramCount != 8u ||
      info->entries[0].paramDataSize != 64u) {
    return 0;
  }

  bindingMask = 0u;
  outputCount = 0u;
  for (uint32_t i = 0u; i < info->paramCount; i++) {
    const GPUShaderPTXParamInfo *param;

    param = &info->params[i];
    if (param->kind == GPUShaderPTXParamBuffer) {
      if (param->bindingType != GPU_BINDING_STORAGE_BUFFER ||
          param->groupIndex != 1u || param->binding != 0u ||
          param->arrayIndex != 0u) {
        return 0;
      }
      outputCount++;
      continue;
    }
    if (param->kind != GPUShaderPTXParamTexture ||
        param->bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
        param->groupIndex != 0u || param->binding >= 7u ||
        param->arrayIndex != 0u ||
        param->samplerGroupIndex != UINT32_MAX ||
        param->samplerBinding != UINT32_MAX ||
        param->samplerArrayIndex != UINT32_MAX ||
        param->staticSamplerId != UINT32_MAX ||
        param->metadataFlags != GPUShaderPTXTextureMetadataNone) {
      return 0;
    }
    bindingMask |= 1u << param->binding;
  }
  return outputCount == 1u && bindingMask == 0x7fu;
}

static int
validate_source(const GPUShaderLibrary *library) {
  const PTXSource *ptx;

  ptx = library ? library->_priv : NULL;
  return ptx && ptx->text &&
         strstr(ptx->text, "tex.base.1d.v4.f32.s32") &&
         strstr(ptx->text, "tex.base.a1d.v4.f32.s32") &&
         strstr(ptx->text, "tex.base.a2d.v4.f32.s32") &&
         strstr(ptx->text, "tex.base.3d.v4.f32.s32") &&
         strstr(ptx->text, "tex.level.2d.v4.f32.s32") &&
         strstr(ptx->text, "tex.base.2d.v4.f32.s32");
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
    fprintf(stderr, "CUDA PTX texture-geometry metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_reflection(&library->_reflection) &&
          validate_params(library->_ptxInfo) && validate_source(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX texture-geometry contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
