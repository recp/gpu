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

#if GPU_DX12_HAS_SAMPLER_FEEDBACK

#if GPU_BUILD_WITH_DEBUG_MARKERS
static void
dx12_setSamplerFeedbackName(ID3D12Resource *resource, const char *label) {
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

static void
dx12_getSamplerFeedbackProperties(
  const GPUAdapter                 *adapter,
  GPUSamplerFeedbackPropertiesEXT *outProperties) {
  const GPUAdapterDX12 *native;

  native = adapter ? adapter->_priv : NULL;
  if (!outProperties) {
    return;
  }

  memset(outProperties, 0, sizeof(*outProperties));
  if (!native) {
    return;
  }
  if (native->samplerFeedbackTier >=
      (uint32_t)D3D12_SAMPLER_FEEDBACK_TIER_1_0) {
    outProperties->tier = GPU_SAMPLER_FEEDBACK_TIER_1_0_EXT;
  } else if (native->samplerFeedbackTier >=
             (uint32_t)D3D12_SAMPLER_FEEDBACK_TIER_0_9) {
    outProperties->tier = GPU_SAMPLER_FEEDBACK_TIER_0_9_EXT;
  }
}

static void
dx12_destroySamplerFeedbackState(GPUSamplerFeedbackMapDX12 *native) {
  if (!native) {
    return;
  }
  if (native->resource) {
    native->resource->lpVtbl->Release(native->resource);
  }
  if (native->device) {
    dx12_freeDescriptors(native->device,
                         D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                         native->descriptorOffset,
                         1u);
  }
  free(native);
}

static GPUResult
dx12_createSamplerFeedback(
  GPUDevice                                *device,
  const GPUSamplerFeedbackMapCreateInfoEXT *info,
  GPUSamplerFeedbackMapEXT                 *map) {
  GPUDeviceDX12              *deviceDX12;
  GPUTextureDX12             *target;
  GPUSamplerFeedbackMapDX12  *native;
  D3D12_HEAP_PROPERTIES       heapProperties = {0};
  D3D12_RESOURCE_DESC         targetDesc;
  D3D12_RESOURCE_DESC1        desc = {0};
  D3D12_CPU_DESCRIPTOR_HANDLE handle;
  GPUResult                   result;
  HRESULT                     nativeResult;

  deviceDX12 = device ? device->_priv : NULL;
  target     = info && info->texture ? info->texture->_priv : NULL;
  if (!deviceDX12 || !deviceDX12->d3dDevice8 ||
      deviceDX12->samplerFeedbackTier == 0u || !target ||
      !target->resource || !map) {
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->device = deviceDX12;
  result = dx12_allocateDescriptors(deviceDX12,
                                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                    1u,
                                    &native->descriptorOffset);
  if (result != GPU_OK) {
    free(native);
    return result;
  }

  target->resource->lpVtbl->GetDesc(target->resource, &targetDesc);
  heapProperties.Type                 = D3D12_HEAP_TYPE_DEFAULT;
  heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
  heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  heapProperties.CreationNodeMask     = 1u;
  heapProperties.VisibleNodeMask      = 1u;

  desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Alignment          = 0u;
  desc.Width              = targetDesc.Width;
  desc.Height             = targetDesc.Height;
  desc.DepthOrArraySize   = targetDesc.DepthOrArraySize;
  desc.MipLevels          = targetDesc.MipLevels;
  desc.Format             = info->mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT
                              ? DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE
                              : DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE;
  desc.SampleDesc.Count   = 1u;
  desc.SampleDesc.Quality = 0u;
  desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  desc.SamplerFeedbackMipRegion.Width  = info->mipRegionWidth;
  desc.SamplerFeedbackMipRegion.Height = info->mipRegionHeight;
  desc.SamplerFeedbackMipRegion.Depth  = 1u;

  nativeResult = deviceDX12->d3dDevice8->lpVtbl->CreateCommittedResource2(
    deviceDX12->d3dDevice8,
    &heapProperties,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    NULL,
    NULL,
    &IID_ID3D12Resource,
    (void **)&native->resource
  );
  if (FAILED(nativeResult) || !native->resource) {
    dx12_destroySamplerFeedbackState(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  native->state = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

  handle = dx12_cpuDescriptor(&deviceDX12->resourceDescriptors,
                              native->descriptorOffset);
  deviceDX12->d3dDevice8->lpVtbl
    ->CreateSamplerFeedbackUnorderedAccessView(deviceDX12->d3dDevice8,
                                               target->resource,
                                               native->resource,
                                               handle);
#if GPU_BUILD_WITH_DEBUG_MARKERS
  dx12_setSamplerFeedbackName(native->resource, info->label);
#endif
  map->_priv = native;
  return GPU_OK;
}

static void
dx12_destroySamplerFeedback(GPUSamplerFeedbackMapEXT *map) {
  GPUSamplerFeedbackMapDX12 *native;

  native = map ? map->_priv : NULL;
  if (map) {
    map->_priv = NULL;
  }
  dx12_destroySamplerFeedbackState(native);
}

static GPUResult
dx12_clearSamplerFeedback(GPUCommandBuffer         *cmdb,
                          GPUSamplerFeedbackMapEXT *map) {
  GPUCommandBufferDX12       *command;
  GPUSamplerFeedbackMapDX12 *native;
  ID3D12DescriptorHeap      *heaps[2];
  D3D12_CPU_DESCRIPTOR_HANDLE cpu;
  D3D12_GPU_DESCRIPTOR_HANDLE gpu;
  D3D12_RESOURCE_BARRIER      barrier = {0};
  UINT                        values[4] = {0};
  UINT                        heapCount;

  command = cmdb ? cmdb->_priv : NULL;
  native  = map ? map->_priv : NULL;
  if (!command || !command->commandList || !native || !native->device ||
      !native->resource || !native->device->resourceDescriptors.heap ||
      !dx12_transitionSamplerFeedback(
        command->commandList,
        native,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
      )) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  heapCount = 0u;
  heaps[heapCount++] = native->device->resourceDescriptors.heap;
  if (native->device->samplerDescriptors.heap) {
    heaps[heapCount++] = native->device->samplerDescriptors.heap;
  }
  command->commandList->lpVtbl->SetDescriptorHeaps(command->commandList,
                                                    heapCount,
                                                    heaps);
  cpu = dx12_cpuDescriptor(&native->device->resourceDescriptors,
                           native->descriptorOffset);
  gpu = dx12_gpuDescriptor(&native->device->resourceDescriptors,
                           native->descriptorOffset);
  command->commandList->lpVtbl->ClearUnorderedAccessViewUint(
    command->commandList,
    gpu,
    cpu,
    native->resource,
    values,
    0u,
    NULL
  );

  barrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  barrier.UAV.pResource = native->resource;
  command->commandList->lpVtbl->ResourceBarrier(command->commandList,
                                                  1u,
                                                  &barrier);
  return GPU_OK;
}

static GPUResult
dx12_transcodeSamplerFeedback(GPUCommandBuffer         *cmdb,
                              GPUSamplerFeedbackMapEXT *map,
                              GPUTexture               *decodedTexture,
                              bool                      encode) {
  GPUCommandBufferDX12       *command;
  GPUSamplerFeedbackMapDX12 *native;
  GPUTextureDX12            *decoded;
  ID3D12Resource            *source;
  ID3D12Resource            *destination;
  D3D12_RESOURCE_STATES      decodedState;
  D3D12_RESOURCE_STATES      feedbackState;
  D3D12_RESOLVE_MODE         resolveMode;
  uint32_t                   layerCount;
  uint32_t                   mipCount;

  command = cmdb ? cmdb->_priv : NULL;
  native  = map ? map->_priv : NULL;
  decoded = decodedTexture ? decodedTexture->_priv : NULL;
  if (!command || !command->commandList || !command->commandList1 ||
      !native || !native->resource || !decoded || !decoded->resource) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  decodedState  = encode ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE
                         : D3D12_RESOURCE_STATE_RESOLVE_DEST;
  feedbackState = encode ? D3D12_RESOURCE_STATE_RESOLVE_DEST
                         : D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
  if (!dx12_transitionTexture(command->commandList,
                              decoded,
                              0u,
                              decodedTexture->mipLevelCount,
                              0u,
                              gpuTextureArrayLayerCount(decodedTexture),
                              decodedState) ||
      !dx12_transitionSamplerFeedback(command->commandList,
                                      native,
                                      feedbackState)) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  source      = encode ? decoded->resource : native->resource;
  destination = encode ? native->resource : decoded->resource;
  resolveMode = encode ? D3D12_RESOLVE_MODE_ENCODE_SAMPLER_FEEDBACK
                       : D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK;
  layerCount  = map->decodeInfo.arrayLayerCount;
  mipCount    = map->decodeInfo.mipLevelCount;

  if (map->mode == GPU_SAMPLER_FEEDBACK_MIN_MIP_EXT) {
    for (uint32_t layer = 0u; layer < layerCount; layer++) {
      UINT sourceSubresource;
      UINT destinationSubresource;

      sourceSubresource      = encode ? layer : UINT_MAX;
      destinationSubresource = encode ? UINT_MAX : layer;
      command->commandList1->lpVtbl->ResolveSubresourceRegion(
        command->commandList1,
        destination,
        destinationSubresource,
        0u,
        0u,
        source,
        sourceSubresource,
        NULL,
        DXGI_FORMAT_R8_UINT,
        resolveMode
      );
    }
  } else {
    for (uint32_t layer = 0u; layer < layerCount; layer++) {
      for (uint32_t mip = 0u; mip < mipCount; mip++) {
        UINT subresource;

        subresource = mip + layer * mipCount;
        command->commandList1->lpVtbl->ResolveSubresourceRegion(
          command->commandList1,
          destination,
          subresource,
          0u,
          0u,
          source,
          subresource,
          NULL,
          DXGI_FORMAT_R8_UINT,
          resolveMode
        );
      }
    }
  }
  return GPU_OK;
}

static GPUResult
dx12_decodeSamplerFeedback(GPUCommandBuffer         *cmdb,
                           GPUSamplerFeedbackMapEXT *map,
                           GPUTexture               *decodedTexture) {
  return dx12_transcodeSamplerFeedback(cmdb, map, decodedTexture, false);
}

static GPUResult
dx12_encodeSamplerFeedback(GPUCommandBuffer         *cmdb,
                           GPUTexture               *decodedTexture,
                           GPUSamplerFeedbackMapEXT *map) {
  return dx12_transcodeSamplerFeedback(cmdb, map, decodedTexture, true);
}

#endif

GPU_HIDE
void
dx12_initSamplerFeedback(GPUApiSamplerFeedback *api) {
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
  api->getProperties = dx12_getSamplerFeedbackProperties;
  api->create        = dx12_createSamplerFeedback;
  api->destroy       = dx12_destroySamplerFeedback;
  api->clear         = dx12_clearSamplerFeedback;
  api->decode        = dx12_decodeSamplerFeedback;
  api->encode        = dx12_encodeSamplerFeedback;
#else
  GPU__UNUSED(api);
#endif
}
