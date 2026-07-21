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

#include "../common.h"
#include "../../../api/sampler_internal.h"

#include <us/compiler.h>

static VkFilter
vk__samplerFilter(GPUFilter filter) {
  return filter == GPU_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
}

static VkSamplerMipmapMode
vk__mipFilter(GPUMipFilter filter) {
  return filter == GPU_MIP_FILTER_LINEAR
           ? VK_SAMPLER_MIPMAP_MODE_LINEAR
           : VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

static VkSamplerAddressMode
vk__addressMode(GPUAddressMode mode) {
  switch (mode) {
    case GPU_ADDRESS_MODE_REPEAT:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case GPU_ADDRESS_MODE_MIRRORED_REPEAT:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case GPU_ADDRESS_MODE_CLAMP_TO_EDGE:
    default:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

static VkCompareOp
vk__gpuCompareOp(GPUCompareOp op) {
  static const VkCompareOp operations[] = {
    [GPU_COMPARE_NEVER]         = VK_COMPARE_OP_NEVER,
    [GPU_COMPARE_LESS]          = VK_COMPARE_OP_LESS,
    [GPU_COMPARE_EQUAL]         = VK_COMPARE_OP_EQUAL,
    [GPU_COMPARE_LESS_EQUAL]    = VK_COMPARE_OP_LESS_OR_EQUAL,
    [GPU_COMPARE_GREATER]       = VK_COMPARE_OP_GREATER,
    [GPU_COMPARE_NOT_EQUAL]     = VK_COMPARE_OP_NOT_EQUAL,
    [GPU_COMPARE_GREATER_EQUAL] = VK_COMPARE_OP_GREATER_OR_EQUAL,
    [GPU_COMPARE_ALWAYS]        = VK_COMPARE_OP_ALWAYS
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : VK_COMPARE_OP_NEVER;
}

GPU_HIDE
void
vk_fillSamplerInfo(const GPUSamplerDesc *desc, VkSamplerCreateInfo *outInfo) {
  memset(outInfo, 0, sizeof(*outInfo));
  outInfo->sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  outInfo->magFilter    = vk__samplerFilter(desc->magFilter);
  outInfo->minFilter    = vk__samplerFilter(desc->minFilter);
  outInfo->mipmapMode   = vk__mipFilter(desc->mipFilter);
  outInfo->addressModeU = vk__addressMode(desc->addressU);
  outInfo->addressModeV = vk__addressMode(desc->addressV);
  outInfo->addressModeW = vk__addressMode(desc->addressW);
  outInfo->compareEnable = desc->compareEnable ? VK_TRUE : VK_FALSE;
  outInfo->compareOp     = vk__gpuCompareOp(desc->compare);
  outInfo->maxLod       = VK_LOD_CLAMP_NONE;
}

static VkCompareOp
vk__compareOp(uint32_t compare) {
  switch (compare) {
    case USL_RUNTIME_COMPARE_LESS:
      return VK_COMPARE_OP_LESS;
    case USL_RUNTIME_COMPARE_EQUAL:
      return VK_COMPARE_OP_EQUAL;
    case USL_RUNTIME_COMPARE_LESS_EQUAL:
      return VK_COMPARE_OP_LESS_OR_EQUAL;
    case USL_RUNTIME_COMPARE_GREATER:
      return VK_COMPARE_OP_GREATER;
    case USL_RUNTIME_COMPARE_NOT_EQUAL:
      return VK_COMPARE_OP_NOT_EQUAL;
    case USL_RUNTIME_COMPARE_GREATER_EQUAL:
      return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case USL_RUNTIME_COMPARE_ALWAYS:
      return VK_COMPARE_OP_ALWAYS;
    case USL_RUNTIME_COMPARE_NEVER:
    default:
      return VK_COMPARE_OP_NEVER;
  }
}

static VkSamplerAddressMode
vk__staticSamplerAddressMode(uint32_t mode) {
  switch (mode) {
    case USL_RUNTIME_ADDRESS_REPEAT:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case USL_RUNTIME_ADDRESS_MIRRORED_REPEAT:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case USL_RUNTIME_ADDRESS_CLAMP_TO_ZERO:
    case USL_RUNTIME_ADDRESS_CLAMP_TO_BORDER:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case USL_RUNTIME_ADDRESS_CLAMP_TO_EDGE:
    default:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

GPU_HIDE
void
vk_fillStaticSamplerInfo(const GPUStaticSamplerDesc *desc,
                         VkSamplerCreateInfo        *outInfo) {
  VkFilter            minFilter;
  VkFilter            magFilter;
  VkSamplerMipmapMode  mipFilter;
  VkSamplerAddressMode addressMode;

  minFilter   = desc->minFilter == USL_RUNTIME_FILTER_LINEAR
                  ? VK_FILTER_LINEAR
                  : VK_FILTER_NEAREST;
  magFilter   = desc->magFilter == USL_RUNTIME_FILTER_LINEAR
                  ? VK_FILTER_LINEAR
                  : VK_FILTER_NEAREST;
  mipFilter   = desc->mipFilter == USL_RUNTIME_FILTER_LINEAR
                  ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                  : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  addressMode = vk__staticSamplerAddressMode(desc->addressMode);

  memset(outInfo, 0, sizeof(*outInfo));
  outInfo->sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  outInfo->magFilter               = magFilter;
  outInfo->minFilter               = minFilter;
  outInfo->mipmapMode              = mipFilter;
  outInfo->addressModeU            = addressMode;
  outInfo->addressModeV            = addressMode;
  outInfo->addressModeW            = addressMode;
  outInfo->compareEnable           = desc->hasCompare ? VK_TRUE : VK_FALSE;
  outInfo->compareOp               = vk__compareOp(desc->compareFunc);
  outInfo->unnormalizedCoordinates =
    desc->coordSpace == USL_RUNTIME_COORD_PIXEL ? VK_TRUE : VK_FALSE;
  outInfo->borderColor             =
    VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  outInfo->maxLod                  = outInfo->unnormalizedCoordinates
                                      ? 0.0f
                                      : VK_LOD_CLAMP_NONE;
}

static GPUResult
vk__createSampler(GPUDevice *device,
                  const VkSamplerCreateInfo *info,
                  GPUSampler **outSampler) {
  GPUDeviceVk  *deviceVk;
  GPUSampler   *sampler;
  GPUSamplerVk *native;

  if (!device || !device->_priv || !info || !outSampler) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSampler = NULL;
  deviceVk    = device->_priv;
  sampler     = calloc(1, sizeof(*sampler) + sizeof(*native));
  if (!sampler) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  native         = (GPUSamplerVk *)(sampler + 1);
  native->device = deviceVk->device;
  if (vkCreateSampler(native->device,
                      info,
                      NULL,
                      &native->sampler) != VK_SUCCESS) {
    free(sampler);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  sampler->_priv = native;
  *outSampler    = sampler;
  return GPU_OK;
}

GPU_HIDE
GPUResult
vk_createSampler(GPUApi                    * __restrict api,
                 GPUDevice                 * __restrict device,
                 const GPUSamplerCreateInfo *info,
                 bool                       staticIfSupported,
                 GPUSampler               **outSampler) {
  VkSamplerCreateInfo samplerInfo = {0};

  GPU__UNUSED(api);
  GPU__UNUSED(staticIfSupported);
  if (!info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  vk_fillSamplerInfo(&info->desc, &samplerInfo);
  return vk__createSampler(device, &samplerInfo, outSampler);
}

GPU_HIDE
void
vk_destroySampler(GPUSampler * __restrict sampler) {
  GPUSamplerVk *native;

  if (!sampler) {
    return;
  }

  native = sampler->_priv;
  if (native && native->device && native->sampler) {
    vkDestroySampler(native->device, native->sampler, NULL);
  }
  free(sampler);
}

GPU_HIDE
void
vk_initSampler(GPUApiSampler *api) {
  api->createSampler  = vk_createSampler;
  api->destroySampler = vk_destroySampler;
}
