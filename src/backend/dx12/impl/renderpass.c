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

#include "../common.h"
#include "../impl.h"

static void
dx12__clearColor(float                    outColor[4],
                 const GPUClearColorValue *color,
                 GPUFormat                 format) {
  switch (gpuFormatNumericType(format)) {
    case GPU_FORMAT_NUMERIC_UINT:
      for (uint32_t i = 0u; i < 4u; i++) {
        outColor[i] = (float)color->uint32[i];
      }
      break;
    case GPU_FORMAT_NUMERIC_SINT:
      for (uint32_t i = 0u; i < 4u; i++) {
        outColor[i] = (float)color->sint32[i];
      }
      break;
    default:
      memcpy(outColor, color->float32, sizeof(color->float32));
      break;
  }
}

static D3D12_RESOURCE_STATES
dx12__bufferBarrierState(const GPUBuffer      *buffer,
                         GPUAccessMask         access,
                         GPUPipelineStageMask  stages) {
  D3D12_RESOURCE_STATES state;
  bool                  inputState;

  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u) {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u) {
    return D3D12_RESOURCE_STATE_COPY_DEST;
  }

  state = D3D12_RESOURCE_STATE_COMMON;
  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u) {
    state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  if ((access & GPU_ACCESS_INDIRECT_READ) != 0u) {
    state |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
  }
  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    inputState = false;
    if ((stages & GPU_STAGE_VERTEX) != 0u) {
      if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_VERTEX)) {
        state     |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        inputState = true;
      }
      if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_INDEX)) {
        state     |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
        inputState = true;
      }
    }
    if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_UNIFORM)) {
      state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
      inputState = true;
    }
    if (!inputState) {
      if ((stages & (GPU_STAGE_VERTEX | GPU_STAGE_COMPUTE)) != 0u) {
        state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
      }
      if ((stages & GPU_STAGE_FRAGMENT) != 0u) {
        state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      }
    }
  }
  return state;
}

