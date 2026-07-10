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

static void
dx12__transitionView(GPURenderEncoderDX12 *encoder,
                     GPUTextureViewDX12   *view,
                     D3D12_RESOURCE_STATES nextState) {
  D3D12_RESOURCE_STATES previousState;

  if (!encoder || !encoder->commandList || !view || !view->resource ||
      !view->state || *view->state == nextState) {
    return;
  }

  previousState = *view->state;
  if (encoder->commandList7) {
    D3D12_TEXTURE_BARRIER barrier = {0};
    D3D12_BARRIER_GROUP   group = {0};

    barrier.SyncBefore = previousState == D3D12_RESOURCE_STATE_RENDER_TARGET
                           ? D3D12_BARRIER_SYNC_RENDER_TARGET
                           : D3D12_BARRIER_SYNC_NONE;
    barrier.SyncAfter  = nextState == D3D12_RESOURCE_STATE_RENDER_TARGET
                           ? D3D12_BARRIER_SYNC_RENDER_TARGET
                           : D3D12_BARRIER_SYNC_NONE;
    barrier.AccessBefore = previousState == D3D12_RESOURCE_STATE_RENDER_TARGET
                             ? D3D12_BARRIER_ACCESS_RENDER_TARGET
                             : D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.AccessAfter  = nextState == D3D12_RESOURCE_STATE_RENDER_TARGET
                             ? D3D12_BARRIER_ACCESS_RENDER_TARGET
                             : D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.LayoutBefore = previousState == D3D12_RESOURCE_STATE_PRESENT
                             ? D3D12_BARRIER_LAYOUT_PRESENT
                             : D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    barrier.LayoutAfter  = nextState == D3D12_RESOURCE_STATE_PRESENT
                             ? D3D12_BARRIER_LAYOUT_PRESENT
                             : D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    barrier.pResource                         = view->resource;
    barrier.Subresources.IndexOrFirstMipLevel = 0u;
    barrier.Subresources.NumMipLevels         = 1u;
    barrier.Subresources.FirstArraySlice      = 0u;
    barrier.Subresources.NumArraySlices       = 1u;
    barrier.Subresources.FirstPlane           = 0u;
    barrier.Subresources.NumPlanes            = 1u;
    barrier.Flags                              = D3D12_TEXTURE_BARRIER_FLAG_NONE;

    group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers      = 1u;
    group.pTextureBarriers = &barrier;
    encoder->commandList7->lpVtbl->Barrier(encoder->commandList7, 1u, &group);
  } else {
    D3D12_RESOURCE_BARRIER barrier = {0};

    barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = view->resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = previousState;
    barrier.Transition.StateAfter  = nextState;
    encoder->commandList->lpVtbl->ResourceBarrier(encoder->commandList,
                                                   1u,
                                                   &barrier);
  }

  *view->state = nextState;
}

GPU_HIDE
GPURenderCommandEncoder*
dx12_renderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  GPUCommandBufferDX12   *command;
  GPURenderPassDX12      *renderPass;
  GPURenderCommandEncoder *encoder;
  GPURenderEncoderDX12   *native;
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  D3D12_VIEWPORT          viewport;
  D3D12_RECT              scissor;

  command    = cmdb ? cmdb->_priv : NULL;
  renderPass = pass ? pass->_priv : NULL;
  if (!command || !command->commandList || !renderPass ||
      renderPass->colorCount == 0u) {
    return NULL;
  }

  encoder = &command->renderEncoder;
  native  = &command->renderState;
  memset(encoder, 0, sizeof(*encoder));
  memset(native, 0, sizeof(*native));
  native->commandList  = command->commandList;
  native->commandList7 = command->commandList7;
  native->renderPass   = renderPass;

  for (uint32_t i = 0u; i < renderPass->colorCount; i++) {
    GPUTextureViewDX12 *view;

    view    = renderPass->colorViews[i];
    rtvs[i] = view->rtv;
    dx12__transitionView(native, view, D3D12_RESOURCE_STATE_RENDER_TARGET);
  }

  native->commandList->lpVtbl->OMSetRenderTargets(native->commandList,
                                                   renderPass->colorCount,
                                                   rtvs,
                                                   FALSE,
                                                   NULL);
  for (uint32_t i = 0u; i < renderPass->colorCount; i++) {
    if (renderPass->loadOps[i] == GPU_LOAD_OP_CLEAR) {
      native->commandList->lpVtbl->ClearRenderTargetView(
        native->commandList,
        rtvs[i],
        renderPass->clearColors[i],
        0u,
        NULL
      );
    }
  }

  memset(&viewport, 0, sizeof(viewport));
  viewport.Width    = (float)renderPass->width;
  viewport.Height   = (float)renderPass->height;
  viewport.MaxDepth = 1.0f;
  scissor.left      = 0;
  scissor.top       = 0;
  scissor.right     = (LONG)renderPass->width;
  scissor.bottom    = (LONG)renderPass->height;
  native->commandList->lpVtbl->RSSetViewports(native->commandList,
                                               1u,
                                               &viewport);
  native->commandList->lpVtbl->RSSetScissorRects(native->commandList,
                                                  1u,
                                                  &scissor);

  encoder->_priv          = native;
  encoder->_primitiveType = GPUPrimitiveTypeTriangle;
  return encoder;
}

