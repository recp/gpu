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

static D3D12_BARRIER_SYNC
dx12__barrierSync(GPUPipelineStageMask stages, GPUAccessMask access) {
  D3D12_BARRIER_SYNC sync;

  if ((stages & (GPU_STAGE_TOP | GPU_STAGE_BOTTOM)) != 0u) {
    return D3D12_BARRIER_SYNC_ALL;
  }

  sync = D3D12_BARRIER_SYNC_NONE;
  if ((stages & GPU_STAGE_VERTEX) != 0u) {
    sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
  }
  if ((stages & GPU_STAGE_FRAGMENT) != 0u) {
    sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
  }
  if ((stages & GPU_STAGE_COMPUTE) != 0u) {
    sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
  }
  if ((stages & GPU_STAGE_TRANSFER) != 0u) {
    sync |= D3D12_BARRIER_SYNC_COPY;
  }
  if ((access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u) {
    sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
  }
  if ((access & (GPU_ACCESS_DEPTH_READ | GPU_ACCESS_DEPTH_WRITE)) != 0u) {
    sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
  }
  if ((access & GPU_ACCESS_INDIRECT_READ) != 0u) {
    sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
  }
  return sync;
}

static D3D12_BARRIER_SYNC
dx12__bufferBarrierSync(const GPUBuffer      *buffer,
                        GPUAccessMask         access,
                        GPUPipelineStageMask  stages) {
  D3D12_BARRIER_SYNC sync;

  sync = dx12__barrierSync(stages, access);
  if ((access & GPU_ACCESS_SHADER_READ) != 0u &&
      (stages & GPU_STAGE_VERTEX) != 0u &&
      gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_INDEX)) {
    sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
  }
  return sync;
}

static D3D12_BARRIER_ACCESS
dx12__bufferBarrierAccess(const GPUBuffer       *buffer,
                          GPUAccessMask          access,
                          GPUPipelineStageMask   stages) {
  D3D12_BARRIER_ACCESS nativeAccess;
  bool                 inputAccess;

  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u) {
    return D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
  }

  nativeAccess = D3D12_BARRIER_ACCESS_NO_ACCESS;
  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
  }
  if ((access & GPU_ACCESS_INDIRECT_READ) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT;
  }
  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    inputAccess = false;
    if ((stages & GPU_STAGE_VERTEX) != 0u) {
      if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_VERTEX)) {
        nativeAccess |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
        inputAccess   = true;
      }
      if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_INDEX)) {
        nativeAccess |= D3D12_BARRIER_ACCESS_INDEX_BUFFER;
        inputAccess   = true;
      }
    }
    if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_UNIFORM)) {
      nativeAccess |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
      inputAccess   = true;
    }
    if (!inputAccess) {
      nativeAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
    }
  }
  return nativeAccess;
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

static D3D12_BARRIER_ACCESS
dx12__textureBarrierAccess(GPUAccessMask access) {
  D3D12_BARRIER_ACCESS nativeAccess;

  nativeAccess = D3D12_BARRIER_ACCESS_NO_ACCESS;
  if ((access & GPU_ACCESS_SHADER_READ) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_SHADER_RESOURCE;
  }
  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
  }
  if ((access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_RENDER_TARGET;
  }
  if ((access & GPU_ACCESS_DEPTH_READ) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ;
  }
  if ((access & GPU_ACCESS_DEPTH_WRITE) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;
  }
  if ((access & GPU_ACCESS_TRANSFER_READ) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_COPY_SOURCE;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u) {
    nativeAccess |= D3D12_BARRIER_ACCESS_COPY_DEST;
  }
  return nativeAccess;
}