static D3D12_RESOURCE_STATES
dx12__textureBarrierState(GPUAccessMask         access,
                          GPUPipelineStageMask  stages) {
  D3D12_RESOURCE_STATES state;

  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u) {
    return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if ((access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u) {
    return D3D12_RESOURCE_STATE_RENDER_TARGET;
  }
  if ((access & GPU_ACCESS_DEPTH_WRITE) != 0u) {
    return D3D12_RESOURCE_STATE_DEPTH_WRITE;
  }
  if ((access & GPU_ACCESS_DEPTH_READ) != 0u) {
    return D3D12_RESOURCE_STATE_DEPTH_READ;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u) {
    return D3D12_RESOURCE_STATE_COPY_DEST;
  }

  state = D3D12_RESOURCE_STATE_COMMON;
  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    if ((stages & (GPU_STAGE_VERTEX | GPU_STAGE_COMPUTE)) != 0u) {
      state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    if ((stages & GPU_STAGE_FRAGMENT) != 0u) {
      state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
    if ((stages & (GPU_STAGE_VERTEX |
                   GPU_STAGE_FRAGMENT |
                   GPU_STAGE_COMPUTE)) == 0u) {
      state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
  }
  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u) {
    state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  return state;
}

/* Legacy states remain canonical until enhanced layouts are tracked end-to-end. */
static void
dx12__encodeBufferBarrier(GPUCommandBufferDX12   *command,
                          const GPUBufferBarrier *barrier,
                          GPUPipelineStageMask    dstStages) {
  GPUBufferDX12          *buffer;
  D3D12_RESOURCE_BARRIER nativeBarrier = {0};
  D3D12_RESOURCE_STATES  nextState;

  buffer = barrier && barrier->buffer ? barrier->buffer->_priv : NULL;
  if (!command || !command->commandList || !buffer || !buffer->resource) {
    return;
  }

  nextState = dx12__bufferBarrierState(barrier->buffer,
                                      barrier->dstAccess,
                                      dstStages);
  if (buffer->state != nextState) {
    nativeBarrier.Type                   =
      D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    nativeBarrier.Transition.pResource   = buffer->resource;
    nativeBarrier.Transition.Subresource =
      D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    nativeBarrier.Transition.StateBefore = buffer->state;
    nativeBarrier.Transition.StateAfter  = nextState;
    command->commandList->lpVtbl->ResourceBarrier(command->commandList,
                                                   1u,
                                                   &nativeBarrier);
  } else if ((barrier->srcAccess & GPU_ACCESS_SHADER_WRITE) != 0u) {
    nativeBarrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    nativeBarrier.UAV.pResource = buffer->resource;
    command->commandList->lpVtbl->ResourceBarrier(command->commandList,
                                                   1u,
                                                   &nativeBarrier);
  }
  buffer->state = nextState;
}

static void
dx12__encodeTextureBarrier(GPUCommandBufferDX12    *command,
                           GPUDevice               *device,
                           const GPUTextureBarrier *barrier,
                           GPUPipelineStageMask     dstStages) {
  GPUTextureDX12        *texture;
  D3D12_RESOURCE_STATES  nextState;

  if (!command || !command->commandList || !barrier || !barrier->texture) {
    return;
  }
  if (!barrier->texture->_ownsNative) {
    gpuDeviceRecordValidationError(
      device,
      "Direct3D 12 barriers do not support swapchain textures"
    );
    return;
  }

  texture = barrier->texture->_priv;
  if (!texture || !texture->resource) {
    gpuDeviceRecordValidationError(
      device,
      "Direct3D 12 texture barrier has no compatible native texture"
    );
    return;
  }

  nextState = dx12__textureBarrierState(barrier->dstAccess, dstStages);
  if ((barrier->srcAccess & GPU_ACCESS_SHADER_WRITE) != 0u &&
      (barrier->dstAccess & GPU_ACCESS_SHADER_WRITE) != 0u) {
    D3D12_RESOURCE_BARRIER nativeBarrier = {0};

    nativeBarrier.Type          = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    nativeBarrier.UAV.pResource = texture->resource;
    command->commandList->lpVtbl->ResourceBarrier(command->commandList,
                                                   1u,
                                                   &nativeBarrier);
  }
  if (!dx12_transitionTexture(command->commandList,
                              texture,
                              barrier->baseMip,
                              barrier->mipCount,
                              barrier->baseLayer,
                              barrier->layerCount,
                              nextState)) {
    gpuDeviceRecordValidationError(
      device,
      "Direct3D 12 texture barrier transition failed"
    );
  }
}

GPU_HIDE
void
dx12_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  GPUCommandBufferDX12 *command;
  GPUDevice            *device;

  command = cmdb ? cmdb->_priv : NULL;
  device  = cmdb && cmdb->_queue ? cmdb->_queue->_device : NULL;
  if (!command || !device || !barriers) {
    return;
  }

  for (uint32_t i = 0u; i < barriers->bufferBarrierCount; i++) {
    dx12__encodeBufferBarrier(command,
                              &barriers->pBufferBarriers[i],
                              barriers->dstStages);
  }
  for (uint32_t i = 0u; i < barriers->textureBarrierCount; i++) {
    dx12__encodeTextureBarrier(command,
                               device,
                               &barriers->pTextureBarriers[i],
                               barriers->dstStages);
  }
}

GPU_HIDE
GPURenderPassDesc*
dx12_beginRenderPass(GPUCommandBuffer             *cmdb,
                     const GPURenderPassCreateInfo *info) {
  GPUCommandBufferDX12 *command;
  GPURenderPassDX12    *renderPass;
  GPURenderPassDesc    *desc;

  command = cmdb ? cmdb->_priv : NULL;
  if (!command || !command->commandList || !info ||
      info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS ||
      (info->colorAttachmentCount > 0u && !info->pColorAttachments) ||
      (info->colorAttachmentCount == 0u &&
       !info->pDepthStencilAttachment)) {
    return NULL;
  }

  renderPass = &command->renderPass;
  desc       = &command->renderPassDesc;
  memset(renderPass, 0, sizeof(*renderPass));
  memset(desc, 0, sizeof(*desc));

  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *attachment;
    GPUTextureViewDX12                 *view;
    GPUTextureViewDX12                 *resolveView;

    attachment = &info->pColorAttachments[i];
    view       = attachment->view ? attachment->view->_priv : NULL;
    resolveView = attachment->resolveView
                    ? attachment->resolveView->_priv
                    : NULL;
    if (!view || !view->resource || !view->state || !view->hasRtv ||
        view->width == 0u || view->height == 0u ||
        (i > 0u &&
         (view->width != renderPass->width ||
          view->height != renderPass->height))) {
      return NULL;
    }
    if (resolveView &&
        (!resolveView->resource || !resolveView->state ||
         resolveView->width != view->width ||
         resolveView->height != view->height)) {
      return NULL;
    }

    renderPass->colorViews[i]     = view;
    renderPass->resolveViews[i]   = resolveView;
    renderPass->resolveFormats[i] = dx12_format(attachment->view->format);
    renderPass->loadOps[i]        = attachment->loadOp;
    renderPass->storeOps[i]       = attachment->storeOp;
    dx12__clearColor(renderPass->clearColors[i],
                     &attachment->clearColor,
                     attachment->view->format);
    renderPass->width             = view->width;
    renderPass->height            = view->height;
  }

  if (info->pDepthStencilAttachment) {
    const GPURenderPassDepthStencilAttachment *attachment;
    GPUTextureViewDX12                       *view;

    attachment = info->pDepthStencilAttachment;
    view       = attachment->view ? attachment->view->_priv : NULL;
    if (!view || !view->resource || !view->state || !view->hasDsv ||
        view->width == 0u || view->height == 0u ||
        (renderPass->width > 0u &&
         (view->width != renderPass->width ||
          view->height != renderPass->height))) {
      return NULL;
    }

    renderPass->depthStencilView = view;
    renderPass->depthLoadOp      = attachment->depthLoadOp;
    renderPass->depthStoreOp     = attachment->depthStoreOp;
    renderPass->stencilLoadOp    = attachment->stencilLoadOp;
    renderPass->stencilStoreOp   = attachment->stencilStoreOp;
    renderPass->clearDepth       = attachment->clearDepth;
    renderPass->clearStencil     = attachment->clearStencil;
    renderPass->depthHasStencil  =
      attachment->view->format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
      attachment->view->format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
    renderPass->width            = view->width;
    renderPass->height           = view->height;
  }

  renderPass->colorCount = info->colorAttachmentCount;
  desc->_priv            = renderPass;
  desc->label            = info->label;
  return desc;
}

GPU_HIDE
void
dx12_destroyRenderPass(GPURenderPassDesc *pass) {
  GPU__UNUSED(pass);
}

static GPUCommandBufferDX12*
dx12__copyCommand(GPUCopyPassEncoder *pass) {
  return pass ? pass->_priv : NULL;
}

static void
dx12__copyError(GPUCopyPassEncoder *pass, const char *message) {
  GPUDevice *device;

  device = pass && pass->_cmdb && pass->_cmdb->_queue
             ? pass->_cmdb->_queue->_device
             : NULL;
  gpuDeviceRecordValidationError(device, message);
}

static bool
dx12__transitionCopyBuffer(GPUCommandBufferDX12 *command,
                           GPUBufferDX12        *buffer,
                           D3D12_RESOURCE_STATES state) {
  return command && dx12_transitionBuffer(command->commandList, buffer, state);
}

static bool
dx12__copyPlane(GPUFormat         format,
                GPUTextureAspect  aspect,
                uint32_t         *outPlane) {
  GPUTextureAspect resolved;

  if (!outPlane ||
      !gpuFormatResolveCopyAspect(format, aspect, &resolved)) {
    return false;
  }

  *outPlane = resolved == GPU_TEXTURE_ASPECT_STENCIL_ONLY &&
              (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
               format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8)
                ? 1u
                : 0u;
  return true;
}

static bool
dx12__copyWholeSubresource(GPUFormat format) {
  return format == GPU_FORMAT_DEPTH16_UNORM ||
         format == GPU_FORMAT_STENCIL8 ||
         format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 ||
         format == GPU_FORMAT_DEPTH32_FLOAT ||
         format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
}

static uint32_t
dx12__copySubresource(const GPUTextureDX12 *texture,
                      uint32_t               mip,
                      uint32_t               layer,
                      uint32_t               plane) {
  return mip + layer * texture->mipLevelCount +
         plane * texture->mipLevelCount * texture->arrayLayerCount;
}

static bool
dx12__alignCopyBytes(uint64_t value, uint64_t alignment, uint64_t *outValue) {
  if (!outValue || alignment == 0u ||
      (alignment & (alignment - 1u)) != 0u ||
      value > UINT64_MAX - (alignment - 1u)) {
    return false;
  }

  *outValue = (value + alignment - 1u) & ~(alignment - 1u);
  return true;
}

GPU_HIDE
void
dx12_resetCopyScratch(GPUCommandBufferDX12 *command) {
  GPUCopyScratchDX12 *scratch;

  for (scratch = command ? command->copyScratch : NULL;
       scratch;
       scratch = scratch->next) {
    scratch->offset = 0u;
  }
}

GPU_HIDE
void
dx12_destroyCopyScratch(GPUCommandBufferDX12 *command) {
  GPUCopyScratchDX12 *scratch;

  scratch = command ? command->copyScratch : NULL;
  while (scratch) {
    GPUCopyScratchDX12 *next;

    next = scratch->next;
    if (scratch->resource) {
      scratch->resource->lpVtbl->Release(scratch->resource);
    }
    free(scratch);
    scratch = next;
  }
  if (command) {
    command->copyScratch = NULL;
  }
}

static bool
dx12__reserveCopyScratch(GPUCommandBufferDX12  *command,
                         uint64_t                sizeBytes,
                         GPUCopyScratchDX12    **outScratch,
                         uint64_t               *outOffset) {
  GPUCopyScratchDX12 *scratch;
  GPUDevice          *device;
  GPUDeviceDX12      *deviceDX12;
  D3D12_HEAP_PROPERTIES heap = {0};
  D3D12_RESOURCE_DESC   desc = {0};
  uint64_t              alignedOffset;
  uint64_t              capacity;
  HRESULT               result;

  if (!command || !command->owner ||
      !(device = command->owner->queue
                  ? command->owner->queue->_device
                  : NULL) ||
      !(deviceDX12 = device->_priv) || !deviceDX12->d3dDevice ||
      sizeBytes == 0u || !outScratch || !outOffset) {
    return false;
  }

  for (scratch = command->copyScratch; scratch; scratch = scratch->next) {
    if (dx12__alignCopyBytes(scratch->offset,
                             D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
                             &alignedOffset) &&
        alignedOffset <= scratch->capacity &&
        sizeBytes <= scratch->capacity - alignedOffset) {
      scratch->offset = alignedOffset + sizeBytes;
      *outScratch     = scratch;
      *outOffset      = alignedOffset;
      return true;
    }
  }

  capacity = sizeBytes > GPU_DX12_COPY_SCRATCH_CAPACITY
               ? sizeBytes
               : GPU_DX12_COPY_SCRATCH_CAPACITY;
  if (!dx12__alignCopyBytes(capacity,
                            D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
                            &capacity) ||
      capacity > UINT64_MAX - sizeof(*scratch) ||
      !(scratch = calloc(1, sizeof(*scratch)))) {
    return false;
  }

  heap.Type             = D3D12_HEAP_TYPE_DEFAULT;
  heap.CreationNodeMask = 1u;
  heap.VisibleNodeMask  = 1u;
  desc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width            = capacity;
  desc.Height           = 1u;
  desc.DepthOrArraySize = 1u;
  desc.MipLevels        = 1u;
  desc.SampleDesc.Count = 1u;
  desc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  result = deviceDX12->d3dDevice->lpVtbl->CreateCommittedResource(
    deviceDX12->d3dDevice,
    &heap,
    D3D12_HEAP_FLAG_NONE,
    &desc,
    D3D12_RESOURCE_STATE_COMMON,
    NULL,
    &IID_ID3D12Resource,
    (void **)&scratch->resource
  );
  if (FAILED(result) || !scratch->resource) {
    free(scratch);
    return false;
  }

  scratch->capacity = capacity;
  scratch->offset   = sizeBytes;
  scratch->state    = D3D12_RESOURCE_STATE_COMMON;
  scratch->next     = command->copyScratch;
  command->copyScratch = scratch;
  gpuDeviceRecordHotPathAlloc(device, sizeof(*scratch) + capacity);
  *outScratch         = scratch;
  *outOffset          = 0u;
  return true;
}

static bool
dx12__transitionCopyScratch(GPUCommandBufferDX12 *command,
                            GPUCopyScratchDX12    *scratch,
                            D3D12_RESOURCE_STATES  state) {
  D3D12_RESOURCE_BARRIER barrier = {0};

  if (!command || !command->commandList || !scratch || !scratch->resource) {
    return false;
  }
  if (scratch->state == state) {
    return true;
  }

  barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource   = scratch->resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = scratch->state;
  barrier.Transition.StateAfter  = state;
  command->commandList->lpVtbl->ResourceBarrier(command->commandList,
                                                  1u,
                                                  &barrier);
  scratch->state = state;
  return true;
}

static bool
dx12__copyFootprintBase(
  const GPUTexture                   *texture,
  const GPUBufferTextureCopyRegion   *region,
  uint32_t                            layer,
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT *outFootprint,
  GPUFormatDataLayout                *outDataLayout) {
  GPUFormatDataLayout                   dataLayout;
  GPUDeviceDX12                        *device;
  GPUTextureDX12                       *native;
  D3D12_RESOURCE_DESC                   desc;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT    nativeFootprint;
  uint64_t                              footprintBytes;
  uint64_t                              rowSize;
  uint32_t                              rowCount;
  uint32_t                              plane;
  uint32_t                              subresource;

  if (!texture || !region || !outFootprint || !outDataLayout ||
      !texture->device ||
      !(device = texture->device->_priv) ||
      !(native = texture->_priv) || !native->resource ||
      !dx12__copyPlane(texture->format,
                       region->texture.texture.aspect,
                       &plane) ||
      !gpuFormatAspectDataLayout(texture->format,
                                 region->texture.texture.aspect,
                                 region->texture.width,
                                 region->texture.height,
                                 region->texture.depth,
                                 region->texture.layerCount,
                                 region->bytesPerRow,
                                 region->rowsPerImage,
                                 &dataLayout)) {
    return false;
  }

  native->resource->lpVtbl->GetDesc(native->resource, &desc);
  subresource = dx12__copySubresource(
    native,
    region->texture.texture.mipLevel,
    region->texture.texture.baseArrayLayer + layer,
    plane
  );
  device->d3dDevice->lpVtbl->GetCopyableFootprints(device->d3dDevice,
                                                    &desc,
                                                    subresource,
                                                    1u,
                                                    0u,
                                                    &nativeFootprint,
                                                    &rowCount,
                                                    &rowSize,
                                                    &footprintBytes);
  if (footprintBytes == 0u || rowCount < dataLayout.blockRows ||
      rowSize < dataLayout.bytesInLastRow ||
      nativeFootprint.Footprint.Format == DXGI_FORMAT_UNKNOWN) {
    return false;
  }

  *outFootprint  = nativeFootprint;
  *outDataLayout = dataLayout;
  return true;
}

static bool
dx12__copyFootprint(const GPUTexture                   *texture,
                    const GPUBufferTextureCopyRegion   *region,
                    uint32_t                            layer,
                    D3D12_PLACED_SUBRESOURCE_FOOTPRINT *outFootprint) {
  GPUFormatDataLayout dataLayout;
  uint64_t            offset;

  if (!dx12__copyFootprintBase(texture,
                               region,
                               layer,
                               outFootprint,
                               &dataLayout) ||
      region->bytesPerRow % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0u ||
      (dx12__copyWholeSubresource(texture->format) &&
       region->rowsPerImage > region->texture.height) ||
      (layer > 0u &&
       dataLayout.bytesPerImage >
         (UINT64_MAX - region->bufferOffset) / layer)) {
    return false;
  }

  offset = region->bufferOffset + dataLayout.bytesPerImage * layer;
  if (offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0u) {
    return false;
  }

  outFootprint->Offset             = offset;
  outFootprint->Footprint.Width    = region->texture.width;
  outFootprint->Footprint.Height   =
    texture->dimension == GPU_TEXTURE_DIMENSION_3D &&
    region->rowsPerImage > 0u
      ? region->rowsPerImage
      : region->texture.height;
  outFootprint->Footprint.Depth    =
    texture->dimension == GPU_TEXTURE_DIMENSION_3D
      ? region->texture.depth
      : 1u;
  outFootprint->Footprint.RowPitch = region->bytesPerRow;
  return true;
}

static bool
dx12__copyScratchLayout(
  const GPUTexture                   *texture,
  const GPUBufferTextureCopyRegion   *region,
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT *outFootprint,
  GPUFormatDataLayout                *outDataLayout,
  uint64_t                           *outImageStride,
  uint64_t                           *outLayerStride,
  uint64_t                           *outTotalBytes) {
  GPUFormatDataLayout dataLayout;
  uint64_t            imageStride;
  uint64_t            layerBytes;
  uint64_t            layerStride;
  uint64_t            rowPitch;
  uint64_t            totalBytes;
  uint32_t            copyCount;
  uint32_t            copyDepth;

  if (!outImageStride || !outLayerStride || !outTotalBytes ||
      !dx12__copyFootprintBase(texture,
                               region,
                               0u,
                               outFootprint,
                               &dataLayout) ||
      !dx12__alignCopyBytes(dataLayout.bytesInLastRow,
                            D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
                            &rowPitch) ||
      rowPitch > UINT32_MAX ||
      dataLayout.blockRows > UINT64_MAX / rowPitch) {
    return false;
  }

  copyDepth   = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                  ? region->texture.depth
                  : 1u;
  copyCount   = texture->dimension == GPU_TEXTURE_DIMENSION_3D
                  ? 1u
                  : region->texture.layerCount;
  imageStride = rowPitch * dataLayout.blockRows;
  if (copyDepth > UINT64_MAX / imageStride) {
    return false;
  }
  layerBytes = imageStride * copyDepth;
  if (!dx12__alignCopyBytes(layerBytes,
                            D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT,
                            &layerStride) ||
      (copyCount > 1u &&
       layerStride > (UINT64_MAX - layerBytes) / (copyCount - 1u))) {
    return false;
  }
  totalBytes = layerStride * (copyCount - 1u) + layerBytes;

  outFootprint->Footprint.Width    = region->texture.width;
  outFootprint->Footprint.Height   = region->texture.height;
  outFootprint->Footprint.Depth    = copyDepth;
  outFootprint->Footprint.RowPitch = (uint32_t)rowPitch;
  *outDataLayout  = dataLayout;
  *outImageStride = imageStride;
  *outLayerStride = layerStride;
  *outTotalBytes  = totalBytes;
  return true;
}

static bool
dx12__copyBufferToTextureScratch(
  GPUCommandBufferDX12             *command,
  GPUBufferDX12                    *srcBuffer,
  GPUTexture                       *dst,
  GPUTextureDX12                   *dstTexture,
  const GPUBufferTextureCopyRegion *region,
  uint32_t                          plane) {
  GPUCopyScratchDX12                *scratch;
  GPUFormatDataLayout                dataLayout;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  uint64_t                           scratchOffset;
  uint64_t                           imageStride;
  uint64_t                           layerStride;
  uint64_t                           totalBytes;
  bool                               texture3D;
  uint32_t                           copyCount;
  uint32_t                           copyDepth;

  texture3D = dst->dimension == GPU_TEXTURE_DIMENSION_3D;
  copyCount = texture3D ? 1u : region->texture.layerCount;
  copyDepth = texture3D ? region->texture.depth : 1u;
  if (!dx12__copyScratchLayout(dst,
                               region,
                               &footprint,
                               &dataLayout,
                               &imageStride,
                               &layerStride,
                               &totalBytes) ||
      !dx12__reserveCopyScratch(command,
                                totalBytes,
                                &scratch,
                                &scratchOffset) ||
      !dx12__transitionCopyBuffer(command,
                                  srcBuffer,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12__transitionCopyScratch(command,
                                   scratch,
                                   D3D12_RESOURCE_STATE_COPY_DEST)) {
    return false;
  }

  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    for (uint32_t slice = 0u; slice < copyDepth; slice++) {
      uint64_t sourceImage;
      uint64_t sourceBase;
      uint64_t scratchBase;

      sourceImage = texture3D ? slice : layer;
      sourceBase  = region->bufferOffset +
                    sourceImage * dataLayout.bytesPerImage;
      scratchBase = scratchOffset + layer * layerStride +
                    slice * imageStride;
      for (uint32_t row = 0u; row < dataLayout.blockRows; row++) {
        command->commandList->lpVtbl->CopyBufferRegion(
          command->commandList,
          scratch->resource,
          scratchBase + (uint64_t)row * footprint.Footprint.RowPitch,
          srcBuffer->resource,
          sourceBase + (uint64_t)row * region->bytesPerRow,
          dataLayout.bytesInLastRow
        );
      }
    }
  }

  if (!dx12__transitionCopyScratch(command,
                                   scratch,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12_transitionTexturePlane(command->commandList,
                                   dstTexture,
                                   region->texture.texture.mipLevel,
                                   1u,
                                   region->texture.texture.baseArrayLayer,
                                   texture3D ? 1u : region->texture.layerCount,
                                   plane,
                                   D3D12_RESOURCE_STATE_COPY_DEST)) {
    return false;
  }

  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_TEXTURE_COPY_LOCATION source      = {0};
    D3D12_TEXTURE_COPY_LOCATION destination = {0};

    footprint.Offset             = scratchOffset + layer * layerStride;
    source.pResource             = scratch->resource;
    source.Type                  = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint       = footprint;
    destination.pResource       = dstTexture->resource;
    destination.Type            = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = dx12__copySubresource(
      dstTexture,
      region->texture.texture.mipLevel,
      region->texture.texture.baseArrayLayer + layer,
      plane
    );
    command->commandList->lpVtbl->CopyTextureRegion(
      command->commandList,
      &destination,
      region->texture.texture.x,
      region->texture.texture.y,
      texture3D ? region->texture.texture.z : 0u,
      &source,
      NULL
    );
  }
  return true;
}

static bool
dx12__copyTextureToBufferScratch(
  GPUCommandBufferDX12             *command,
  GPUTexture                       *src,
  GPUTextureDX12                   *srcTexture,
  GPUBufferDX12                    *dstBuffer,
  const GPUBufferTextureCopyRegion *region,
  uint32_t                          plane) {
  GPUCopyScratchDX12                *scratch;
  GPUFormatDataLayout                dataLayout;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  D3D12_BOX                          sourceBox = {0};
  uint64_t                           scratchOffset;
  uint64_t                           imageStride;
  uint64_t                           layerStride;
  uint64_t                           totalBytes;
  bool                               texture3D;
  uint32_t                           copyCount;
  uint32_t                           copyDepth;

  texture3D = src->dimension == GPU_TEXTURE_DIMENSION_3D;
  copyCount = texture3D ? 1u : region->texture.layerCount;
  copyDepth = texture3D ? region->texture.depth : 1u;
  if (!dx12__copyScratchLayout(src,
                               region,
                               &footprint,
                               &dataLayout,
                               &imageStride,
                               &layerStride,
                               &totalBytes) ||
      !dx12__reserveCopyScratch(command,
                                totalBytes,
                                &scratch,
                                &scratchOffset) ||
      !dx12_transitionTexturePlane(command->commandList,
                                   srcTexture,
                                   region->texture.texture.mipLevel,
                                   1u,
                                   region->texture.texture.baseArrayLayer,
                                   texture3D ? 1u : region->texture.layerCount,
                                   plane,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12__transitionCopyScratch(command,
                                   scratch,
                                   D3D12_RESOURCE_STATE_COPY_DEST)) {
    return false;
  }

  sourceBox.left   = region->texture.texture.x;
  sourceBox.top    = region->texture.texture.y;
  sourceBox.front  = texture3D ? region->texture.texture.z : 0u;
  sourceBox.right  = sourceBox.left + region->texture.width;
  sourceBox.bottom = sourceBox.top + region->texture.height;
  sourceBox.back   = sourceBox.front + copyDepth;
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_TEXTURE_COPY_LOCATION source      = {0};
    D3D12_TEXTURE_COPY_LOCATION destination = {0};

    footprint.Offset               = scratchOffset + layer * layerStride;
    source.pResource               = srcTexture->resource;
    source.Type                    = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex        = dx12__copySubresource(
      srcTexture,
      region->texture.texture.mipLevel,
      region->texture.texture.baseArrayLayer + layer,
      plane
    );
    destination.pResource          = scratch->resource;
    destination.Type               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint    = footprint;
    command->commandList->lpVtbl->CopyTextureRegion(
      command->commandList,
      &destination,
      0u,
      0u,
      0u,
      &source,
      dx12__copyWholeSubresource(src->format) ? NULL : &sourceBox
    );
  }

  if (!dx12__transitionCopyScratch(command,
                                   scratch,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12__transitionCopyBuffer(command,
                                  dstBuffer,
                                  D3D12_RESOURCE_STATE_COPY_DEST)) {
    return false;
  }
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    for (uint32_t slice = 0u; slice < copyDepth; slice++) {
      uint64_t destinationImage;
      uint64_t destinationBase;
      uint64_t scratchBase;

      destinationImage = texture3D ? slice : layer;
      destinationBase  = region->bufferOffset +
                         destinationImage * dataLayout.bytesPerImage;
      scratchBase      = scratchOffset + layer * layerStride +
                         slice * imageStride;
      for (uint32_t row = 0u; row < dataLayout.blockRows; row++) {
        command->commandList->lpVtbl->CopyBufferRegion(
          command->commandList,
          dstBuffer->resource,
          destinationBase + (uint64_t)row * region->bytesPerRow,
          scratch->resource,
          scratchBase + (uint64_t)row * footprint.Footprint.RowPitch,
          dataLayout.bytesInLastRow
        );
      }
    }
  }
  return true;
}

static GPUCopyPassEncoder*
dx12_beginCopyPass(GPUCommandBuffer *cmdb, const char *label) {
  GPUCommandBufferDX12 *command;
  GPUCopyPassEncoder   *pass;

  command = cmdb ? cmdb->_priv : NULL;
  if (!command || !command->commandList) {
    return NULL;
  }

  pass = &command->copyEncoder;
  memset(pass, 0, sizeof(*pass));
  pass->_priv = command;
  command->copyDebugEventActive = dx12_beginDebugEvent(
    gpuCommandBufferDevice(cmdb),
    command->commandList,
    label
  );
  return pass;
}

static void
dx12_copyBufferToBuffer(GPUCopyPassEncoder        *pass,
                        GPUBuffer                 *src,
                        GPUBuffer                 *dst,
                        const GPUBufferCopyRegion *region) {
  GPUCommandBufferDX12 *command;
  GPUBufferDX12        *srcBuffer;
  GPUBufferDX12        *dstBuffer;

  command   = dx12__copyCommand(pass);
  srcBuffer = src ? src->_priv : NULL;
  dstBuffer = dst ? dst->_priv : NULL;
  if (!command || !srcBuffer || !dstBuffer || !region || src == dst ||
      !dx12__transitionCopyBuffer(command,
                                  srcBuffer,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12__transitionCopyBuffer(command,
                                  dstBuffer,
                                  D3D12_RESOURCE_STATE_COPY_DEST)) {
    dx12__copyError(pass, "Direct3D 12 buffer copy is not representable");
    return;
  }

  command->commandList->lpVtbl->CopyBufferRegion(command->commandList,
                                                  dstBuffer->resource,
                                                  region->dstOffset,
                                                  srcBuffer->resource,
                                                  region->srcOffset,
                                                  region->sizeBytes);
}

static void
dx12_copyBufferToTexture(GPUCopyPassEncoder               *pass,
                         GPUBuffer                        *src,
                         GPUTexture                       *dst,
                         const GPUBufferTextureCopyRegion *region) {
  GPUCommandBufferDX12 *command;
  GPUBufferDX12        *srcBuffer;
  GPUTextureDX12       *dstTexture;
  D3D12_BOX             sourceBox = {0};
  bool                  texture3D;
  bool                  directCopy;
  uint32_t              plane;
  uint32_t              copyCount;

  command    = dx12__copyCommand(pass);
  srcBuffer  = src ? src->_priv : NULL;
  dstTexture = dst ? dst->_priv : NULL;
  texture3D  = dst && dst->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (!command || !srcBuffer || !dstTexture || !region ||
      !dx12__copyPlane(dst->format,
                       region->texture.texture.aspect,
                       &plane)) {
    dx12__copyError(pass, "Direct3D 12 buffer-to-texture copy is invalid");
    return;
  }
  if (dx12_combinedStencilPlane(dst->format, plane) &&
      !dx12_stencilPlaneCopiesSupported(gpuCommandBufferDevice(pass->_cmdb))) {
    dx12__copyError(pass,
                    "Direct3D 12 stencil plane copies are unsupported");
    return;
  }

  copyCount = texture3D ? 1u : region->texture.layerCount;
  directCopy = true;
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;

    if (!dx12__copyFootprint(dst, region, layer, &footprint)) {
      directCopy = false;
      break;
    }
  }
  if (!directCopy) {
    if (!dx12__copyBufferToTextureScratch(command,
                                           srcBuffer,
                                           dst,
                                           dstTexture,
                                           region,
                                           plane)) {
      dx12__copyError(pass,
                      "Direct3D 12 buffer-to-texture layout is invalid");
    }
    return;
  }
  if (!dx12__transitionCopyBuffer(command,
                                  srcBuffer,
                                  D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12_transitionTexturePlane(command->commandList,
                                   dstTexture,
                                   region->texture.texture.mipLevel,
                                   1u,
                                   region->texture.texture.baseArrayLayer,
                                   texture3D ? 1u : region->texture.layerCount,
                                   plane,
                                   D3D12_RESOURCE_STATE_COPY_DEST)) {
    dx12__copyError(pass,
                    "Direct3D 12 buffer-to-texture copy transition failed");
    return;
  }

  sourceBox.right  = region->texture.width;
  sourceBox.bottom = region->texture.height;
  sourceBox.back   = texture3D ? region->texture.depth : 1u;
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_TEXTURE_COPY_LOCATION source      = {0};
    D3D12_TEXTURE_COPY_LOCATION destination = {0};

    if (!dx12__copyFootprint(dst, region, layer, &source.PlacedFootprint)) {
      dx12__copyError(pass,
                      "Direct3D 12 buffer-to-texture layout is invalid");
      return;
    }
    source.pResource        = srcBuffer->resource;
    source.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.pResource   = dstTexture->resource;
    destination.Type        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = dx12__copySubresource(
      dstTexture,
      region->texture.texture.mipLevel,
      region->texture.texture.baseArrayLayer + layer,
      plane
    );
    command->commandList->lpVtbl->CopyTextureRegion(
      command->commandList,
      &destination,
      region->texture.texture.x,
      region->texture.texture.y,
      texture3D ? region->texture.texture.z : 0u,
      &source,
      dx12__copyWholeSubresource(dst->format) ? NULL : &sourceBox
    );
  }
}

static void
dx12_copyTextureToBuffer(GPUCopyPassEncoder               *pass,
                         GPUTexture                       *src,
                         GPUBuffer                        *dst,
                         const GPUBufferTextureCopyRegion *region) {
  GPUCommandBufferDX12 *command;
  GPUTextureDX12       *srcTexture;
  GPUBufferDX12        *dstBuffer;
  D3D12_BOX             sourceBox = {0};
  bool                  texture3D;
  bool                  directCopy;
  uint32_t              plane;
  uint32_t              copyCount;

  command    = dx12__copyCommand(pass);
  srcTexture = src ? src->_priv : NULL;
  dstBuffer  = dst ? dst->_priv : NULL;
  texture3D  = src && src->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (!command || !srcTexture || !dstBuffer || !region ||
      !dx12__copyPlane(src->format,
                       region->texture.texture.aspect,
                       &plane)) {
    dx12__copyError(pass, "Direct3D 12 texture-to-buffer copy is invalid");
    return;
  }
  if (dx12_combinedStencilPlane(src->format, plane) &&
      !dx12_stencilPlaneCopiesSupported(gpuCommandBufferDevice(pass->_cmdb))) {
    dx12__copyError(pass,
                    "Direct3D 12 stencil plane copies are unsupported");
    return;
  }

  copyCount = texture3D ? 1u : region->texture.layerCount;
  directCopy = true;
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;

    if (!dx12__copyFootprint(src, region, layer, &footprint)) {
      directCopy = false;
      break;
    }
  }
  if (!directCopy) {
    if (!dx12__copyTextureToBufferScratch(command,
                                           src,
                                           srcTexture,
                                           dstBuffer,
                                           region,
                                           plane)) {
      dx12__copyError(pass,
                      "Direct3D 12 texture-to-buffer layout is invalid");
    }
    return;
  }
  if (!dx12_transitionTexturePlane(command->commandList,
                                   srcTexture,
                                   region->texture.texture.mipLevel,
                                   1u,
                                   region->texture.texture.baseArrayLayer,
                                   texture3D ? 1u : region->texture.layerCount,
                                   plane,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12__transitionCopyBuffer(command,
                                  dstBuffer,
                                  D3D12_RESOURCE_STATE_COPY_DEST)) {
    dx12__copyError(pass,
                    "Direct3D 12 texture-to-buffer copy transition failed");
    return;
  }

  sourceBox.left   = region->texture.texture.x;
  sourceBox.top    = region->texture.texture.y;
  sourceBox.front  = texture3D ? region->texture.texture.z : 0u;
  sourceBox.right  = sourceBox.left + region->texture.width;
  sourceBox.bottom = sourceBox.top + region->texture.height;
  sourceBox.back   = sourceBox.front +
                     (texture3D ? region->texture.depth : 1u);
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_TEXTURE_COPY_LOCATION source      = {0};
    D3D12_TEXTURE_COPY_LOCATION destination = {0};

    if (!dx12__copyFootprint(src,
                             region,
                             layer,
                             &destination.PlacedFootprint)) {
      dx12__copyError(pass,
                      "Direct3D 12 texture-to-buffer layout is invalid");
      return;
    }
    source.pResource   = srcTexture->resource;
    source.Type        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = dx12__copySubresource(
      srcTexture,
      region->texture.texture.mipLevel,
      region->texture.texture.baseArrayLayer + layer,
      plane
    );
    destination.pResource = dstBuffer->resource;
    destination.Type      = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    command->commandList->lpVtbl->CopyTextureRegion(command->commandList,
                                                     &destination,
                                                     0u,
                                                     0u,
                                                     0u,
                                                     &source,
                                                     dx12__copyWholeSubresource(
                                                       src->format
                                                     ) ? NULL : &sourceBox);
  }
}

