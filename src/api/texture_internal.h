/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef gpu_texture_internal_h
#define gpu_texture_internal_h

#include "../common.h"

struct GPUTexture {
  void                         *_priv;
  GPUDevice                    *device;
  GPUHeap                      *_heap;
  uint64_t                      _heapOffset;
  uint64_t                      _allocationSize;
  GPUSparseTextureRequirements _sparseRequirements;
  GPUFormat                     format;
  GPUTextureDimension           dimension;
  uint32_t                      width;
  uint32_t                      height;
  uint32_t                      depthOrLayers;
  uint32_t                      mipLevelCount;
  uint32_t                      sampleCount;
  GPUTextureUsageFlags          usage;
  bool                          _sparse;
  bool                          _ownsNative;
};

struct GPUTextureView {
  void              *_priv;
  GPUTexture        *_texture;
  uint64_t           _gpuResourceID;
  GPUFormat          format;
  GPUTextureViewType viewType;
  uint32_t           baseMipLevel;
  uint32_t           mipLevelCount;
  uint32_t           baseArrayLayer;
  uint32_t           arrayLayerCount;
  bool               _ownsNative;
};

GPU_HIDE
GPUResult
gpuValidateTextureCreateInfo(const GPUDevice            *device,
                             const GPUTextureCreateInfo *info);

static inline uint32_t
gpuTextureArrayLayerCount(const GPUTexture *texture) {
  if (!texture) {
    return 0u;
  }

  return texture->dimension == GPU_TEXTURE_DIMENSION_3D
           ? 1u
           : texture->depthOrLayers;
}

static inline bool
gpuTextureSubresourceRangeValid(const GPUTexture *texture,
                                uint32_t          baseMip,
                                uint32_t          mipCount,
                                uint32_t          baseLayer,
                                uint32_t          layerCount) {
  uint32_t arrayLayerCount;

  if (!texture || mipCount == 0u || layerCount == 0u ||
      baseMip >= texture->mipLevelCount ||
      mipCount > texture->mipLevelCount - baseMip) {
    return false;
  }

  arrayLayerCount = gpuTextureArrayLayerCount(texture);
  return baseLayer < arrayLayerCount &&
         layerCount <= arrayLayerCount - baseLayer;
}

#endif /* gpu_texture_internal_h */
