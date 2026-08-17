#include "api/device_internal.h"
#include "api/library_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GPU_PTX_EXPECT_DYNAMIC
#  error "GPU_PTX_EXPECT_DYNAMIC must be defined"
#endif

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
find_resource(const GPUShaderReflection *reflection, uint32_t binding) {
  if (!reflection) {
    return NULL;
  }
  for (uint32_t i = 0u; i < reflection->resourceCount; i++) {
    const GPUShaderResourceReflection *resource;

    resource = &reflection->pResources[i];
    if (resource->groupIndex == 1u && resource->binding == binding) {
      return resource;
    }
  }
  return NULL;
}

static int
validate_param(const GPUShaderPTXParamInfo *param,
               uint32_t                     binding,
               uint32_t                     arrayIndex,
               GPUBindingType               bindingType,
               uint32_t                     dataOffset) {
  return param && param->kind == GPUShaderPTXParamBuffer &&
         param->bindingType == bindingType && param->groupIndex == 1u &&
         param->binding == binding && param->arrayIndex == arrayIndex &&
         param->dataOffset == dataOffset &&
         param->samplerGroupIndex == UINT32_MAX &&
         param->samplerBinding == UINT32_MAX &&
         param->samplerArrayIndex == UINT32_MAX &&
         param->staticSamplerId == UINT32_MAX &&
         param->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_contract(const GPUShaderLibrary *library) {
  const GPUShaderResourceReflection *buffers;
  const GPUShaderResourceReflection *output;
  const GPUShaderResourceReflection *selection;
  const GPUShaderPTXInfo             *info;
  const PTXSource                    *ptx;
  GPUBindingType                      bufferType;
  bool                                dynamic;

  if (!library || library->_reflection.resourceCount != 3u ||
      !(buffers = find_resource(&library->_reflection, 0u)) ||
      !(selection = find_resource(&library->_reflection, 2u)) ||
      !(output = find_resource(&library->_reflection, 3u))) {
    return 0;
  }
  dynamic    = buffers->hasDynamicOffset;
  bufferType = dynamic ? GPU_BINDING_STORAGE_BUFFER
                       : GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  if (dynamic != (GPU_PTX_EXPECT_DYNAMIC != 0) ||
      strcmp(buffers->name, "buffers") != 0 ||
      buffers->bindingType != bufferType || buffers->arrayCount != 2u ||
      buffers->buffer.minBindingSize != 16u ||
      buffers->buffer.strideBytes != 16u ||
      strcmp(selection->name, "selection") != 0 ||
      selection->bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      selection->arrayCount != 1u || selection->buffer.minBindingSize != 4u ||
      selection->buffer.strideBytes != 0u ||
      strcmp(output->name, "output") != 0 ||
      output->bindingType != GPU_BINDING_STORAGE_BUFFER ||
      output->arrayCount != 1u || output->buffer.minBindingSize != 16u ||
      output->buffer.strideBytes != 16u) {
    return 0;
  }

  info = library->_ptxInfo;
  if (!info || info->entryCount != 1u || info->paramCount != 4u ||
      info->entries[0].paramStart != 0u ||
      info->entries[0].paramCount != 4u ||
      info->entries[0].paramDataSize != 32u ||
      !validate_param(&info->params[0], 0u, 0u, bufferType, 0u) ||
      !validate_param(&info->params[1], 0u, 1u, bufferType, 8u) ||
      !validate_param(&info->params[2],
                      2u,
                      0u,
                      GPU_BINDING_UNIFORM_BUFFER,
                      16u) ||
      !validate_param(&info->params[3],
                      3u,
                      0u,
                      GPU_BINDING_STORAGE_BUFFER,
                      24u)) {
    return 0;
  }

  ptx = library->_priv;
  return ptx && ptx->text &&
         strstr(ptx->text, ".visible .entry buffer_descriptor_array_cs(") &&
         strstr(ptx->text, "selp.b64") &&
         strstr(ptx->text, "ld.global.v4.f32") &&
         strstr(ptx->text, "st.global.v4.f32");
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
  device._api                         = &api;
  device.enabledFeatureMask           =
    (UINT64_C(1) << GPU_FEATURE_COMPUTE) |
    (UINT64_C(1) << GPU_FEATURE_DESCRIPTOR_INDEXING);
  device.uslTargetArchitecture        = 89u;
  device.uslBoundedDescriptorIndexing = true;

  library = NULL;
  result  = GPUCreateShaderLibraryFromUSL(&device,
                                          artifact,
                                          artifactSize,
                                          &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr,
            "CUDA PTX buffer descriptor-array metadata failed (%d)\n",
            result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_contract(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX buffer descriptor-array contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
