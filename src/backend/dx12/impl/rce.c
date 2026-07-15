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

static uint32_t
dx12__firstSetBit(uint32_t mask) {
  uint32_t index;

  index = 0u;
  while ((mask & 1u) == 0u) {
    mask >>= 1u;
    index++;
  }
  return index;
}

static void
dx12__emitVertexBuffer(GPURenderEncoderDX12 *encoder, uint32_t index) {
  GPURenderPipelineDX12   *pipeline;
  GPUBuffer               *buffer;
  GPUBufferDX12           *nativeBuffer;
  D3D12_VERTEX_BUFFER_VIEW view = {0};
  uint64_t                 offset;
  uint64_t                 remaining;

  pipeline = encoder ? encoder->pipeline : NULL;
  if (!encoder || !encoder->commandList || !pipeline ||
      index >= pipeline->vertexBufferCount ||
      index >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
    return;
  }

  buffer       = encoder->vertexBuffers[index];
  offset       = encoder->vertexOffsets[index];
  nativeBuffer = buffer ? buffer->_priv : NULL;
  if (!buffer || !nativeBuffer || !nativeBuffer->resource ||
      nativeBuffer->gpuAddress == 0u || offset >= buffer->sizeBytes ||
      offset > UINT64_MAX - nativeBuffer->gpuAddress ||
      !dx12_transitionBuffer(
        encoder->commandList,
        nativeBuffer,
        D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
      )) {
    return;
  }

  remaining           = buffer->sizeBytes - offset;
  view.BufferLocation = nativeBuffer->gpuAddress + offset;
  view.SizeInBytes    = remaining > UINT_MAX ? UINT_MAX : (UINT)remaining;
  view.StrideInBytes  = pipeline->vertexStrides[index];
  encoder->commandList->lpVtbl->IASetVertexBuffers(encoder->commandList,
                                                   index,
                                                   1u,
                                                   &view);
}

static bool
dx12__bindIndexBuffer(GPURenderCommandEncoder *encoder,
                      GPURenderEncoderDX12     *native,
                      uint32_t                  firstIndex,
                      uint32_t                  indexCount) {
  GPUBuffer              *buffer;
  GPUBufferDX12          *nativeBuffer;
  D3D12_INDEX_BUFFER_VIEW view = {0};
  uint64_t                offset;
  uint64_t                remaining;
  uint64_t                firstByte;
  uint64_t                byteCount;
  uint64_t                indexSize;
  uint64_t                viewSize;

  buffer       = encoder ? encoder->_indexBuffer : NULL;
  nativeBuffer = buffer ? buffer->_priv : NULL;
  offset       = encoder ? encoder->_indexBufferOffset : 0u;
  indexSize    = encoder && encoder->_indexType == GPU_INDEX_TYPE_UINT32
                   ? 4u
                   : 2u;
  if (!native || !native->commandList || !buffer || !nativeBuffer ||
      !nativeBuffer->resource || nativeBuffer->gpuAddress == 0u ||
      offset >= buffer->sizeBytes || (offset & (indexSize - 1u)) != 0u ||
      offset > UINT64_MAX - nativeBuffer->gpuAddress ||
      !dx12_transitionBuffer(native->commandList,
                             nativeBuffer,
                             D3D12_RESOURCE_STATE_INDEX_BUFFER)) {
    return false;
  }

  remaining = buffer->sizeBytes - offset;
  viewSize  = remaining > UINT_MAX ? UINT_MAX : remaining;
  viewSize &= ~(indexSize - 1u);
  if (viewSize == 0u) {
    return false;
  }
  if (indexCount > 0u) {
    firstByte = (uint64_t)firstIndex * indexSize;
    byteCount = (uint64_t)indexCount * indexSize;
    if (firstByte > viewSize || byteCount > viewSize - firstByte) {
      return false;
    }
  }

  if (native->indexBound && native->indexBuffer == buffer &&
      native->indexOffset == offset &&
      native->indexType == encoder->_indexType) {
    return true;
  }

  view.BufferLocation = nativeBuffer->gpuAddress + offset;
  view.SizeInBytes    = (UINT)viewSize;
  view.Format         = encoder->_indexType == GPU_INDEX_TYPE_UINT32
                          ? DXGI_FORMAT_R32_UINT
                          : DXGI_FORMAT_R16_UINT;
  native->commandList->lpVtbl->IASetIndexBuffer(native->commandList, &view);
  native->indexBuffer = buffer;
  native->indexOffset = offset;
  native->indexType   = encoder->_indexType;
  native->indexBound  = true;
  return true;
}

