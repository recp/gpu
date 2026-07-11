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
dx12__barrierSync(GPUPipelineStageMask stages) {
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
  return sync;
}

static D3D12_BARRIER_ACCESS
dx12__bufferBarrierAccess(const GPUBuffer       *buffer,
                          GPUAccessMask          access,
                          GPUPipelineStageMask   stages) {
  D3D12_BARRIER_ACCESS nativeAccess;

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
    if ((stages & GPU_STAGE_VERTEX) != 0u &&
        gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_VERTEX)) {
      nativeAccess |= D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
    } else if (gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_UNIFORM)) {
      nativeAccess |= D3D12_BARRIER_ACCESS_CONSTANT_BUFFER;
    } else {
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
    if ((stages & GPU_STAGE_VERTEX) != 0u &&
        gpuBufferHasUsage(buffer, GPU_BUFFER_USAGE_VERTEX)) {
      state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    } else {
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

    nativeBarrier.SyncBefore   = dx12__barrierSync(srcStages);
    nativeBarrier.SyncAfter    = dx12__barrierSync(dstStages);
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

GPU_HIDE
void
dx12_encodeBarriers(GPUCommandBuffer *cmdb, const GPUBarrierBatch *barriers) {
  GPUCommandBufferDX12 *command;

  command = cmdb ? cmdb->_priv : NULL;
  if (!command || !barriers) {
    return;
  }

  for (uint32_t i = 0u; i < barriers->bufferBarrierCount; i++) {
    dx12__encodeBufferBarrier(command,
                              &barriers->pBufferBarriers[i],
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
