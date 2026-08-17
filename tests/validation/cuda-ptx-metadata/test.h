#ifndef gpu_tests_cuda_ptx_metadata_test_h
#define gpu_tests_cuda_ptx_metadata_test_h

#include "api/device_internal.h"
#include "api/library_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct PTXSource {
  char *text;
} PTXSource;

static GPUShaderLibrary *
ptx_new_library(GPUDevice *device, const char *source, uint64_t sourceSize) {
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
ptx_destroy_library(GPUShaderLibrary *library) {
  PTXSource *ptx;

  ptx = library ? library->_priv : NULL;
  if (ptx) {
    free(ptx->text);
    free(ptx);
  }
  free(library);
}

static void
ptx_init_device(GPUDevice   *device,
                GPUAdapter  *adapter,
                GPUInstance *instance,
                GPUApi      *api,
                uint64_t     featureMask,
                uint32_t     architecture) {
  memset(device, 0, sizeof(*device));
  memset(adapter, 0, sizeof(*adapter));
  memset(instance, 0, sizeof(*instance));
  memset(api, 0, sizeof(*api));

  api->backend                       = GPU_BACKEND_CUDA;
  api->library.newLibraryWithSource = ptx_new_library;
  api->library.destroyLibrary       = ptx_destroy_library;
  instance->_api                    = api;
  adapter->inst                     = instance;
  device->_api                      = api;
  device->adapter                   = adapter;
  device->enabledFeatureMask        = featureMask;
  device->uslTargetArchitecture     = architecture;
}

static inline const GPUShaderResourceReflection *
ptx_find_resource(const GPUShaderReflection *reflection,
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

static inline int
ptx_validate_buffer_resource(const GPUShaderReflection *reflection,
                             const char                *name,
                             uint32_t                   group,
                             uint32_t                   binding,
                             GPUBindingType             bindingType,
                             uint64_t                   minBindingSize,
                             uint32_t                   strideBytes) {
  const GPUShaderResourceReflection *resource;

  resource = ptx_find_resource(reflection, group, binding);
  return resource && strcmp(resource->name, name) == 0 &&
         resource->bindingType == bindingType && resource->arrayCount == 1u &&
         resource->buffer.minBindingSize == minBindingSize &&
         resource->buffer.strideBytes == strideBytes &&
         !resource->hasDynamicOffset;
}

static inline int
ptx_validate_buffer_param(const GPUShaderPTXParamInfo *param,
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

static const char *
ptx_source(const GPUShaderLibrary *library) {
  const PTXSource *ptx;

  ptx = library ? library->_priv : NULL;
  return ptx ? ptx->text : NULL;
}

#endif /* gpu_tests_cuda_ptx_metadata_test_h */