GPU_HIDE
void
dx12_setRenderPipelineState(GPURenderCommandEncoder *encoder,
                            GPURenderPipelineState  *pipelineState) {
  GPURenderEncoderDX12  *native;
  GPURenderPipelineDX12 *pipeline;

  native   = encoder ? encoder->_priv : NULL;
  pipeline = pipelineState ? pipelineState->_priv : NULL;
  if (!native || !native->commandList || !pipeline ||
      !pipeline->pipelineState || !pipeline->rootSignature) {
    return;
  }

  native->commandList->lpVtbl->SetGraphicsRootSignature(native->commandList,
                                                         pipeline->rootSignature);
  native->commandList->lpVtbl->SetPipelineState(native->commandList,
                                                 pipeline->pipelineState);
  native->commandList->lpVtbl->IASetPrimitiveTopology(native->commandList,
                                                       pipeline->topology);
}

GPU_HIDE
void
dx12_viewport(GPURenderCommandEncoder *encoder, const GPUViewport *value) {
  GPURenderEncoderDX12 *native;
  D3D12_VIEWPORT        viewport;

  native = encoder ? encoder->_priv : NULL;
  if (!native || !native->commandList || !value) {
    return;
  }

  viewport.TopLeftX = (float)value->originX;
  viewport.TopLeftY = (float)value->originY;
  viewport.Width    = (float)value->width;
  viewport.Height   = (float)value->height;
  viewport.MinDepth = (float)value->znear;
  viewport.MaxDepth = (float)value->zfar;
  native->commandList->lpVtbl->RSSetViewports(native->commandList,
                                               1u,
                                               &viewport);
}

GPU_HIDE
void
dx12_scissor(GPURenderCommandEncoder *encoder, const GPUScissorRect *value) {
  GPURenderEncoderDX12 *native;
  D3D12_RECT            scissor;

  native = encoder ? encoder->_priv : NULL;
  if (!native || !native->commandList || !value ||
      value->x > LONG_MAX || value->y > LONG_MAX ||
      value->width > (uint32_t)(LONG_MAX - (LONG)value->x) ||
      value->height > (uint32_t)(LONG_MAX - (LONG)value->y)) {
    return;
  }

  scissor.left   = (LONG)value->x;
  scissor.top    = (LONG)value->y;
  scissor.right  = scissor.left + (LONG)value->width;
  scissor.bottom = scissor.top + (LONG)value->height;
  native->commandList->lpVtbl->RSSetScissorRects(native->commandList,
                                                  1u,
                                                  &scissor);
}

GPU_HIDE
void
dx12_blendConstant(GPURenderCommandEncoder *encoder, const float rgba[4]) {
  GPURenderEncoderDX12 *native;

  native = encoder ? encoder->_priv : NULL;
  if (native && native->commandList && rgba) {
    native->commandList->lpVtbl->OMSetBlendFactor(native->commandList, rgba);
  }
}

GPU_HIDE
void
dx12_stencilReference(GPURenderCommandEncoder *encoder, uint32_t reference) {
  GPURenderEncoderDX12 *native;

  native = encoder ? encoder->_priv : NULL;
  if (native && native->commandList) {
    native->commandList->lpVtbl->OMSetStencilRef(native->commandList, reference);
  }
}

GPU_HIDE
void
dx12_drawPrimitives(GPURenderCommandEncoder *encoder,
                    GPUPrimitiveType         type,
                    size_t                   start,
                    size_t                   count,
                    uint32_t                 instanceCount,
                    uint32_t                 firstInstance) {
  GPURenderEncoderDX12 *native;

  GPU__UNUSED(type);

  native = encoder ? encoder->_priv : NULL;
  if (!native || !native->commandList || start > UINT32_MAX ||
      count > UINT32_MAX) {
    return;
  }

  native->commandList->lpVtbl->DrawInstanced(native->commandList,
                                              (UINT)count,
                                              instanceCount,
                                              (UINT)start,
                                              firstInstance);
}

GPU_HIDE
void
dx12_endRenderEncoding(GPURenderCommandEncoder *encoder) {
  GPURenderEncoderDX12 *native;
  GPURenderPassDX12    *renderPass;

  native     = encoder ? encoder->_priv : NULL;
  renderPass = native ? native->renderPass : NULL;
  if (!native || !renderPass) {
    return;
  }

  for (uint32_t i = 0u; i < renderPass->colorCount; i++) {
    GPUTextureViewDX12 *view;

    view = renderPass->colorViews[i];
    if (view && view->swapchain) {
      dx12__transitionView(native, view, D3D12_RESOURCE_STATE_PRESENT);
    }
  }
}

GPU_HIDE
void
dx12_initRCE(GPUApiRCE *api) {
  api->renderCommandEncoder   = dx12_renderCommandEncoder;
  api->setRenderPipelineState = dx12_setRenderPipelineState;
  api->viewport               = dx12_viewport;
  api->scissor                = dx12_scissor;
  api->blendConstant          = dx12_blendConstant;
  api->stencilReference       = dx12_stencilReference;
  api->drawPrimitives         = dx12_drawPrimitives;
  api->endEncoding            = dx12_endRenderEncoding;
}
