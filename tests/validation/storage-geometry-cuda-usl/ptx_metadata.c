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
  static const GPUTextureViewType ViewTypes[5] = {
    GPU_TEXTURE_VIEW_1D,
    GPU_TEXTURE_VIEW_1D_ARRAY,
    GPU_TEXTURE_VIEW_2D_ARRAY,
    GPU_TEXTURE_VIEW_3D,
    GPU_TEXTURE_VIEW_2D
  };
  const GPUShaderResourceReflection *resource;

  if (!reflection || reflection->resourceCount != 6u) {
    return 0;
  }
  for (uint32_t i = 0u; i < 5u; i++) {
    resource = find_resource(reflection, 0u, i);
    if (!resource ||
        resource->bindingType != GPU_BINDING_STORAGE_TEXTURE ||
        resource->storageTexture.viewType != ViewTypes[i] ||
        resource->storageTexture.format != GPU_FORMAT_RGBA32_FLOAT ||
        resource->storageTexture.access !=
          GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE ||
        resource->arrayCount != 1u) {
      return 0;
    }
  }
  resource = find_resource(reflection, 1u, 0u);
  return resource && resource->bindingType == GPU_BINDING_STORAGE_BUFFER &&
         resource->buffer.strideBytes == 16u && resource->arrayCount == 1u;
}

static int
validate_surface(const GPUShaderPTXParamInfo *param,
                 uint32_t                     binding,
                 uint32_t                     dataOffset) {
  return param && param->kind == GPUShaderPTXParamSurface &&
         param->bindingType == GPU_BINDING_STORAGE_TEXTURE &&
         param->groupIndex == 0u && param->binding == binding &&
         param->arrayIndex == 0u && param->dataOffset == dataOffset &&
         param->samplerGroupIndex == UINT32_MAX &&
         param->samplerBinding == UINT32_MAX &&
         param->samplerArrayIndex == UINT32_MAX &&
         param->staticSamplerId == UINT32_MAX &&
         param->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_params(const GPUShaderPTXInfo *info) {
  const GPUShaderPTXEntryInfo *mutate;
  const GPUShaderPTXEntryInfo *read;

  if (!info || info->entryCount != 2u || info->paramCount != 11u) {
    return 0;
  }
  mutate = &info->entries[0];
  read   = &info->entries[1];
  if (mutate->paramStart != 0u || mutate->paramCount != 5u ||
      mutate->paramDataSize != 40u || read->paramStart != 5u ||
      read->paramCount != 6u || read->paramDataSize != 48u) {
    return 0;
  }
  for (uint32_t i = 0u; i < 5u; i++) {
    if (!validate_surface(&info->params[mutate->paramStart + i], i, i * 8u) ||
        !validate_surface(&info->params[read->paramStart + i], i, i * 8u)) {
      return 0;
    }
  }
  return info->params[read->paramStart + 5u].kind ==
           GPUShaderPTXParamBuffer &&
         info->params[read->paramStart + 5u].bindingType ==
           GPU_BINDING_STORAGE_BUFFER &&
         info->params[read->paramStart + 5u].groupIndex == 1u &&
         info->params[read->paramStart + 5u].binding == 0u &&
         info->params[read->paramStart + 5u].arrayIndex == 0u &&
         info->params[read->paramStart + 5u].dataOffset == 40u;
}

static int
validate_source(const GPUShaderLibrary *library) {
  const PTXSource *ptx;

  ptx = library ? library->_priv : NULL;
  return ptx && ptx->text &&
         strstr(ptx->text, "suld.b.1d.v4.b32") &&
         strstr(ptx->text, "suld.b.a1d.v4.b32") &&
         strstr(ptx->text, "suld.b.a2d.v4.b32") &&
         strstr(ptx->text, "suld.b.3d.v4.b32") &&
         strstr(ptx->text, "suld.b.2d.v4.b32") &&
         strstr(ptx->text, "sust.b.1d.v4.b32") &&
         strstr(ptx->text, "sust.b.a1d.v4.b32") &&
         strstr(ptx->text, "sust.b.a2d.v4.b32") &&
         strstr(ptx->text, "sust.b.3d.v4.b32") &&
         strstr(ptx->text, "sust.b.2d.v4.b32") &&
         strstr(ptx->text, "[%rd0, {32}]") &&
         !strstr(ptx->text, "mul.lo.");
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
    fprintf(stderr, "CUDA PTX storage-geometry metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_reflection(&library->_reflection) &&
          validate_params(library->_ptxInfo) && validate_source(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX storage-geometry contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
