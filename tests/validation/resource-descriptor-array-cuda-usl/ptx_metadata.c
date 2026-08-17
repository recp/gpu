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
  const GPUShaderResourceReflection *images;
  const GPUShaderResourceReflection *output;
  const GPUShaderResourceReflection *samplers;
  const GPUShaderResourceReflection *selection;
  const GPUShaderResourceReflection *textures;

  if (!reflection || reflection->resourceCount != 5u ||
      !(textures = find_resource(reflection, 0u)) ||
      !(samplers = find_resource(reflection, 2u)) ||
      !(selection = find_resource(reflection, 4u)) ||
      !(output = find_resource(reflection, 5u)) ||
      !(images = find_resource(reflection, 6u))) {
    return 0;
  }
  return textures->bindingType == GPU_BINDING_SAMPLED_TEXTURE &&
         textures->arrayCount == 2u &&
         textures->sampledTexture.viewType == GPU_TEXTURE_VIEW_2D &&
         textures->sampledTexture.sampleType == GPU_TEXTURE_SAMPLE_TYPE_FLOAT &&
         !textures->sampledTexture.multisampled &&
         samplers->bindingType == GPU_BINDING_SAMPLER &&
         samplers->arrayCount == 2u &&
         samplers->sampler.type == GPU_SAMPLER_BINDING_FILTERING &&
         selection->bindingType == GPU_BINDING_UNIFORM_BUFFER &&
         selection->arrayCount == 1u &&
         selection->buffer.minBindingSize == 4u &&
         selection->buffer.strideBytes == 0u &&
         output->bindingType == GPU_BINDING_STORAGE_BUFFER &&
         output->arrayCount == 1u && output->buffer.minBindingSize == 16u &&
         output->buffer.strideBytes == 16u &&
         images->bindingType == GPU_BINDING_STORAGE_TEXTURE &&
         images->arrayCount == 2u &&
         images->storageTexture.viewType == GPU_TEXTURE_VIEW_2D &&
         images->storageTexture.format == GPU_FORMAT_RGBA32_FLOAT &&
         images->storageTexture.access ==
           GPU_STORAGE_TEXTURE_ACCESS_READ_WRITE;
}

static int
validate_buffer(const GPUShaderPTXParamInfo *param,
                uint32_t                     binding,
                GPUBindingType               bindingType,
                uint32_t                     dataOffset) {
  return param && param->kind == GPUShaderPTXParamBuffer &&
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
validate_surface(const GPUShaderPTXParamInfo *param,
                 uint32_t                     arrayIndex,
                 uint32_t                     dataOffset) {
  return param && param->kind == GPUShaderPTXParamSurface &&
         param->bindingType == GPU_BINDING_STORAGE_TEXTURE &&
         param->groupIndex == 0u && param->binding == 6u &&
         param->arrayIndex == arrayIndex && param->dataOffset == dataOffset &&
         param->samplerGroupIndex == UINT32_MAX &&
         param->samplerBinding == UINT32_MAX &&
         param->samplerArrayIndex == UINT32_MAX &&
         param->staticSamplerId == UINT32_MAX &&
         param->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_sampled(const GPUShaderPTXParamInfo *param,
                 uint32_t                     textureIndex,
                 uint32_t                     samplerIndex,
                 uint32_t                     dataOffset) {
  return param && param->kind == GPUShaderPTXParamSampledTexture &&
         param->bindingType == GPU_BINDING_SAMPLED_TEXTURE &&
         param->groupIndex == 0u && param->binding == 0u &&
         param->arrayIndex == textureIndex &&
         param->samplerGroupIndex == 0u && param->samplerBinding == 2u &&
         param->samplerArrayIndex == samplerIndex &&
         param->staticSamplerId == UINT32_MAX &&
         param->dataOffset == dataOffset &&
         param->metadataFlags == GPUShaderPTXTextureMetadataNone;
}

static int
validate_params(const GPUShaderPTXInfo *info) {
  if (!info || info->entryCount != 2u || info->paramCount != 11u ||
      info->entries[0].paramStart != 0u ||
      info->entries[0].paramCount != 8u ||
      info->entries[0].paramDataSize != 64u ||
      info->entries[1].paramStart != 8u ||
      info->entries[1].paramCount != 3u ||
      info->entries[1].paramDataSize != 24u) {
    return 0;
  }
  return validate_buffer(&info->params[0],
                         4u,
                         GPU_BINDING_UNIFORM_BUFFER,
                         0u) &&
         validate_buffer(&info->params[1],
                         5u,
                         GPU_BINDING_STORAGE_BUFFER,
                         8u) &&
         validate_surface(&info->params[2], 0u, 16u) &&
         validate_surface(&info->params[3], 1u, 24u) &&
         validate_sampled(&info->params[4], 0u, 0u, 32u) &&
         validate_sampled(&info->params[5], 0u, 1u, 40u) &&
         validate_sampled(&info->params[6], 1u, 0u, 48u) &&
         validate_sampled(&info->params[7], 1u, 1u, 56u) &&
         validate_buffer(&info->params[8],
                         5u,
                         GPU_BINDING_STORAGE_BUFFER,
                         0u) &&
         validate_surface(&info->params[9], 0u, 8u) &&
         validate_surface(&info->params[10], 1u, 16u);
}

static int
validate_source(const GPUShaderLibrary *library) {
  const PTXSource *ptx;

  ptx = library ? library->_priv : NULL;
  return ptx && ptx->text &&
         strstr(ptx->text, ".visible .entry descriptor_array_write(") &&
         strstr(ptx->text, ".visible .entry descriptor_array_read(") &&
         strstr(ptx->text, "tex.level.2d.v4.f32.f32") &&
         strstr(ptx->text, "txq.width.b32") &&
         strstr(ptx->text, "suq.width.b32") &&
         strstr(ptx->text, "sust.b.2d.v4.b32") &&
         strstr(ptx->text, "suld.b.2d.v4.b32") &&
         strstr(ptx->text, "selp.b64");
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
            "CUDA PTX resource descriptor-array metadata failed (%d)\n",
            result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = validate_reflection(&library->_reflection) &&
          validate_params(library->_ptxInfo) && validate_source(library);
  if (!valid) {
    fprintf(stderr, "CUDA PTX resource descriptor-array contract mismatch\n");
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
