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

GPU_HIDE
bool
vk_formatFromGPU(GPUFormat format, VkFormat *outFormat) {
  VkFormat result;

  if (!outFormat) {
    return false;
  }

  switch (format) {
    case GPU_FORMAT_RGBA8_UNORM:
      result = VK_FORMAT_R8G8B8A8_UNORM;
      break;
    case GPU_FORMAT_RGBA8_UNORM_SRGB:
      result = VK_FORMAT_R8G8B8A8_SRGB;
      break;
    case GPU_FORMAT_BGRA8_UNORM:
      result = VK_FORMAT_B8G8R8A8_UNORM;
      break;
    case GPU_FORMAT_BGRA8_UNORM_SRGB:
      result = VK_FORMAT_B8G8R8A8_SRGB;
      break;
    case GPU_FORMAT_RGBA16_FLOAT:
      result = VK_FORMAT_R16G16B16A16_SFLOAT;
      break;
    case GPU_FORMAT_RGBA32_FLOAT:
      result = VK_FORMAT_R32G32B32A32_SFLOAT;
      break;
    case GPU_FORMAT_RG11B10_UFLOAT:
      result = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
      break;
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
      result = VK_FORMAT_D24_UNORM_S8_UINT;
      break;
    case GPU_FORMAT_DEPTH32_FLOAT:
      result = VK_FORMAT_D32_SFLOAT;
      break;
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      result = VK_FORMAT_D32_SFLOAT_S8_UINT;
      break;
    default:
      return false;
  }

  *outFormat = result;
  return true;
}

GPU_HIDE
GPUFormat
vk_formatToGPU(VkFormat format) {
  switch (format) {
    case VK_FORMAT_R8G8B8A8_UNORM:
      return GPU_FORMAT_RGBA8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:
      return GPU_FORMAT_RGBA8_UNORM_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:
      return GPU_FORMAT_BGRA8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:
      return GPU_FORMAT_BGRA8_UNORM_SRGB;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      return GPU_FORMAT_RGBA16_FLOAT;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
      return GPU_FORMAT_RGBA32_FLOAT;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
      return GPU_FORMAT_RG11B10_UFLOAT;
    case VK_FORMAT_D24_UNORM_S8_UINT:
      return GPU_FORMAT_DEPTH24_UNORM_STENCIL8;
    case VK_FORMAT_D32_SFLOAT:
      return GPU_FORMAT_DEPTH32_FLOAT;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
    default:
      return GPU_FORMAT_UNDEFINED;
  }
}