static D3D12_BARRIER_LAYOUT
dx12__textureBarrierLayout(GPUAccessMask access, bool source) {
  bool shaderRead;
  bool transferRead;

  if (access == GPU_ACCESS_NONE) {
    return source ? D3D12_BARRIER_LAYOUT_UNDEFINED
                  : D3D12_BARRIER_LAYOUT_COMMON;
  }
  if ((access & GPU_ACCESS_SHADER_WRITE) != 0u) {
    return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
  }
  if ((access & (GPU_ACCESS_COLOR_READ | GPU_ACCESS_COLOR_WRITE)) != 0u) {
    return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
  }
  if ((access & GPU_ACCESS_DEPTH_WRITE) != 0u) {
    return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
  }
  if ((access & GPU_ACCESS_DEPTH_READ) != 0u) {
    return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
  }
  if ((access & GPU_ACCESS_TRANSFER_WRITE) != 0u) {
    return D3D12_BARRIER_LAYOUT_COPY_DEST;
  }

  shaderRead   = (access & GPU_ACCESS_SHADER_READ) != 0u;
  transferRead = (access & GPU_ACCESS_TRANSFER_READ) != 0u;
  if (shaderRead && transferRead) {
    return D3D12_BARRIER_LAYOUT_GENERIC_READ;
  }
  if (shaderRead) {
    return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
  }
  if (transferRead) {
    return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
  }
  return D3D12_BARRIER_LAYOUT_COMMON;
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

static void
dx12__encodeBufferBarrier(GPUCommandBufferDX12   *command,
                          const GPUBufferBarrier *barrier,
                          GPUPipelineStageMask    srcStages,
                          GPUPipelineStageMask    dstStages) {
  GPUBufferDX12        *buffer;
  D3D12_RESOURCE_STATES nextState;

  buffer = barrier && barrier->buffer ? barrier->buffer->_priv : NULL;
  if (!command || !command->commandList || !buffer || !buffer->resource) {
    return;
  }

  nextState = dx12__bufferBarrierState(barrier->buffer,
                                      barrier->dstAccess,
                                      dstStages);
  if (command->commandList7) {
    D3D12_BUFFER_BARRIER nativeBarrier = {0};
    D3D12_BARRIER_GROUP  group = {0};

    nativeBarrier.SyncBefore   = dx12__bufferBarrierSync(barrier->buffer,
                                                         barrier->srcAccess,
                                                         srcStages);
    nativeBarrier.SyncAfter    = dx12__bufferBarrierSync(barrier->buffer,
                                                         barrier->dstAccess,
                                                         dstStages);
    nativeBarrier.AccessBefore = dx12__bufferBarrierAccess(barrier->buffer,
                                                           barrier->srcAccess,
                                                           srcStages);
    nativeBarrier.AccessAfter  = dx12__bufferBarrierAccess(barrier->buffer,
                                                           barrier->dstAccess,
                                                           dstStages);
    nativeBarrier.pResource    = buffer->resource;
    nativeBarrier.Offset       = barrier->offset;
    nativeBarrier.Size         = barrier->sizeBytes;
    group.Type                 = D3D12_BARRIER_TYPE_BUFFER;
    group.NumBarriers          = 1u;
    group.pBufferBarriers      = &nativeBarrier;
    command->commandList7->lpVtbl->Barrier(command->commandList7, 1u, &group);
  } else {
    D3D12_RESOURCE_BARRIER nativeBarrier = {0};

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
  }
  buffer->state = nextState;
}

static void
dx12__encodeTextureBarrier(GPUCommandBufferDX12    *command,
                           GPUDevice               *device,
                           const GPUTextureBarrier *barrier,
                           GPUPipelineStageMask     srcStages,
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
  if (command->commandList7) {
    D3D12_TEXTURE_BARRIER nativeBarrier = {0};
    D3D12_BARRIER_GROUP   group = {0};

    nativeBarrier.SyncBefore = dx12__barrierSync(srcStages,
                                                 barrier->srcAccess);
    nativeBarrier.SyncAfter  = dx12__barrierSync(dstStages,
                                                 barrier->dstAccess);
    nativeBarrier.AccessBefore = dx12__textureBarrierAccess(
      barrier->srcAccess
    );
    nativeBarrier.AccessAfter = dx12__textureBarrierAccess(
      barrier->dstAccess
    );
    nativeBarrier.LayoutBefore = dx12__textureBarrierLayout(
      barrier->srcAccess,
      true
    );
    nativeBarrier.LayoutAfter = dx12__textureBarrierLayout(
      barrier->dstAccess,
      false
    );
    nativeBarrier.pResource                         = texture->resource;
    nativeBarrier.Subresources.IndexOrFirstMipLevel = barrier->baseMip;
    nativeBarrier.Subresources.NumMipLevels         = barrier->mipCount;
    nativeBarrier.Subresources.FirstArraySlice      = barrier->baseLayer;
    nativeBarrier.Subresources.NumArraySlices       = barrier->layerCount;
    nativeBarrier.Subresources.FirstPlane           = 0u;
    nativeBarrier.Subresources.NumPlanes             = 1u;
    nativeBarrier.Flags                              =
      D3D12_TEXTURE_BARRIER_FLAG_NONE;
    group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers      = 1u;
    group.pTextureBarriers = &nativeBarrier;
    command->commandList7->lpVtbl->Barrier(command->commandList7,
                                            1u,
                                            &group);
    dx12_setTextureState(texture,
                         barrier->baseMip,
                         barrier->mipCount,
                         barrier->baseLayer,
                         barrier->layerCount,
                         nextState);
    return;
  }

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
                              barriers->srcStages,
                              barriers->dstStages);
  }
  for (uint32_t i = 0u; i < barriers->textureBarrierCount; i++) {
    dx12__encodeTextureBarrier(command,
                               device,
                               &barriers->pTextureBarriers[i],
                               barriers->srcStages,
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
      info->colorAttachmentCount == 0u ||
      info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS ||
      !info->pColorAttachments || info->pDepthStencilAttachment) {
    return NULL;
  }

  renderPass = &command->renderPass;
  desc       = &command->renderPassDesc;
  memset(renderPass, 0, sizeof(*renderPass));
  memset(desc, 0, sizeof(*desc));

  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *attachment;
    GPUTextureViewDX12                 *view;

    attachment = &info->pColorAttachments[i];
    view       = attachment->view ? attachment->view->_priv : NULL;
    if (!view || !view->resource || !view->state || attachment->resolveView ||
        view->width == 0u || view->height == 0u ||
        (i > 0u &&
         (view->width != renderPass->width ||
          view->height != renderPass->height))) {
      return NULL;
    }

    renderPass->colorViews[i]     = view;
    renderPass->loadOps[i]        = attachment->loadOp;
    renderPass->storeOps[i]       = attachment->storeOp;
    renderPass->clearColors[i][0] = attachment->clearColor.float32[0];
    renderPass->clearColors[i][1] = attachment->clearColor.float32[1];
    renderPass->clearColors[i][2] = attachment->clearColor.float32[2];
    renderPass->clearColors[i][3] = attachment->clearColor.float32[3];
    renderPass->width             = view->width;
    renderPass->height            = view->height;
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

GPU_HIDE
void
dx12_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass   = dx12_beginRenderPass;
  api->destroyRenderPass = dx12_destroyRenderPass;
  api->encodeBarriers    = dx12_encodeBarriers;
}
