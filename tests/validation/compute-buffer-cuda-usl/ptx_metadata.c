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
validate_resource(const GPUShaderReflection *reflection,
                  const char                *name,
                  uint32_t                   group,
                  uint32_t                   binding,
                  GPUBindingType             bindingType,
                  uint64_t                   minBindingSize,
                  uint32_t                   strideBytes,
                  bool                       dynamic) {
  const GPUShaderResourceReflection *resource;

  resource = find_resource(reflection, group, binding);
  return resource && strcmp(resource->name, name) == 0 &&
         resource->bindingType == bindingType && resource->arrayCount == 1u &&
         resource->buffer.minBindingSize == minBindingSize &&
         resource->buffer.strideBytes == strideBytes &&
         resource->hasDynamicOffset == dynamic;
}

static int
validate_param(const GPUShaderPTXParamInfo *param,
               uint32_t                     group,
               uint32_t                     binding,
               GPUBindingType               bindingType,
               uint32_t                     dataOffset) {
  return param && param->kind == GPUShaderPTXParamBuffer &&
         param->bindingType == bindingType && param->groupIndex == group &&
         param->binding == binding && param->arrayIndex == 0u &&
         param->dataOffset == dataOffset &&
         param->samplerGroupIndex == UINT32_MAX &&
         param->samplerBinding == UINT32_MAX &&
         param->samplerArrayIndex == UINT32_MAX &&
         param->staticSamplerId == UINT32_MAX &&
         param->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_contract(const GPUShaderLibrary *library) {
  const GPUShaderPTXInfo *info;
  const PTXSource        *ptx;

  if (!library || library->_reflection.resourceCount != 3u ||
      !validate_resource(&library->_reflection,
                         "values",
                         1u,
                         0u,
                         GPU_BINDING_READ_ONLY_STORAGE_BUFFER,
                         4u,
                         4u,
                         false) ||
      !validate_resource(&library->_reflection,
                         "output",
                         1u,
                         1u,
                         GPU_BINDING_STORAGE_BUFFER,
                         4u,
                         4u,
                         false) ||
      !validate_resource(&library->_reflection,
                         "params",
                         0u,
                         0u,
                         GPU_BINDING_UNIFORM_BUFFER,
                         8u,
                         0u,
                         true)) {
    return 0;
  }

  info = library->_ptxInfo;
  if (!info || info->entryCount != 1u || info->paramCount != 3u ||
      info->entries[0].paramStart != 0u ||
      info->entries[0].paramCount != 3u ||
      info->entries[0].paramDataSize != 24u ||
      !validate_param(&info->params[0],
                      1u,
                      0u,
                      GPU_BINDING_READ_ONLY_STORAGE_BUFFER,
                      0u) ||
      !validate_param(&info->params[1],
                      1u,
                      1u,
                      GPU_BINDING_STORAGE_BUFFER,
                      8u) ||
      !validate_param(&info->params[2],
                      0u,
                      0u,
                      GPU_BINDING_UNIFORM_BUFFER,
                      16u)) {
    return 0;
  }

  ptx = library->_priv;
  return ptx && ptx->text && strstr(ptx->text, ".visible .entry saxpy(") &&
         strstr(ptx->text, ".reqntid 256, 1, 1") &&
         strstr(ptx->text, "fma.rn.f32") &&
         strstr(ptx->text, "st.global.f32");
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
  device._api                       = &api;
  device.enabledFeatureMask         = UINT64_C(1) << GPU_FEATURE_COMPUTE;
  device.uslTargetArchitecture      = 89u;

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr, "CUDA PTX compute-buffer metadata failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX compute-buffer contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
