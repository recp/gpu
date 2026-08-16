#include "api/device_internal.h"
#include "api/library_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
validate_entry(const GPUShaderPTXInfo *info,
               uint32_t                entryIndex,
               int                     staticSampler) {
  const GPUShaderPTXEntryInfo *entry;
  const GPUShaderPTXParamInfo *sampled;
  const GPUShaderPTXParamInfo *output;

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
  sampled = output + 1;
  if (sampled->kind != GPUShaderPTXParamSampledTexture ||
      sampled->bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      sampled->groupIndex != 0u || sampled->binding != 3u ||
      sampled->arrayIndex != 0u || sampled->dataOffset != 8u ||
      output->kind != GPUShaderPTXParamBuffer ||
      output->bindingType != GPU_BINDING_STORAGE_BUFFER ||
      output->groupIndex != 2u || output->binding != 2u ||
      output->arrayIndex != 0u || output->dataOffset != 0u) {
    return 0;
  }

  if (staticSampler) {
    return sampled->samplerGroupIndex == UINT32_MAX &&
           sampled->samplerBinding == UINT32_MAX &&
           sampled->samplerArrayIndex == UINT32_MAX &&
           sampled->staticSamplerId == 0u;
  }
  return sampled->samplerGroupIndex == 1u &&
         sampled->samplerBinding == 7u &&
         sampled->samplerArrayIndex == 0u &&
         sampled->staticSamplerId == UINT32_MAX;
}

static int
validate_fetch_entry(const GPUShaderPTXInfo *info, uint32_t entryIndex) {
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
         output->groupIndex == 2u && output->binding == 2u &&
         output->arrayIndex == 0u && output->dataOffset == 0u &&
         texture->kind == GPUShaderPTXParamTexture &&
         texture->bindingType == GPU_BINDING_SAMPLED_TEXTURE &&
         texture->groupIndex == 0u && texture->binding == 3u &&
         texture->arrayIndex == 0u && texture->dataOffset == 8u &&
         texture->samplerGroupIndex == UINT32_MAX &&
         texture->samplerBinding == UINT32_MAX &&
         texture->samplerArrayIndex == UINT32_MAX &&
         texture->staticSamplerId == UINT32_MAX;
}

static int
validate_query_entry(const GPUShaderPTXInfo *info, uint32_t entryIndex) {
  const GPUShaderPTXEntryInfo *entry;
  const GPUShaderPTXParamInfo *metadata;
  const GPUShaderPTXParamInfo *output;

  if (!info || entryIndex >= info->entryCount) {
    return 0;
  }
  entry = &info->entries[entryIndex];
  if (entry->paramCount != 2u || entry->paramDataSize != 24u ||
      entry->paramStart > info->paramCount ||
      entry->paramCount > info->paramCount - entry->paramStart) {
    return 0;
  }

  output   = &info->params[entry->paramStart];
  metadata = output + 1;
  return output->kind == GPUShaderPTXParamBuffer &&
         output->bindingType == GPU_BINDING_STORAGE_BUFFER &&
         output->groupIndex == 2u && output->binding == 2u &&
         output->arrayIndex == 0u && output->dataOffset == 0u &&
         metadata->kind == GPUShaderPTXParamTextureMetadata &&
         metadata->bindingType == GPU_BINDING_SAMPLED_TEXTURE &&
         metadata->groupIndex == 0u && metadata->binding == 3u &&
         metadata->arrayIndex == 0u && metadata->dataOffset == 8u &&
         metadata->metadataFlags ==
           GPUShaderPTXTextureMetadataMipLevelCountBit;
}

static void
print_metadata(const GPUShaderLibrary *library) {
  const GPUShaderPTXInfo *info;

  info = library ? library->_ptxInfo : NULL;
  fprintf(stderr,
          "CUDA PTX metadata: entries=%u params=%u static-samplers=%u\n",
          info ? info->entryCount : 0u,
          info ? info->paramCount : 0u,
          library && library->_staticSamplers
            ? library->_staticSamplers->count : 0u);
  if (!info) {
    return;
  }
  for (uint32_t i = 0u; i < info->entryCount; i++) {
    const GPUShaderPTXEntryInfo *entry;

    entry = &info->entries[i];
    fprintf(stderr,
            "  entry %u: start=%u count=%u bytes=%u\n",
            i,
            entry->paramStart,
            entry->paramCount,
            entry->paramDataSize);
  }
  for (uint32_t i = 0u; i < info->paramCount; i++) {
    const GPUShaderPTXParamInfo *param;

    param = &info->params[i];
    fprintf(stderr,
            "  param %u: kind=%u type=%u group=%u binding=%u array=%u "
            "sampler=%u/%u/%u static=%u offset=%u flags=%u\n",
            i,
            (uint32_t)param->kind,
            (uint32_t)param->bindingType,
            param->groupIndex,
            param->binding,
            param->arrayIndex,
            param->samplerGroupIndex,
            param->samplerBinding,
            param->samplerArrayIndex,
            param->staticSamplerId,
            param->dataOffset,
            param->metadataFlags);
  }
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
    fprintf(stderr, "CUDA PTX metadata creation failed (%d)\n", result);
    GPUDestroyShaderLibrary(library);
    return 0;
  }

  valid = library->_ptxInfo->entryCount == 5u &&
          library->_ptxInfo->paramCount == 10u &&
          library->_staticSamplers &&
          library->_staticSamplers->count == 1u &&
          validate_entry(library->_ptxInfo, 0u, 0) &&
          validate_entry(library->_ptxInfo, 1u, 1) &&
          validate_entry(library->_ptxInfo, 2u, 1) &&
          validate_fetch_entry(library->_ptxInfo, 3u) &&
          validate_query_entry(library->_ptxInfo, 4u);
  if (!valid) {
    fprintf(stderr, "CUDA PTX sampled-texture metadata mismatch\n");
    print_metadata(library);
  }
  GPUDestroyShaderLibrary(library);
  return valid;
}
