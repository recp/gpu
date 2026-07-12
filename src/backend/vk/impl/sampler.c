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
  outInfo->maxLod       = VK_LOD_CLAMP_NONE;
}

static VkCompareOp
vk__compareOp(uint32_t compare) {
  switch (compare) {
    case GPUUSLSamplerCompareLess:
      return VK_COMPARE_OP_LESS;
    case GPUUSLSamplerCompareEqual:
      return VK_COMPARE_OP_EQUAL;
    case GPUUSLSamplerCompareLessEqual:
      return VK_COMPARE_OP_LESS_OR_EQUAL;
    case GPUUSLSamplerCompareGreater:
      return VK_COMPARE_OP_GREATER;
    case GPUUSLSamplerCompareNotEqual:
      return VK_COMPARE_OP_NOT_EQUAL;
    case GPUUSLSamplerCompareGreaterEqual:
      return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case GPUUSLSamplerCompareAlways:
      return VK_COMPARE_OP_ALWAYS;
    case GPUUSLSamplerCompareNever:
    default:
      return VK_COMPARE_OP_NEVER;
  }
}

static VkSamplerAddressMode
vk__uslAddressMode(uint32_t mode) {
  switch (mode) {
    case GPUUSLSamplerAddressRepeat:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case GPUUSLSamplerAddressMirroredRepeat:
      return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case GPUUSLSamplerAddressClampToZero:
    case GPUUSLSamplerAddressClampToBorder:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case GPUUSLSamplerAddressClampToEdge:
    default:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
}

GPU_HIDE
void
vk_fillUSLSamplerInfo(const GPUUSLStaticSamplerDesc *desc,
                      VkSamplerCreateInfo           *outInfo) {
  VkFilter            minFilter;
  VkFilter            magFilter;
  VkSamplerMipmapMode  mipFilter;
  VkSamplerAddressMode addressMode;

  minFilter   = desc->minFilter == GPUUSLSamplerFilterLinear
                  ? VK_FILTER_LINEAR
                  : VK_FILTER_NEAREST;
  magFilter   = desc->magFilter == GPUUSLSamplerFilterLinear
                  ? VK_FILTER_LINEAR
                  : VK_FILTER_NEAREST;
  mipFilter   = desc->mipFilter == GPUUSLSamplerFilterLinear
                  ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                  : VK_SAMPLER_MIPMAP_MODE_NEAREST;
  addressMode = vk__uslAddressMode(desc->addressMode);

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
    desc->coordSpace == GPUUSLSamplerCoordPixel ? VK_TRUE : VK_FALSE;
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
GPUResult
vk_createSamplerFromUSL(GPUApi                        * __restrict api,
                        GPUDevice                     * __restrict device,
                        const GPUUSLStaticSamplerDesc *desc,
                        bool                           staticIfSupported,
                        GPUSampler                   **outSampler) {
  VkSamplerCreateInfo samplerInfo = {0};
  GPU__UNUSED(api);
  GPU__UNUSED(staticIfSupported);
  if (!desc || desc->maxAnisotropy > 1u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (desc->coordSpace == GPUUSLSamplerCoordPixel &&
      (desc->minFilter != desc->magFilter ||
       desc->mipFilter != GPUUSLSamplerFilterNearest ||
       desc->hasCompare ||
       (desc->addressMode != GPUUSLSamplerAddressClampToEdge &&
        desc->addressMode != GPUUSLSamplerAddressClampToZero &&
        desc->addressMode != GPUUSLSamplerAddressClampToBorder))) {
    return GPU_ERROR_UNSUPPORTED;
  }

  vk_fillUSLSamplerInfo(desc, &samplerInfo);
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
  api->createSampler                     = vk_createSampler;
  api->createSamplerFromUSLStaticSampler = vk_createSamplerFromUSL;
  api->destroySampler                    = vk_destroySampler;
}