static void
dx12_copyTextureToTexture(
  GPUCopyPassEncoder                  *pass,
  GPUTexture                          *src,
  GPUTexture                          *dst,
  const GPUTextureToTextureCopyRegion *region) {
  GPUCommandBufferDX12 *command;
  GPUTextureDX12       *srcTexture;
  GPUTextureDX12       *dstTexture;
  D3D12_BOX             sourceBox = {0};
  bool                  texture3D;
  uint32_t              srcPlane;
  uint32_t              dstPlane;
  uint32_t              copyCount;

  command    = dx12__copyCommand(pass);
  srcTexture = src ? src->_priv : NULL;
  dstTexture = dst ? dst->_priv : NULL;
  texture3D  = src && src->dimension == GPU_TEXTURE_DIMENSION_3D;
  if (!command || !srcTexture || !dstTexture || !region ||
      !dx12__copyPlane(src->format, region->src.aspect, &srcPlane) ||
      !dx12__copyPlane(dst->format, region->dst.aspect, &dstPlane) ||
      srcPlane != dstPlane) {
    dx12__copyError(pass, "Direct3D 12 texture copy is invalid");
    return;
  }
  if (dx12_combinedStencilPlane(src->format, srcPlane) &&
      !dx12_stencilPlaneCopiesSupported(gpuCommandBufferDevice(pass->_cmdb))) {
    dx12__copyError(pass,
                    "Direct3D 12 stencil plane copies are unsupported");
    return;
  }
  if (!dx12_transitionTexturePlane(command->commandList,
                                   srcTexture,
                                   region->src.mipLevel,
                                   1u,
                                   region->src.baseArrayLayer,
                                   texture3D ? 1u : region->layerCount,
                                   srcPlane,
                                   D3D12_RESOURCE_STATE_COPY_SOURCE) ||
      !dx12_transitionTexturePlane(command->commandList,
                                   dstTexture,
                                   region->dst.mipLevel,
                                   1u,
                                   region->dst.baseArrayLayer,
                                   texture3D ? 1u : region->layerCount,
                                   dstPlane,
                                   D3D12_RESOURCE_STATE_COPY_DEST)) {
    dx12__copyError(pass,
                    "Direct3D 12 texture-to-texture copy transition failed");
    return;
  }

  copyCount       = texture3D ? 1u : region->layerCount;
  sourceBox.left   = region->src.x;
  sourceBox.top    = region->src.y;
  sourceBox.front  = texture3D ? region->src.z : 0u;
  sourceBox.right  = sourceBox.left + region->width;
  sourceBox.bottom = sourceBox.top + region->height;
  sourceBox.back   = sourceBox.front + (texture3D ? region->depth : 1u);
  for (uint32_t layer = 0u; layer < copyCount; layer++) {
    D3D12_TEXTURE_COPY_LOCATION source      = {0};
    D3D12_TEXTURE_COPY_LOCATION destination = {0};

    source.pResource   = srcTexture->resource;
    source.Type        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    source.SubresourceIndex = dx12__copySubresource(
      srcTexture,
      region->src.mipLevel,
      region->src.baseArrayLayer + layer,
      srcPlane
    );
    destination.pResource   = dstTexture->resource;
    destination.Type        = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    destination.SubresourceIndex = dx12__copySubresource(
      dstTexture,
      region->dst.mipLevel,
      region->dst.baseArrayLayer + layer,
      dstPlane
    );
    command->commandList->lpVtbl->CopyTextureRegion(
      command->commandList,
      &destination,
      region->dst.x,
      region->dst.y,
      texture3D ? region->dst.z : 0u,
      &source,
      dx12__copyWholeSubresource(src->format) ? NULL : &sourceBox
    );
  }
}

static void
dx12_endCopyPass(GPUCopyPassEncoder *pass) {
  GPUCommandBufferDX12 *command;

  command = dx12__copyCommand(pass);
  if (command && command->copyDebugEventActive) {
    dx12_endDebugEvent(gpuCommandBufferDevice(pass->_cmdb),
                       command->commandList);
    command->copyDebugEventActive = false;
  }
}

GPU_HIDE
void
dx12_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass      = dx12_beginRenderPass;
  api->destroyRenderPass    = dx12_destroyRenderPass;
  api->beginCopyPass        = dx12_beginCopyPass;
  api->copyBufferToBuffer   = dx12_copyBufferToBuffer;
  api->copyBufferToTexture  = dx12_copyBufferToTexture;
  api->copyTextureToBuffer  = dx12_copyTextureToBuffer;
  api->copyTextureToTexture = dx12_copyTextureToTexture;
  api->endCopyPass          = dx12_endCopyPass;
  api->encodeBarriers       = dx12_encodeBarriers;
}
