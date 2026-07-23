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
#include "../impl.h"

enum {
  DX12_TEXTURE_BARRIER_CHUNK_SIZE = 16u
};

#if GPU_BUILD_WITH_DEBUG_MARKERS
static void
dx12__setTextureName(ID3D12Resource *resource, const char *label) {
  wchar_t name[256];

  if (!resource || !label || label[0] == '\0' ||
      MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          label,
                          -1,
                          name,
                          (int)GPU_ARRAY_LEN(name)) <= 0) {
    return;
  }

  (void)resource->lpVtbl->SetName(resource, name);
}
#endif

static bool
dx12__textureDimension(GPUTextureDimension dimension,
                       D3D12_RESOURCE_DIMENSION *outDimension) {
  if (!outDimension) {
    return false;
  }

  switch (dimension) {
    case GPU_TEXTURE_DIMENSION_1D:
      *outDimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
      return true;
    case GPU_TEXTURE_DIMENSION_2D:
      *outDimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      return true;
    case GPU_TEXTURE_DIMENSION_3D:
      *outDimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
      return true;
    default:
      return false;
  }
}

static D3D12_RESOURCE_STATES
dx12__textureFinalState(GPUTextureUsageFlags usage) {
  if ((usage & GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT) != 0u) {
    return D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE;
  }
  if ((usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u) {
    return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }
  if ((usage & GPU_TEXTURE_USAGE_STORAGE) != 0u) {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if ((usage & GPU_TEXTURE_USAGE_COPY_SRC) != 0u) {
    return D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  return D3D12_RESOURCE_STATE_COPY_DEST;
}

static bool
dx12__depthFormat(GPUFormat format) {
  return format == GPU_FORMAT_DEPTH16_UNORM ||
         format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
         format == GPU_FORMAT_DEPTH32_FLOAT ||
         format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
}

static DXGI_FORMAT
dx12__textureResourceFormat(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_DEPTH16_UNORM:
      return DXGI_FORMAT_R16_TYPELESS;
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
      return DXGI_FORMAT_R24G8_TYPELESS;
    case GPU_FORMAT_DEPTH32_FLOAT:
      return DXGI_FORMAT_R32_TYPELESS;
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:
      return dx12_format(format);
  }
}

static DXGI_FORMAT
dx12__textureSrvFormat(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_DEPTH16_UNORM:
      return DXGI_FORMAT_R16_UNORM;
    case GPU_FORMAT_DEPTH24_UNORM_STENCIL8:
      return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case GPU_FORMAT_DEPTH32_FLOAT:
      return DXGI_FORMAT_R32_FLOAT;
    case GPU_FORMAT_DEPTH32_FLOAT_STENCIL8:
      return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:
      return dx12_format(format);
  }
}

static uint32_t
dx12__texturePlaneCount(GPUFormat format) {
  return format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
         format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8
           ? 2u
           : 1u;
}

static bool
dx12__textureWritePlane(GPUFormat         format,
                        GPUTextureAspect  aspect,
                        uint32_t         *outPlane) {
  GPUTextureAspect resolved;

  if (!outPlane ||
      !gpuFormatResolveCopyAspect(format, aspect, &resolved)) {
    return false;
  }

  *outPlane = resolved == GPU_TEXTURE_ASPECT_STENCIL_ONLY &&
              dx12__texturePlaneCount(format) > 1u
                ? 1u
                : 0u;
  return true;
}

static bool
dx12__textureRangeValid(const GPUTextureDX12 *texture,
                        uint32_t               baseMip,
                        uint32_t               mipCount,
                        uint32_t               baseLayer,
                        uint32_t               layerCount) {
  return texture && texture->resource && texture->states &&
         mipCount > 0u && layerCount > 0u &&
         baseMip < texture->mipLevelCount &&
         mipCount <= texture->mipLevelCount - baseMip &&
         baseLayer < texture->arrayLayerCount &&
         layerCount <= texture->arrayLayerCount - baseLayer;
}

static uint32_t
dx12__textureSubresource(const GPUTextureDX12 *texture,
                         uint32_t               mip,
                         uint32_t               layer,
                         uint32_t               plane) {
  return mip + layer * texture->mipLevelCount +
         plane * texture->mipLevelCount * texture->arrayLayerCount;
}

static bool
dx12__textureRangeFull(const GPUTextureDX12 *texture,
                       uint32_t               baseMip,
                       uint32_t               mipCount,
                       uint32_t               baseLayer,
                       uint32_t               layerCount,
                       uint32_t               basePlane,
                       uint32_t               planeCount) {
  return baseMip == 0u && mipCount == texture->mipLevelCount &&
         baseLayer == 0u && layerCount == texture->arrayLayerCount &&
         basePlane == 0u && planeCount == texture->planeCount;
}

static void
dx12__materializeTextureStates(GPUTextureDX12 *texture) {
  if (!texture || !texture->states || !texture->stateUniform) {
    return;
  }

  for (uint32_t i = 0u; i < texture->subresourceCount; i++) {
    texture->states[i] = texture->state;
  }
}

GPU_HIDE
void
dx12_setTextureState(GPUTextureDX12        *texture,
                     uint32_t               baseMip,
                     uint32_t               mipCount,
                     uint32_t               baseLayer,
                     uint32_t               layerCount,
                     D3D12_RESOURCE_STATES  state) {
  if (!dx12__textureRangeValid(texture,
                               baseMip,
                               mipCount,
                               baseLayer,
                               layerCount)) {
    return;
  }

  if (dx12__textureRangeFull(texture,
                             baseMip,
                             mipCount,
                             baseLayer,
                             layerCount,
                             0u,
                             texture->planeCount)) {
    texture->state        = state;
    texture->stateUniform = true;
    return;
  }
  if (texture->stateUniform && texture->state == state) {
    return;
  }

  dx12__materializeTextureStates(texture);
  for (uint32_t plane = 0u; plane < texture->planeCount; plane++) {
    for (uint32_t layer = baseLayer; layer < baseLayer + layerCount; layer++) {
      for (uint32_t mip = baseMip; mip < baseMip + mipCount; mip++) {
        uint32_t subresource;

        subresource = dx12__textureSubresource(texture, mip, layer, plane);
        texture->states[subresource] = state;
      }
    }
  }
  texture->stateUniform = false;
}

static bool
dx12__transitionTexture(ID3D12GraphicsCommandList *commandList,
                        GPUTextureDX12            *texture,
                        uint32_t                   baseMip,
                        uint32_t                   mipCount,
                        uint32_t                   baseLayer,
                        uint32_t                   layerCount,
                        uint32_t                   basePlane,
                        uint32_t                   planeCount,
                        D3D12_RESOURCE_STATES      state) {
  D3D12_RESOURCE_BARRIER barriers[DX12_TEXTURE_BARRIER_CHUNK_SIZE];
  uint32_t               barrierCount;
  bool                   fullRange;
  bool                   changed;

  if (!commandList ||
      !dx12__textureRangeValid(texture,
                               baseMip,
                               mipCount,
                               baseLayer,
                               layerCount) ||
      planeCount == 0u || basePlane >= texture->planeCount ||
      planeCount > texture->planeCount - basePlane) {
    return false;
  }

  fullRange = dx12__textureRangeFull(texture,
                                     baseMip,
                                     mipCount,
                                     baseLayer,
                                     layerCount,
                                     basePlane,
                                     planeCount);
  if (fullRange && texture->stateUniform) {
    if (texture->state == state) {
      return true;
    }

    memset(&barriers[0], 0, sizeof(barriers[0]));
    barriers[0].Type                   =
      D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource   = texture->resource;
    barriers[0].Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barriers[0].Transition.StateBefore = texture->state;
    barriers[0].Transition.StateAfter  = state;
    commandList->lpVtbl->ResourceBarrier(commandList, 1u, barriers);
    texture->state        = state;
    texture->stateUniform = true;
    return true;
  }
  if (texture->stateUniform && texture->state == state) {
    return true;
  }

  dx12__materializeTextureStates(texture);
  barrierCount = 0u;
  changed      = false;
  for (uint32_t plane = basePlane; plane < basePlane + planeCount; plane++) {
    for (uint32_t layer = baseLayer; layer < baseLayer + layerCount; layer++) {
      for (uint32_t mip = baseMip; mip < baseMip + mipCount; mip++) {
        D3D12_RESOURCE_BARRIER *barrier;
        uint32_t                subresource;

        subresource = dx12__textureSubresource(texture, mip, layer, plane);
        if (texture->states[subresource] == state) {
          continue;
        }

        barrier = &barriers[barrierCount++];
        memset(barrier, 0, sizeof(*barrier));
        barrier->Type                   =
          D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier->Transition.pResource   = texture->resource;
        barrier->Transition.Subresource = subresource;
        barrier->Transition.StateBefore = texture->states[subresource];
        barrier->Transition.StateAfter  = state;
        texture->states[subresource]    = state;
        changed                         = true;

        if (barrierCount == DX12_TEXTURE_BARRIER_CHUNK_SIZE) {
          commandList->lpVtbl->ResourceBarrier(commandList,
                                                barrierCount,
                                                barriers);
          barrierCount = 0u;
        }
      }
    }
  }
  if (barrierCount > 0u) {
    commandList->lpVtbl->ResourceBarrier(commandList,
                                          barrierCount,
                                          barriers);
  }

  if (fullRange) {
    texture->state        = state;
    texture->stateUniform = true;
  } else if (changed && texture->stateUniform && texture->state != state) {
    texture->stateUniform = false;
  }
  return true;
}

GPU_HIDE
bool
dx12_transitionTexture(ID3D12GraphicsCommandList *commandList,
                       GPUTextureDX12            *texture,
                       uint32_t                   baseMip,
                       uint32_t                   mipCount,
                       uint32_t                   baseLayer,
                       uint32_t                   layerCount,
                       D3D12_RESOURCE_STATES      state) {
  return dx12__transitionTexture(commandList,
                                 texture,
                                 baseMip,
                                 mipCount,
                                 baseLayer,
                                 layerCount,
                                 0u,
                                 texture ? texture->planeCount : 0u,
                                 state);
}

GPU_HIDE
bool
dx12_transitionTexturePlane(ID3D12GraphicsCommandList *commandList,
                            GPUTextureDX12            *texture,
                            uint32_t                   baseMip,
                            uint32_t                   mipCount,
                            uint32_t                   baseLayer,
                            uint32_t                   layerCount,
                            uint32_t                   plane,
                            D3D12_RESOURCE_STATES      state) {
  return dx12__transitionTexture(commandList,
                                 texture,
                                 baseMip,
                                 mipCount,
                                 baseLayer,
                                 layerCount,
                                 plane,
                                 1u,
                                 state);
}

static GPUResult
dx12__textureDesc(GPUDevice                  *device,
                  const GPUTextureCreateInfo *info,
                  D3D12_RESOURCE_DESC        *outDesc,
                  D3D12_CLEAR_VALUE          *outClearValue,
                  D3D12_RESOURCE_STATES      *outInitialState,
                  uint32_t                   *outMipLevelCount,
                  uint32_t                   *outArrayLayerCount,
                  uint32_t                   *outPlaneCount,
                  uint32_t                   *outSubresourceCount) {
  GPUDeviceDX12          *deviceDX12;
  D3D12_RESOURCE_DIMENSION dimension;
  DXGI_FORMAT             format;
  uint32_t                mipLevelCount;
  uint32_t                arrayLayerCount;
  uint32_t                planeCount;
  uint32_t                subresourceCount;
  uint32_t                sampleCount;

  if (!device || !(deviceDX12 = device->_priv) || !info || !outDesc ||
      !outClearValue || !outInitialState || !outMipLevelCount ||
      !outArrayLayerCount || !outPlaneCount || !outSubresourceCount ||
      info->mipLevelCount > UINT16_MAX ||
      info->depthOrLayers > UINT16_MAX ||
      !dx12__textureDimension(info->dimension, &dimension)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if ((info->dimension == GPU_TEXTURE_DIMENSION_1D && info->height != 1u) ||
      (info->dimension == GPU_TEXTURE_DIMENSION_3D &&
       info->depthOrLayers == 0u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  mipLevelCount = info->mipLevelCount ? info->mipLevelCount : 1u;
  format        = dx12__textureResourceFormat(info->format);
  if (format == DXGI_FORMAT_UNKNOWN) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (((info->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u &&
       dx12__depthFormat(info->format)) ||
      ((info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u &&
       !dx12__depthFormat(info->format)) ||
      (info->usage & (GPU_TEXTURE_USAGE_COLOR_TARGET |
                      GPU_TEXTURE_USAGE_DEPTH_STENCIL)) ==
        (GPU_TEXTURE_USAGE_COLOR_TARGET |
         GPU_TEXTURE_USAGE_DEPTH_STENCIL) ||
      ((info->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u &&
       ((info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u ||
        info->sampleCount > 1u)) ||
      ((info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u &&
       info->dimension != GPU_TEXTURE_DIMENSION_2D)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if ((info->usage & GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT) != 0u &&
      (deviceDX12->vrsTier < D3D12_VARIABLE_SHADING_RATE_TIER_2 ||
       info->format != GPU_FORMAT_R8_UINT ||
       info->dimension != GPU_TEXTURE_DIMENSION_2D ||
       info->depthOrLayers != 1u || mipLevelCount != 1u ||
       (info->usage & (GPU_TEXTURE_USAGE_COLOR_TARGET |
                       GPU_TEXTURE_USAGE_DEPTH_STENCIL)) != 0u ||
       (info->sampleCount != 0u && info->sampleCount != 1u))) {
    return GPU_ERROR_UNSUPPORTED;
  }

  arrayLayerCount = info->dimension == GPU_TEXTURE_DIMENSION_3D
                      ? 1u
                      : info->depthOrLayers;
  planeCount      = dx12__texturePlaneCount(info->format);
  if (mipLevelCount > UINT32_MAX / arrayLayerCount) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  subresourceCount = mipLevelCount * arrayLayerCount;
  if (subresourceCount > UINT32_MAX / planeCount) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  subresourceCount *= planeCount;

  sampleCount = info->sampleCount ? info->sampleCount : 1u;
  if (sampleCount > 1u) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = {0};

    levels.Format      = dx12_format(info->format);
    levels.SampleCount = sampleCount;
    if (FAILED(deviceDX12->d3dDevice->lpVtbl->CheckFeatureSupport(
          deviceDX12->d3dDevice,
          D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
          &levels,
          sizeof(levels))) || levels.NumQualityLevels == 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  outDesc->Dimension        = dimension;
  outDesc->Width            = info->width;
  outDesc->Height           = info->dimension == GPU_TEXTURE_DIMENSION_1D
                                ? 1u
                                : info->height;
  outDesc->DepthOrArraySize = (UINT16)info->depthOrLayers;
  outDesc->MipLevels        = (UINT16)mipLevelCount;
  outDesc->Format           = format;
  outDesc->SampleDesc.Count = sampleCount;
  outDesc->Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if ((info->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u) {
    outDesc->Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }
  if ((info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u) {
    outDesc->Flags                     |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    outClearValue->Format               = dx12_format(info->format);
    outClearValue->DepthStencil.Depth   = 1.0f;
    outClearValue->DepthStencil.Stencil = 0u;
  }
  if ((info->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u) {
    outDesc->Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  *outInitialState = (info->usage & GPU_TEXTURE_USAGE_COPY_DST) != 0u
                       ? D3D12_RESOURCE_STATE_COPY_DEST
                       : D3D12_RESOURCE_STATE_COMMON;
  *outMipLevelCount    = mipLevelCount;
  *outArrayLayerCount  = arrayLayerCount;
  *outPlaneCount       = planeCount;
  *outSubresourceCount = subresourceCount;
  return GPU_OK;
}

static GPUResult
dx12__wrapTexture(GPUDevice                  *device,
                  const GPUTextureCreateInfo *info,
                  ID3D12Resource             *resource,
                  D3D12_RESOURCE_STATES       initialState,
                  uint32_t                    mipLevelCount,
                  uint32_t                    arrayLayerCount,
                  uint32_t                    planeCount,
                  uint32_t                    subresourceCount,
                  GPUTexture                **outTexture) {
  GPUTexture     *texture;
  GPUTextureDX12 *native;
  size_t          allocationSize;

  if (subresourceCount >
      (SIZE_MAX - sizeof(*texture) - sizeof(*native)) /
        sizeof(*native->states)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  allocationSize = sizeof(*texture) + sizeof(*native) +
                   (size_t)subresourceCount * sizeof(*native->states);
  texture = calloc(1, allocationSize);
  if (!texture) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  dx12__setTextureName(resource, gpuDeviceDebugLabel(device, info->label));
#endif
  native                   = (GPUTextureDX12 *)(texture + 1);
  native->resource         = resource;
  native->states           = (D3D12_RESOURCE_STATES *)(native + 1);
  native->state            = initialState;
  native->mipLevelCount    = mipLevelCount;
  native->arrayLayerCount  = arrayLayerCount;
  native->subresourceCount = subresourceCount;
  native->planeCount       = planeCount;
  native->stateUniform     = true;
  texture->_priv           = native;
  texture->device          = device;
  texture->format          = info->format;
  texture->dimension       = info->dimension;
  texture->width           = info->width;
  texture->height          = info->height;
  texture->depthOrLayers   = info->depthOrLayers;
  texture->mipLevelCount   = mipLevelCount;
  texture->sampleCount     = info->sampleCount ? info->sampleCount : 1u;
  texture->usage           = info->usage;
  texture->_ownsNative     = true;
  *outTexture              = texture;
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_getTextureMemoryRequirements(GPUDevice                  *device,
                                  const GPUTextureCreateInfo *info,
                                  GPUMemoryRequirements      *outRequirements) {
  GPUDeviceDX12                 *deviceDX12;
  D3D12_RESOURCE_DESC            desc = {0};
  D3D12_CLEAR_VALUE              clearValue = {0};
  D3D12_RESOURCE_ALLOCATION_INFO allocationInfo;
  D3D12_RESOURCE_STATES          initialState;
  uint32_t                       mipLevelCount;
  uint32_t                       arrayLayerCount;
  uint32_t                       planeCount;
  uint32_t                       subresourceCount;
  GPUResult                      result;

  if (!device || !(deviceDX12 = device->_priv) || !info || !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = dx12__textureDesc(device,
                            info,
                            &desc,
                            &clearValue,
                            &initialState,
                            &mipLevelCount,
                            &arrayLayerCount,
                            &planeCount,
                            &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }
  GPU__UNUSED(clearValue);
  GPU__UNUSED(initialState);
  GPU__UNUSED(mipLevelCount);
  GPU__UNUSED(arrayLayerCount);
  GPU__UNUSED(planeCount);
  GPU__UNUSED(subresourceCount);
  deviceDX12->d3dDevice->lpVtbl->GetResourceAllocationInfo(
    deviceDX12->d3dDevice,
    &allocationInfo,
    0u,
    1u,
    &desc
  );
  if (allocationInfo.SizeInBytes == UINT64_MAX ||
      allocationInfo.Alignment == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  outRequirements->sizeBytes         = allocationInfo.SizeInBytes;
  outRequirements->alignmentBytes    = allocationInfo.Alignment;
  outRequirements->compatibilityMask = dx12_memoryCompatibility(device,
                                                                 &desc);
  if (outRequirements->compatibilityMask == 0u) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

static GPUResult
dx12__sparseTextureRequirements(
  GPUDevice                    *device,
  ID3D12Resource              *resource,
  const D3D12_RESOURCE_DESC   *desc,
  uint32_t                     mipLevelCount,
  GPUSparseTextureRequirements *outRequirements,
  D3D12_PACKED_MIP_INFO       *outPackedMipInfo,
  D3D12_TILE_SHAPE            *outTileShape
) {
  GPUDeviceDX12           *deviceDX12;
  D3D12_PACKED_MIP_INFO    packedMipInfo = {0};
  D3D12_TILE_SHAPE         tileShape = {0};
  UINT                     tileCount;
  UINT                     arrayLayerCount;
  UINT                     subresourceTilingCount;

  if (!device || !(deviceDX12 = device->_priv) || !resource || !desc ||
      !outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  tileCount               = 0u;
  subresourceTilingCount  = 0u;
  deviceDX12->d3dDevice->lpVtbl->GetResourceTiling(
    deviceDX12->d3dDevice,
    resource,
    &tileCount,
    &packedMipInfo,
    &tileShape,
    &subresourceTilingCount,
    0u,
    NULL
  );
  arrayLayerCount = desc->Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                      ? 1u
                      : desc->DepthOrArraySize;
  if (tileCount == 0u || arrayLayerCount == 0u ||
      tileCount % arrayLayerCount != 0u ||
      tileShape.WidthInTexels == 0u ||
      tileShape.HeightInTexels == 0u || tileShape.DepthInTexels == 0u ||
      packedMipInfo.NumStandardMips > mipLevelCount) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outRequirements->compatibilityMask = dx12_memoryCompatibility(device, desc);
  outRequirements->pageSizeBytes     = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  outRequirements->mipTailTileCount  = packedMipInfo.NumTilesForPackedMips;
  outRequirements->mipTailLayerStrideTiles =
    packedMipInfo.NumPackedMips > 0u
      ? tileCount / arrayLayerCount
      : 0u;
  outRequirements->tileWidth         = tileShape.WidthInTexels;
  outRequirements->tileHeight        = tileShape.HeightInTexels;
  outRequirements->tileDepth         = tileShape.DepthInTexels;
  outRequirements->firstMipInTail    = packedMipInfo.NumPackedMips > 0u
                                         ? packedMipInfo.NumStandardMips
                                         : mipLevelCount;
  if (outPackedMipInfo) {
    *outPackedMipInfo = packedMipInfo;
  }
  if (outTileShape) {
    *outTileShape = tileShape;
  }
  return outRequirements->compatibilityMask != 0u
           ? GPU_OK
           : GPU_ERROR_BACKEND_FAILURE;
}

GPU_HIDE
GPUResult
dx12_getSparseTextureRequirements(
  GPUDevice                    *device,
  const GPUTextureCreateInfo   *info,
  GPUSparseTextureRequirements *outRequirements
) {
  GPUDeviceDX12       *deviceDX12;
  ID3D12Resource      *resource;
  D3D12_RESOURCE_DESC  desc = {0};
  D3D12_CLEAR_VALUE    clearValue = {0};
  D3D12_RESOURCE_STATES initialState;
  uint32_t             mipLevelCount;
  uint32_t             arrayLayerCount;
  uint32_t             planeCount;
  uint32_t             subresourceCount;
  GPUResult            result;
  HRESULT              nativeResult;

  if (!device || !(deviceDX12 = device->_priv) || !info ||
      !outRequirements ||
      deviceDX12->tiledResourcesTier < D3D12_TILED_RESOURCES_TIER_1 ||
      info->dimension == GPU_TEXTURE_DIMENSION_1D ||
      (info->sampleCount != 0u && info->sampleCount != 1u) ||
      (info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  result = dx12__textureDesc(device,
                            info,
                            &desc,
                            &clearValue,
                            &initialState,
                            &mipLevelCount,
                            &arrayLayerCount,
                            &planeCount,
                            &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }
  GPU__UNUSED(clearValue);
  GPU__UNUSED(initialState);
  GPU__UNUSED(arrayLayerCount);
  GPU__UNUSED(planeCount);
  GPU__UNUSED(subresourceCount);
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

  resource = NULL;
  nativeResult = deviceDX12->d3dDevice->lpVtbl->CreateReservedResource(
    deviceDX12->d3dDevice,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  if (FAILED(nativeResult) || !resource) {
    return GPU_ERROR_UNSUPPORTED;
  }
  result = dx12__sparseTextureRequirements(device,
                                            resource,
                                            &desc,
                                            mipLevelCount,
                                            outRequirements,
                                            NULL,
                                            NULL);
  resource->lpVtbl->Release(resource);
  return result;
}

GPU_HIDE
GPUResult
dx12_createSparseTexture(GPUDevice                  *device,
                         const GPUTextureCreateInfo *info,
                         GPUHeap                    *heap,
                         GPUTexture                **outTexture) {
  GPUDeviceDX12       *deviceDX12;
  ID3D12Resource      *resource;
  D3D12_RESOURCE_DESC  desc = {0};
  D3D12_CLEAR_VALUE    clearValue = {0};
  D3D12_RESOURCE_STATES initialState;
  GPUSparseTextureRequirements requirements;
  D3D12_PACKED_MIP_INFO packedMipInfo;
  D3D12_TILE_SHAPE      tileShape;
  uint32_t             mipLevelCount;
  uint32_t             arrayLayerCount;
  uint32_t             planeCount;
  uint32_t             subresourceCount;
  GPUResult            result;
  HRESULT              nativeResult;

  if (!device || !(deviceDX12 = device->_priv) || !info || !heap ||
      !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = dx12__textureDesc(device,
                            info,
                            &desc,
                            &clearValue,
                            &initialState,
                            &mipLevelCount,
                            &arrayLayerCount,
                            &planeCount,
                            &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }
  desc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

  resource = NULL;
  nativeResult = deviceDX12->d3dDevice->lpVtbl->CreateReservedResource(
    deviceDX12->d3dDevice,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  if (FAILED(nativeResult) || !resource) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = dx12__sparseTextureRequirements(device,
                                            resource,
                                            &desc,
                                            mipLevelCount,
                                            &requirements,
                                            &packedMipInfo,
                                            &tileShape);
  if (result == GPU_OK) {
    result = dx12__wrapTexture(device,
                               info,
                               resource,
                               D3D12_RESOURCE_STATE_COMMON,
                               mipLevelCount,
                               arrayLayerCount,
                               planeCount,
                               subresourceCount,
                               outTexture);
  }
  if (result != GPU_OK) {
    resource->lpVtbl->Release(resource);
    return result;
  }
  ((GPUTextureDX12 *)(*outTexture)->_priv)->packedMipInfo = packedMipInfo;
  ((GPUTextureDX12 *)(*outTexture)->_priv)->tileShape     = tileShape;
  ((GPUTextureDX12 *)(*outTexture)->_priv)->sparse       = true;
  GPU__UNUSED(heap);
  return GPU_OK;
}

GPU_HIDE
GPUResult
dx12_createPlacedTexture(GPUDevice                  *device,
                         const GPUTextureCreateInfo *info,
                         GPUHeap                    *heap,
                         uint64_t                    heapOffset,
                         GPUTexture                **outTexture) {
  GPUDeviceDX12       *deviceDX12;
  GPUHeapDX12         *heapDX12;
  ID3D12Resource      *resource;
  D3D12_RESOURCE_DESC  desc = {0};
  D3D12_CLEAR_VALUE    clearValue = {0};
  D3D12_RESOURCE_STATES initialState;
  uint32_t             mipLevelCount;
  uint32_t             arrayLayerCount;
  uint32_t             planeCount;
  uint32_t             subresourceCount;
  GPUResult            result;
  HRESULT              nativeResult;

  if (!device || !(deviceDX12 = device->_priv) || !info || !heap ||
      !(heapDX12 = heap->_priv) || !heapDX12->heap || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = dx12__textureDesc(device,
                            info,
                            &desc,
                            &clearValue,
                            &initialState,
                            &mipLevelCount,
                            &arrayLayerCount,
                            &planeCount,
                            &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }

  resource = NULL;
  nativeResult = deviceDX12->d3dDevice->lpVtbl->CreatePlacedResource(
    deviceDX12->d3dDevice,
    heapDX12->heap,
    heapOffset,
    &desc,
    initialState,
    (info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u ? &clearValue : NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  if (FAILED(nativeResult) || !resource) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = dx12__wrapTexture(device,
                             info,
                             resource,
                             initialState,
                             mipLevelCount,
                             arrayLayerCount,
                             planeCount,
                             subresourceCount,
                             outTexture);
  if (result != GPU_OK) {
    resource->lpVtbl->Release(resource);
  }
  return result;
}

GPU_HIDE
GPUResult
dx12_createTexture(GPUDevice                  * __restrict device,
                   const GPUTextureCreateInfo * __restrict info,
                   GPUTexture                ** __restrict outTexture) {
  GPUDeviceDX12        *deviceDX12;
  ID3D12Resource       *resource;
  D3D12_HEAP_PROPERTIES heap = {0};
  D3D12_RESOURCE_DESC   desc = {0};
  D3D12_CLEAR_VALUE     clearValue = {0};
  D3D12_RESOURCE_STATES initialState;
  uint32_t              mipLevelCount;
  uint32_t              arrayLayerCount;
  uint32_t              planeCount;
  uint32_t              subresourceCount;
  GPUResult             result;
  HRESULT               nativeResult;

  if (!device || !(deviceDX12 = device->_priv) || !info || !outTexture) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outTexture = NULL;
  result = dx12__textureDesc(device,
                            info,
                            &desc,
                            &clearValue,
                            &initialState,
                            &mipLevelCount,
                            &arrayLayerCount,
                            &planeCount,
                            &subresourceCount);
  if (result != GPU_OK) {
    return result;
  }

  heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  resource              = NULL;
  nativeResult = deviceDX12->d3dDevice->lpVtbl->CreateCommittedResource(
    deviceDX12->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    initialState,
    (info->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u ? &clearValue : NULL,
    &IID_ID3D12Resource,
    (void **)&resource
  );
  if (FAILED(nativeResult) || !resource) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  result = dx12__wrapTexture(device,
                             info,
                             resource,
                             initialState,
                             mipLevelCount,
                             arrayLayerCount,
                             planeCount,
                             subresourceCount,
                             outTexture);
  if (result != GPU_OK) {
    resource->lpVtbl->Release(resource);
  }
  return result;
}

GPU_HIDE
void
dx12_destroyTexture(GPUTexture * __restrict texture) {
  GPUTextureDX12 *native;

  if (!texture) {
    return;
  }

  native = texture->_priv;
  if (native && native->resource) {
    native->resource->lpVtbl->Release(native->resource);
  }
  free(texture);
}

static bool
dx12__fillTextureSrv(const GPUTextureViewCreateInfo *info,
                     bool                            multisampled,
                     D3D12_SHADER_RESOURCE_VIEW_DESC *srv) {
  switch (info->viewType) {
    case GPU_TEXTURE_VIEW_1D:
      if (info->arrayLayerCount != 1u) {
        return false;
      }
      srv->ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE1D;
      srv->Texture1D.MostDetailedMip     = info->baseMipLevel;
      srv->Texture1D.MipLevels           = info->mipLevelCount;
      srv->Texture1D.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      srv->ViewDimension                      =
        D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
      srv->Texture1DArray.MostDetailedMip     = info->baseMipLevel;
      srv->Texture1DArray.MipLevels           = info->mipLevelCount;
      srv->Texture1DArray.FirstArraySlice     = info->baseArrayLayer;
      srv->Texture1DArray.ArraySize           = info->arrayLayerCount;
      srv->Texture1DArray.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_2D:
      if (info->arrayLayerCount != 1u) {
        return false;
      }
      if (multisampled) {
        srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        return info->baseMipLevel == 0u && info->mipLevelCount == 1u;
      }
      srv->ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
      srv->Texture2D.MostDetailedMip     = info->baseMipLevel;
      srv->Texture2D.MipLevels           = info->mipLevelCount;
      srv->Texture2D.PlaneSlice          = 0u;
      srv->Texture2D.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      srv->ViewDimension                      =
        D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      srv->Texture2DArray.MostDetailedMip     = info->baseMipLevel;
      srv->Texture2DArray.MipLevels           = info->mipLevelCount;
      srv->Texture2DArray.FirstArraySlice     = info->baseArrayLayer;
      srv->Texture2DArray.ArraySize           = info->arrayLayerCount;
      srv->Texture2DArray.PlaneSlice          = 0u;
      srv->Texture2DArray.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_CUBE:
      if (info->arrayLayerCount != 6u) {
        return false;
      }
      srv->ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURECUBE;
      srv->TextureCube.MostDetailedMip     = info->baseMipLevel;
      srv->TextureCube.MipLevels           = info->mipLevelCount;
      srv->TextureCube.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_CUBE_ARRAY:
      if (info->baseArrayLayer % 6u != 0u ||
          info->arrayLayerCount % 6u != 0u) {
        return false;
      }
      srv->ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
      srv->TextureCubeArray.MostDetailedMip = info->baseMipLevel;
      srv->TextureCubeArray.MipLevels       = info->mipLevelCount;
      srv->TextureCubeArray.First2DArrayFace = info->baseArrayLayer;
      srv->TextureCubeArray.NumCubes        = info->arrayLayerCount / 6u;
      srv->TextureCubeArray.ResourceMinLODClamp = 0.0f;
      return true;
    case GPU_TEXTURE_VIEW_3D:
      srv->ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE3D;
      srv->Texture3D.MostDetailedMip     = info->baseMipLevel;
      srv->Texture3D.MipLevels           = info->mipLevelCount;
      srv->Texture3D.ResourceMinLODClamp = 0.0f;
      return true;
    default:
      return false;
  }
}

static bool
dx12__fillTextureRtv(const GPUTextureViewCreateInfo *info,
                     DXGI_FORMAT                     format,
                     bool                            multisampled,
                     D3D12_RENDER_TARGET_VIEW_DESC  *rtv) {
  if (!info || !rtv || info->mipLevelCount != 1u) {
    return false;
  }

  memset(rtv, 0, sizeof(*rtv));
  rtv->Format = format;
  switch (info->viewType) {
    case GPU_TEXTURE_VIEW_2D:
      rtv->ViewDimension = multisampled
                             ? D3D12_RTV_DIMENSION_TEXTURE2DMS
                             : D3D12_RTV_DIMENSION_TEXTURE2D;
      if (!multisampled) {
        rtv->Texture2D.MipSlice   = info->baseMipLevel;
        rtv->Texture2D.PlaneSlice = 0u;
      }
      return info->arrayLayerCount == 1u;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      rtv->ViewDimension                         =
        D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
      rtv->Texture2DArray.MipSlice               = info->baseMipLevel;
      rtv->Texture2DArray.FirstArraySlice        = info->baseArrayLayer;
      rtv->Texture2DArray.ArraySize              = info->arrayLayerCount;
      rtv->Texture2DArray.PlaneSlice             = 0u;
      return true;
    default:
      return false;
  }
}

static bool
dx12__fillTextureUav(const GPUTextureViewCreateInfo   *info,
                     DXGI_FORMAT                       format,
                     D3D12_UNORDERED_ACCESS_VIEW_DESC *uav) {
  if (!info || !uav || info->mipLevelCount != 1u) {
    return false;
  }

  memset(uav, 0, sizeof(*uav));
  uav->Format = format;
  switch (info->viewType) {
    case GPU_TEXTURE_VIEW_1D:
      uav->ViewDimension       = D3D12_UAV_DIMENSION_TEXTURE1D;
      uav->Texture1D.MipSlice  = info->baseMipLevel;
      return info->arrayLayerCount == 1u;
    case GPU_TEXTURE_VIEW_1D_ARRAY:
      uav->ViewDimension                  = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
      uav->Texture1DArray.MipSlice        = info->baseMipLevel;
      uav->Texture1DArray.FirstArraySlice = info->baseArrayLayer;
      uav->Texture1DArray.ArraySize       = info->arrayLayerCount;
      return true;
    case GPU_TEXTURE_VIEW_2D:
      uav->ViewDimension         = D3D12_UAV_DIMENSION_TEXTURE2D;
      uav->Texture2D.MipSlice    = info->baseMipLevel;
      uav->Texture2D.PlaneSlice  = 0u;
      return info->arrayLayerCount == 1u;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      uav->ViewDimension                         =
        D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
      uav->Texture2DArray.MipSlice               = info->baseMipLevel;
      uav->Texture2DArray.FirstArraySlice        = info->baseArrayLayer;
      uav->Texture2DArray.ArraySize              = info->arrayLayerCount;
      uav->Texture2DArray.PlaneSlice             = 0u;
      return true;
    case GPU_TEXTURE_VIEW_3D:
      uav->ViewDimension         = D3D12_UAV_DIMENSION_TEXTURE3D;
      uav->Texture3D.MipSlice    = info->baseMipLevel;
      uav->Texture3D.FirstWSlice = 0u;
      uav->Texture3D.WSize       = UINT32_MAX;
      return true;
    default:
      return false;
  }
}

static bool
dx12__fillTextureDsv(const GPUTextureViewCreateInfo *info,
                     DXGI_FORMAT                     format,
                     bool                            multisampled,
                     D3D12_DEPTH_STENCIL_VIEW_DESC  *dsv) {
  if (!info || !dsv || info->mipLevelCount != 1u) {
    return false;
  }

  memset(dsv, 0, sizeof(*dsv));
  dsv->Format = format;
  switch (info->viewType) {
    case GPU_TEXTURE_VIEW_2D:
      dsv->ViewDimension = multisampled
                             ? D3D12_DSV_DIMENSION_TEXTURE2DMS
                             : D3D12_DSV_DIMENSION_TEXTURE2D;
      if (!multisampled) {
        dsv->Texture2D.MipSlice = info->baseMipLevel;
      }
      return info->arrayLayerCount == 1u;
    case GPU_TEXTURE_VIEW_2D_ARRAY:
      dsv->ViewDimension                   = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
      dsv->Texture2DArray.MipSlice         = info->baseMipLevel;
      dsv->Texture2DArray.FirstArraySlice  = info->baseArrayLayer;
      dsv->Texture2DArray.ArraySize        = info->arrayLayerCount;
      return true;
    default:
      return false;
  }
}

GPU_HIDE
GPUResult
dx12_createTextureView(GPUTexture                     * __restrict texture,
                       const GPUTextureViewCreateInfo * __restrict info,
                       GPUTextureView                ** __restrict outView) {
  GPUDeviceDX12      *device;
  GPUTextureDX12     *textureDX12;
  GPUTextureView     *view;
  GPUTextureViewDX12 *native;
  D3D12_SHADER_RESOURCE_VIEW_DESC  srv = {0};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {0};
  D3D12_RENDER_TARGET_VIEW_DESC    rtv = {0};
  D3D12_DEPTH_STENCIL_VIEW_DESC    dsv = {0};
  DXGI_FORMAT                      format;
  GPUResult                        result;
  uint32_t                         rtvOffset;
  uint32_t                         dsvOffset;
  bool                             sampled;
  bool                             storage;
  bool                             colorTarget;
  bool                             depthTarget;
  bool                             shadingRate;
  bool                             hasRtv;
  bool                             hasDsv;
  bool                             hasUav;

  textureDX12 = texture ? texture->_priv : NULL;
  if (!textureDX12 || !textureDX12->resource || !info || !outView ||
      info->format != texture->format) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if ((info->viewType == GPU_TEXTURE_VIEW_1D &&
       (texture->dimension != GPU_TEXTURE_DIMENSION_1D ||
        texture->depthOrLayers != 1u || info->baseArrayLayer != 0u)) ||
      (info->viewType == GPU_TEXTURE_VIEW_1D_ARRAY &&
       (texture->dimension != GPU_TEXTURE_DIMENSION_1D ||
        texture->depthOrLayers <= 1u)) ||
      (info->viewType == GPU_TEXTURE_VIEW_2D &&
       (texture->dimension != GPU_TEXTURE_DIMENSION_2D ||
        texture->depthOrLayers != 1u || info->baseArrayLayer != 0u)) ||
      ((info->viewType == GPU_TEXTURE_VIEW_2D_ARRAY ||
        info->viewType == GPU_TEXTURE_VIEW_CUBE ||
        info->viewType == GPU_TEXTURE_VIEW_CUBE_ARRAY) &&
       texture->dimension != GPU_TEXTURE_DIMENSION_2D) ||
      (info->viewType == GPU_TEXTURE_VIEW_CUBE &&
       info->baseArrayLayer != 0u) ||
      (info->viewType == GPU_TEXTURE_VIEW_3D &&
       (texture->dimension != GPU_TEXTURE_DIMENSION_3D ||
        info->baseArrayLayer != 0u || info->arrayLayerCount != 1u))) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outView = NULL;
  format = dx12_format(info->format);
  if (format == DXGI_FORMAT_UNKNOWN) {
    return GPU_ERROR_UNSUPPORTED;
  }

  sampled     = (texture->usage & GPU_TEXTURE_USAGE_SAMPLED) != 0u;
  storage     = (texture->usage & GPU_TEXTURE_USAGE_STORAGE) != 0u;
  colorTarget = (texture->usage & GPU_TEXTURE_USAGE_COLOR_TARGET) != 0u;
  depthTarget = (texture->usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) != 0u;
  shadingRate =
    (texture->usage & GPU_TEXTURE_USAGE_SHADING_RATE_ATTACHMENT_EXT) != 0u;
  if (sampled) {
    srv.Format                  = dx12__textureSrvFormat(info->format);
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  }
  hasRtv = colorTarget && dx12__fillTextureRtv(info,
                                               format,
                                               texture->sampleCount > 1u,
                                               &rtv);
  hasDsv = depthTarget && dx12__fillTextureDsv(info,
                                               format,
                                               texture->sampleCount > 1u,
                                               &dsv);
  hasUav = storage && texture->sampleCount == 1u &&
           dx12__fillTextureUav(info, format, &uav);
  if ((!sampled && !hasRtv && !hasDsv && !hasUav && !shadingRate) ||
      (sampled &&
       !dx12__fillTextureSrv(info, texture->sampleCount > 1u, &srv)) ||
      (storage && !hasUav)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  view = calloc(1, sizeof(*view) + sizeof(*native));
  if (!view) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  device = texture->device ? texture->device->_priv : NULL;
  if (!device || !device->d3dDevice) {
    free(view);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  native = (GPUTextureViewDX12 *)(view + 1);
  native->device = device;
  if (sampled) {
    native->srv    = srv;
    native->hasSrv = true;
  }
  if (hasUav) {
    native->uav    = uav;
    native->hasUav = true;
  }
  if (hasRtv) {
    result = dx12_allocateDescriptors(device,
                                      D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                                      1u,
                                      &rtvOffset);
    if (result != GPU_OK) {
      free(view);
      return result;
    }
    native->device    = device;
    native->rtvOffset = rtvOffset;
    native->rtv       = dx12_cpuDescriptor(&device->rtvDescriptors, rtvOffset);
    if (native->rtv.ptr == 0u) {
      dx12_freeDescriptors(device,
                           D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                           rtvOffset,
                           1u);
      free(view);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    device->d3dDevice->lpVtbl->CreateRenderTargetView(device->d3dDevice,
                                                       textureDX12->resource,
                                                       &rtv,
                                                       native->rtv);
    native->hasRtv = true;
  }
  if (hasDsv) {
    result = dx12_allocateDescriptors(device,
                                      D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                                      1u,
                                      &dsvOffset);
    if (result != GPU_OK) {
      if (native->hasRtv) {
        dx12_freeDescriptors(device,
                             D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                             native->rtvOffset,
                             1u);
      }
      free(view);
      return result;
    }
    native->device    = device;
    native->dsvOffset = dsvOffset;
    native->dsv       = dx12_cpuDescriptor(&device->dsvDescriptors, dsvOffset);
    if (native->dsv.ptr == 0u) {
      dx12_freeDescriptors(device,
                           D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                           dsvOffset,
                           1u);
      if (native->hasRtv) {
        dx12_freeDescriptors(device,
                             D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                             native->rtvOffset,
                             1u);
      }
      free(view);
      return GPU_ERROR_BACKEND_FAILURE;
    }
    device->d3dDevice->lpVtbl->CreateDepthStencilView(device->d3dDevice,
                                                       textureDX12->resource,
                                                       &dsv,
                                                       native->dsv);
    native->hasDsv = true;
  }

  native->resource   = textureDX12->resource;
  native->state      = &textureDX12->state;
  native->texture    = textureDX12;
  native->width      = texture->width >> info->baseMipLevel;
  native->height     = texture->height >> info->baseMipLevel;
  if (native->width == 0u) {
    native->width = 1u;
  }
  if (native->height == 0u) {
    native->height = 1u;
  }
  native->baseMip    = info->baseMipLevel;
  native->mipCount   = info->mipLevelCount;
  native->baseLayer  = info->baseArrayLayer;
  native->layerCount = info->arrayLayerCount;
  view->_priv        = native;
  view->_ownsNative  = true;
  *outView           = view;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyTextureView(GPUTextureView * __restrict view) {
  GPUTextureViewDX12 *native;

  native = view ? view->_priv : NULL;
  if (native && native->device && native->hasRtv) {
    dx12_freeDescriptors(native->device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                         native->rtvOffset,
                         1u);
  }
  if (native && native->device && native->hasDsv) {
    dx12_freeDescriptors(native->device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                         native->dsvOffset,
                         1u);
  }
  free(view);
}

GPU_HIDE
GPUResult
dx12_writeTexture(GPUQueue             * __restrict queue,
                   GPUTexture                  * __restrict texture,
                   const GPUTextureWriteRegion * __restrict region,
                   const void                  * __restrict data,
                   uint64_t                                 sizeBytes) {
  GPUFormatDataLayout        dataLayout;
  GPUQueueDX12              *queueDX12;
  GPUDeviceDX12             *deviceDX12;
  GPUTextureDX12            *native;
  ID3D12GraphicsCommandList *commandList;
  ID3D12Resource            *upload;
  D3D12_RESOURCE_DESC        textureDesc;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {0};
  D3D12_BOX                   sourceBox = {0};
  D3D12_RESOURCE_STATES       finalState;
  void                       *mappedData;
  uint8_t                    *mapped;
  uint64_t                    rowSize;
  uint64_t                    layerSize;
  uint64_t                    layerStride;
  uint64_t                    stagingOffset;
  uint64_t                    totalSize;
  uint32_t                    copyCount;
  uint32_t                    copyDepth;
  uint32_t                    plane;
  uint32_t                    rowCount;
  uint32_t                    subresource;
  uint32_t                    transitionLayerCount;
  GPUResult                   result;
  bool                        texture3D;

  queueDX12  = queue ? queue->_priv : NULL;
  deviceDX12 = queue && queue->_device ? queue->_device->_priv : NULL;
  native     = texture ? texture->_priv : NULL;
  if (!queueDX12 || !queueDX12->commandQueue || !deviceDX12 || !native ||
      !native->resource ||
      !region || !data || sizeBytes > SIZE_MAX ||
      queueDX12->type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!dx12__textureWritePlane(texture->format,
                               region->aspect,
                               &plane) ||
      !gpuFormatAspectDataLayout(texture->format,
                                 region->aspect,
                                 region->width,
                                 region->height,
                                 region->depth,
                                 region->layerCount,
                                 region->bytesPerRow,
                                 region->rowsPerImage,
                                 &dataLayout)) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (dx12_combinedStencilPlane(texture->format, plane) &&
      !deviceDX12->stencilPlaneCopies) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (sizeBytes < dataLayout.requiredBytes) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  texture3D            = texture->dimension == GPU_TEXTURE_DIMENSION_3D;
  copyCount            = texture3D ? 1u : region->layerCount;
  copyDepth            = texture3D ? region->depth : 1u;
  transitionLayerCount = texture3D ? 1u : region->layerCount;
  native->resource->lpVtbl->GetDesc(native->resource, &textureDesc);
  subresource = dx12__textureSubresource(native,
                                         region->mipLevel,
                                         region->baseArrayLayer,
                                         plane);
  deviceDX12->d3dDevice->lpVtbl->GetCopyableFootprints(
    deviceDX12->d3dDevice,
    &textureDesc,
    subresource,
    1u,
    0u,
    &footprint,
    &rowCount,
    &rowSize,
    &layerSize
  );
  if (layerSize == 0u ||
      rowCount < dataLayout.blockRows ||
      rowSize < dataLayout.bytesInLastRow ||
      footprint.Footprint.Depth < copyDepth ||
      layerSize > UINT64_MAX -
                    (D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  layerStride =
    (layerSize + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u) &
    ~(uint64_t)(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u);
  if (copyCount > 1u &&
      layerStride > (UINT64_MAX - layerSize) / (copyCount - 1u)) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  totalSize = layerStride * (copyCount - 1u) + layerSize;
  if (totalSize > SIZE_MAX) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  result = dx12_beginTransfer(queue,
                              D3D12_HEAP_TYPE_UPLOAD,
                              totalSize,
                              GPU_DX12_TEXTURE_TRANSFER_CAPACITY,
                              &commandList,
                              &upload,
                              &mappedData,
                              &stagingOffset);
  if (result != GPU_OK) {
    return result;
  }
  mapped = mappedData;
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    uint64_t baseOffset;
    uint64_t ignoredSize;

    baseOffset  = stagingOffset + (uint64_t)layer * layerStride;
    subresource = dx12__textureSubresource(native,
                                           region->mipLevel,
                                           region->baseArrayLayer + layer,
                                           plane);
    deviceDX12->d3dDevice->lpVtbl->GetCopyableFootprints(
      deviceDX12->d3dDevice,
      &textureDesc,
      subresource,
      1u,
      baseOffset,
      &footprint,
      &rowCount,
      &rowSize,
      &ignoredSize
    );
    if (rowCount < dataLayout.blockRows ||
        rowSize < dataLayout.bytesInLastRow ||
        footprint.Footprint.Depth < copyDepth) {
      dx12_abortTransfer(queue);
      return GPU_ERROR_BACKEND_FAILURE;
    }

    for (uint32_t slice = 0u; slice < copyDepth; slice++) {
      uint32_t sourceImage;

      sourceImage = texture3D ? slice : layer;
      for (uint32_t row = 0u; row < dataLayout.blockRows; row++) {
        uint64_t destinationOffset;
        uint64_t sourceOffset;

        destinationOffset = footprint.Offset +
                            (uint64_t)slice *
                              footprint.Footprint.RowPitch * rowCount +
                            (uint64_t)row * footprint.Footprint.RowPitch;
        sourceOffset = (uint64_t)sourceImage * dataLayout.bytesPerImage +
                       (uint64_t)row * region->bytesPerRow;
        memcpy(mapped + (size_t)destinationOffset,
               (const uint8_t *)data + (size_t)sourceOffset,
               dataLayout.bytesInLastRow);
      }
    }
  }

  if (!dx12_transitionTexturePlane(commandList,
                                   native,
                                   region->mipLevel,
                                   1u,
                                   region->baseArrayLayer,
                                   transitionLayerCount,
                                   plane,
                                   D3D12_RESOURCE_STATE_COPY_DEST)) {
    dx12_abortTransfer(queue);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  sourceBox.right  = region->width;
  sourceBox.bottom = region->height;
  sourceBox.back   = copyDepth;
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_TEXTURE_COPY_LOCATION source      = {0};
    D3D12_TEXTURE_COPY_LOCATION destination = {0};
    uint64_t                    ignoredSize;

    subresource = dx12__textureSubresource(native,
                                           region->mipLevel,
                                           region->baseArrayLayer + layer,
                                           plane);
    deviceDX12->d3dDevice->lpVtbl->GetCopyableFootprints(
      deviceDX12->d3dDevice,
      &textureDesc,
      subresource,
      1u,
      stagingOffset + (uint64_t)layer * layerStride,
      &source.PlacedFootprint,
      &rowCount,
      &rowSize,
      &ignoredSize
    );
    source.pResource              = upload;
    source.Type                   = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.pResource        = native->resource;
    destination.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = subresource;
    commandList->lpVtbl->CopyTextureRegion(commandList,
                                           &destination,
                                           0u,
                                           0u,
                                           0u,
                                           &source,
                                           dx12__depthFormat(texture->format)
                                             ? NULL
                                             : &sourceBox);
  }
  finalState = dx12__textureFinalState(texture->usage);
  if (!dx12_transitionTexturePlane(commandList,
                                   native,
                                   region->mipLevel,
                                   1u,
                                   region->baseArrayLayer,
                                   transitionLayerCount,
                                   plane,
                                   finalState)) {
    dx12_abortTransfer(queue);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  return dx12_submitTransfer(queue, false);
}

GPU_HIDE
void
dx12_initTexture(GPUApiTexture *api) {
  api->create      = dx12_createTexture;
  api->destroy     = dx12_destroyTexture;
  api->createView  = dx12_createTextureView;
  api->destroyView = dx12_destroyTextureView;
  api->write       = dx12_writeTexture;
}