static void
dx12__transitionView(GPURenderEncoderDX12 *encoder,
                     GPUTextureViewDX12   *view,
                     D3D12_RESOURCE_STATES nextState) {
  D3D12_RESOURCE_STATES previousState;

  if (!encoder || !encoder->commandList || !view || !view->resource) {
    return;
  }
  if (view->texture) {
    (void)dx12_transitionTexture(encoder->commandList,
                                 view->texture,
                                 view->baseMip,
                                 view->mipCount,
                                 view->baseLayer,
                                 view->layerCount,
                                 nextState);
    return;
  }
  if (!view->state || *view->state == nextState) {
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
  GPUDeviceDX12          *device;
  GPUCommandBufferDX12   *command;
  GPURenderPassDX12      *renderPass;
  GPURenderCommandEncoder *encoder;
  GPURenderEncoderDX12   *native;
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = {0};
  D3D12_VIEWPORT          viewport;
  D3D12_RECT              scissor;

  device     = cmdb && cmdb->_queue && cmdb->_queue->_device
                 ? cmdb->_queue->_device->_priv
                 : NULL;
  command    = cmdb ? cmdb->_priv : NULL;
  renderPass = pass ? pass->_priv : NULL;
  if (!device || !command || !command->commandList || !renderPass ||
      (renderPass->colorCount == 0u && !renderPass->depthStencilView)) {
    return NULL;
  }

  encoder = &command->renderEncoder;
  native  = &command->renderState;
  memset(encoder, 0, sizeof(*encoder));
  memset(native, 0, sizeof(*native));
  native->device           = device;
  native->commandList      = command->commandList;
  native->commandList6     = command->commandList6;
  native->commandList7     = command->commandList7;
  native->renderPass       = renderPass;
  native->debugEventActive = dx12_beginDebugEvent(
    gpuCommandBufferDevice(cmdb),
    native->commandList,
    pass->label
  );

  for (uint32_t i = 0u; i < renderPass->colorCount; i++) {
    GPUTextureViewDX12 *view;

    view    = renderPass->colorViews[i];
    rtvs[i] = view->rtv;
    dx12__transitionView(native, view, D3D12_RESOURCE_STATE_RENDER_TARGET);
  }
  if (renderPass->depthStencilView) {
    dsv = renderPass->depthStencilView->dsv;
    dx12__transitionView(native,
                         renderPass->depthStencilView,
                         D3D12_RESOURCE_STATE_DEPTH_WRITE);
  }

  native->commandList->lpVtbl->OMSetRenderTargets(native->commandList,
                                                   renderPass->colorCount,
                                                   renderPass->colorCount > 0u
                                                     ? rtvs
                                                     : NULL,
                                                   FALSE,
                                                   renderPass->depthStencilView
                                                     ? &dsv
                                                     : NULL);
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
  if (renderPass->depthStencilView) {
    D3D12_CLEAR_FLAGS flags;

    flags = (D3D12_CLEAR_FLAGS)0;
    if (renderPass->depthLoadOp == GPU_LOAD_OP_CLEAR) {
      flags |= D3D12_CLEAR_FLAG_DEPTH;
    }
    if (renderPass->depthHasStencil &&
        renderPass->stencilLoadOp == GPU_LOAD_OP_CLEAR) {
      flags |= D3D12_CLEAR_FLAG_STENCIL;
    }
    if (flags != 0) {
      native->commandList->lpVtbl->ClearDepthStencilView(
        native->commandList,
        dsv,
        flags,
        renderPass->clearDepth,
        (UINT8)renderPass->clearStencil,
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
  if (!pipeline->mesh) {
    native->commandList->lpVtbl->IASetPrimitiveTopology(native->commandList,
                                                         pipeline->topology);
  }
  native->rootSignature = pipeline->rootSignature;
  native->pipeline      = pipeline;

  for (uint32_t mask = native->vertexBufferMask;
       mask != 0u;
       mask &= mask - 1u) {
    dx12__emitVertexBuffer(native, dx12__firstSetBit(mask));
  }
}

GPU_HIDE
void
dx12_vertexBuffer(GPURenderCommandEncoder *encoder,
                  GPUBuffer               *buffer,
                  uint64_t                 offset,
                  uint32_t                 index) {
  GPURenderEncoderDX12 *native;

  native = encoder ? encoder->_priv : NULL;
  if (!native || !buffer || index >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
    return;
  }

  native->vertexBuffers[index] = buffer;
  native->vertexOffsets[index] = offset;
  native->vertexBufferMask    |= 1u << index;
  dx12__emitVertexBuffer(native, index);
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

  viewport.TopLeftX = value->x;
  viewport.TopLeftY = value->y;
  viewport.Width    = value->width;
  viewport.Height   = value->height;
  viewport.MinDepth = value->minDepth;
  viewport.MaxDepth = value->maxDepth;
  native->commandList->lpVtbl->RSSetViewports(native->commandList,
                                               1u,
                                               &viewport);
}

static void
dx12__scissorAxis(int32_t  origin,
                   uint32_t extent,
                   uint32_t limit,
                   LONG    *outStart,
                   LONG    *outEnd) {
  uint64_t clipped;
  uint32_t start;

  if (origin < 0) {
    clipped = (uint64_t)-(int64_t)origin;
    extent  = clipped >= extent ? 0u : extent - (uint32_t)clipped;
    origin  = 0;
  }

  start = (uint32_t)origin;
  if (start >= limit) {
    *outStart = (LONG)limit;
    *outEnd   = (LONG)limit;
    return;
  }
  if (extent > limit - start) {
    extent = limit - start;
  }

  *outStart = (LONG)start;
  *outEnd   = (LONG)(start + extent);
}

GPU_HIDE
void
dx12_scissor(GPURenderCommandEncoder *encoder, const GPUScissorRect *value) {
  GPURenderEncoderDX12 *native;
  D3D12_RECT            scissor;

  native = encoder ? encoder->_priv : NULL;
  if (!native || !native->commandList || !value) {
    return;
  }

  if (!native->renderPass) {
    return;
  }
  dx12__scissorAxis(value->x,
                    value->width,
                    native->renderPass->width,
                    &scissor.left,
                    &scissor.right);
  dx12__scissorAxis(value->y,
                    value->height,
                    native->renderPass->height,
                    &scissor.top,
                    &scissor.bottom);
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
dx12_renderPushConstants(GPURenderCommandEncoder *encoder,
                         GPUShaderStageFlags       stages,
                         const void               *data,
                         uint32_t                  sizeBytes) {
  GPURenderEncoderDX12  *native;
  GPUPipelineLayoutDX12 *layout;

  GPU__UNUSED(stages);

  native = encoder ? encoder->_priv : NULL;
  layout = encoder && encoder->_pipelineLayout
             ? encoder->_pipelineLayout->_native
             : NULL;
  if (!native || !native->commandList || !layout || !data ||
      layout->pushConstantRootParameter == UINT32_MAX ||
      sizeBytes != layout->pushConstantDwordCount * 4u) {
    return;
  }

  native->commandList->lpVtbl->SetGraphicsRoot32BitConstants(
    native->commandList,
    layout->pushConstantRootParameter,
    layout->pushConstantDwordCount,
    data,
    0u
  );
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
dx12_drawIndexedPrims(GPURenderCommandEncoder *encoder,
                      uint32_t                 indexCount,
                      uint32_t                 instanceCount,
                      uint32_t                 firstIndex,
                      int32_t                  vertexOffset,
                      uint32_t                 firstInstance) {
  GPURenderEncoderDX12 *native;

  native = encoder ? encoder->_priv : NULL;
  if (!dx12__bindIndexBuffer(encoder,
                             native,
                             firstIndex,
                             indexCount)) {
    return;
  }

  native->commandList->lpVtbl->DrawIndexedInstanced(native->commandList,
                                                     indexCount,
                                                     instanceCount,
                                                     firstIndex,
                                                     vertexOffset,
                                                     firstInstance);
}

GPU_HIDE
void
dx12_drawMesh(GPURenderCommandEncoder *encoder,
              uint32_t                 groupCountX,
              uint32_t                 groupCountY,
              uint32_t                 groupCountZ,
              const uint32_t           taskWorkgroupSize[3],
              const uint32_t           meshWorkgroupSize[3]) {
  GPURenderEncoderDX12 *native;

  GPU__UNUSED(taskWorkgroupSize);
  GPU__UNUSED(meshWorkgroupSize);

  native = encoder ? encoder->_priv : NULL;
  if (!native || !native->commandList6 || !native->pipeline ||
      !native->pipeline->mesh) {
    return;
  }
  native->commandList6->lpVtbl->DispatchMesh(native->commandList6,
                                              groupCountX,
                                              groupCountY,
                                              groupCountZ);
}

static bool
dx12__drawIndirect(GPURenderCommandEncoder *encoder,
                   GPUBuffer               *argsBuffer,
                   uint64_t                 argsOffset,
                   uint32_t                 drawCount,
                   uint32_t                 strideBytes,
                   bool                     indexed) {
  GPURenderEncoderDX12   *native;
  GPUBufferDX12          *buffer;
  ID3D12CommandSignature *signature;
  uint32_t                 commandSize;

  native = encoder ? encoder->_priv : NULL;
  buffer = argsBuffer ? argsBuffer->_priv : NULL;
  if (!native || !native->device || !native->commandList || !buffer ||
      !buffer->resource ||
      !dx12_transitionBuffer(native->commandList,
                             buffer,
                             D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)) {
    return false;
  }

  signature   = indexed ?
    native->device->drawIndexedSignature :
    native->device->drawSignature;
  commandSize = indexed ?
    (uint32_t)sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) :
    (uint32_t)sizeof(D3D12_DRAW_ARGUMENTS);
  if (!signature || strideBytes != commandSize ||
      (indexed && !dx12__bindIndexBuffer(encoder, native, 0u, 0u))) {
    return false;
  }

  native->commandList->lpVtbl->ExecuteIndirect(
    native->commandList,
    signature,
    drawCount,
    buffer->resource,
    argsOffset,
    NULL,
    0u
  );
  return true;
}

GPU_HIDE
void
dx12_drawPrimitivesIndirect(GPURenderCommandEncoder *encoder,
                            GPUPrimitiveType         type,
                            GPUBuffer               *argsBuffer,
                            uint64_t                 argsOffset) {
  GPU__UNUSED(type);

  (void)dx12__drawIndirect(encoder,
                           argsBuffer,
                           argsOffset,
                           1u,
                           (uint32_t)sizeof(D3D12_DRAW_ARGUMENTS),
                           false);
}

GPU_HIDE
void
dx12_drawIndexedPrimsIndirect(GPURenderCommandEncoder *encoder,
                              GPUBuffer               *argsBuffer,
                              uint64_t                 argsOffset) {
  (void)dx12__drawIndirect(encoder,
                           argsBuffer,
                           argsOffset,
                           1u,
                           (uint32_t)sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
                           true);
}

GPU_HIDE
bool
dx12_multiDrawPrimitivesIndirect(GPURenderCommandEncoder *encoder,
                                 GPUPrimitiveType         type,
                                 GPUBuffer               *argsBuffer,
                                 uint64_t                 argsOffset,
                                 uint32_t                 drawCount,
                                 uint32_t                 strideBytes) {
  GPU__UNUSED(type);

  return dx12__drawIndirect(encoder,
                            argsBuffer,
                            argsOffset,
                            drawCount,
                            strideBytes,
                            false);
}

GPU_HIDE
bool
dx12_multiDrawIndexedPrimsIndirect(GPURenderCommandEncoder *encoder,
                                   GPUBuffer               *argsBuffer,
                                   uint64_t                 argsOffset,
                                   uint32_t                 drawCount,
                                   uint32_t                 strideBytes) {
  return dx12__drawIndirect(encoder,
                            argsBuffer,
                            argsOffset,
                            drawCount,
                            strideBytes,
                            true);
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
    GPUTextureViewDX12 *resolveView;
    uint32_t             srcSubresource;
    uint32_t             dstSubresource;

    view        = renderPass->colorViews[i];
    resolveView = renderPass->resolveViews[i];
    if (view && resolveView) {
      dx12__transitionView(native, view, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      dx12__transitionView(native,
                           resolveView,
                           D3D12_RESOURCE_STATE_RESOLVE_DEST);
      srcSubresource = view->baseMip +
                       view->baseLayer * view->texture->mipLevelCount;
      dstSubresource = resolveView->baseMip +
                       resolveView->baseLayer *
                         resolveView->texture->mipLevelCount;
      native->commandList->lpVtbl->ResolveSubresource(
        native->commandList,
        resolveView->resource,
        dstSubresource,
        view->resource,
        srcSubresource,
        renderPass->resolveFormats[i]
      );
    }
    view = resolveView ? resolveView : view;
    if (view && view->swapchain) {
      dx12__transitionView(native, view, D3D12_RESOURCE_STATE_PRESENT);
    }
  }
  if (native->debugEventActive) {
    dx12_endDebugEvent(gpuCommandBufferDevice(encoder->_cmdb),
                       native->commandList);
    native->debugEventActive = false;
  }
}

GPU_HIDE
void
dx12_initRCE(GPUApiRCE *api) {
  api->renderCommandEncoder     = dx12_renderCommandEncoder;
  api->setRenderPipelineState   = dx12_setRenderPipelineState;
  api->viewport                 = dx12_viewport;
  api->scissor                  = dx12_scissor;
  api->blendConstant            = dx12_blendConstant;
  api->stencilReference         = dx12_stencilReference;
  api->pushConstants            = dx12_renderPushConstants;
  api->vertexBuffer             = dx12_vertexBuffer;
  api->vertexInputBuffer        = dx12_vertexBuffer;
  api->drawPrimitives           = dx12_drawPrimitives;
  api->drawIndexedPrims         = dx12_drawIndexedPrims;
  api->drawMesh                 = dx12_drawMesh;
  api->drawPrimitivesIndirect   = dx12_drawPrimitivesIndirect;
  api->drawIndexedPrimsIndirect = dx12_drawIndexedPrimsIndirect;

  api->multiDrawPrimitivesIndirect = dx12_multiDrawPrimitivesIndirect;
  api->multiDrawIndexedPrimsIndirect = dx12_multiDrawIndexedPrimsIndirect;

  api->endEncoding              = dx12_endRenderEncoding;
}
