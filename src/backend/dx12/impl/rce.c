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

static const D3D12_SHADING_RATE dx12_shadingRates[] = {
  [GPU_SHADING_RATE_1X1_EXT] = D3D12_SHADING_RATE_1X1,
  [GPU_SHADING_RATE_1X2_EXT] = D3D12_SHADING_RATE_1X2,
  [GPU_SHADING_RATE_2X1_EXT] = D3D12_SHADING_RATE_2X1,
  [GPU_SHADING_RATE_2X2_EXT] = D3D12_SHADING_RATE_2X2,
  [GPU_SHADING_RATE_2X4_EXT] = D3D12_SHADING_RATE_2X4,
  [GPU_SHADING_RATE_4X2_EXT] = D3D12_SHADING_RATE_4X2,
  [GPU_SHADING_RATE_4X4_EXT] = D3D12_SHADING_RATE_4X4
};

static const D3D12_SHADING_RATE_COMBINER dx12_shadingCombiners[] = {
  [GPU_SHADING_RATE_COMBINER_KEEP_EXT]    = D3D12_SHADING_RATE_COMBINER_PASSTHROUGH,
  [GPU_SHADING_RATE_COMBINER_REPLACE_EXT] = D3D12_SHADING_RATE_COMBINER_OVERRIDE,
  [GPU_SHADING_RATE_COMBINER_MIN_EXT]     = D3D12_SHADING_RATE_COMBINER_MAX,
  [GPU_SHADING_RATE_COMBINER_MAX_EXT]     = D3D12_SHADING_RATE_COMBINER_MIN
};

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
dx12__emitVertexBuffer(RenderEncoderDX12    *encoder, uint32_t index) {
  D3D12_VERTEX_BUFFER_VIEW view = {0};
  RenderPipelineDX12      *pipeline;
  GPUBuffer               *buffer;
  BufferDX12              *nativeBuffer;
  uint64_t                 offset;
  uint64_t                 remaining;

  pipeline = encoder ? encoder->pipeline : NULL;

  if (!encoder || !encoder->commandList || !pipeline
      || index >= pipeline->vertexBufferCount
      || index >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT) {
    return;
  }

  buffer       = encoder->vertexBuffers[index];
  offset       = encoder->vertexOffsets[index];
  nativeBuffer = buffer ? buffer->_priv : NULL;

  if (!buffer || !nativeBuffer || !nativeBuffer->resource
      || nativeBuffer->gpuAddress == 0u || offset >= buffer->sizeBytes
      || offset > UINT64_MAX - nativeBuffer->gpuAddress
      || !dx12_transitionBuffer(encoder->commandList,
                                nativeBuffer,
                                D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER)) {
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
dx12__bindIndexBuffer(GPURenderPassEncoder *encoder,
                      RenderEncoderDX12    *native,
                      uint32_t              firstIndex,
                      uint32_t              indexCount) {
  D3D12_INDEX_BUFFER_VIEW view = {0};
  GPUBuffer              *buffer;
  BufferDX12             *nativeBuffer;
  uint64_t                offset;
  uint64_t                remaining;
  uint64_t                firstByte;
  uint64_t                byteCount;
  uint64_t                indexSize;
  uint64_t                viewSize;

  buffer       = encoder ? encoder->_indexBuffer : NULL;
  nativeBuffer = buffer ? buffer->_priv : NULL;
  offset       = encoder ? encoder->_indexBufferOffset : 0u;
  indexSize    = encoder && encoder->_indexType == GPU_INDEX_TYPE_UINT32 ? 4u : 2u;

  if (!native || !native->commandList || !buffer || !nativeBuffer
      || !nativeBuffer->resource || nativeBuffer->gpuAddress == 0u
      || offset >= buffer->sizeBytes || (offset & (indexSize - 1u)) != 0u
      || offset > UINT64_MAX - nativeBuffer->gpuAddress
      || !dx12_transitionBuffer(native->commandList,
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

  if (native->indexBound && native->indexBuffer == buffer
      && native->indexOffset == offset
      && native->indexType == encoder->_indexType) {
    return true;
  }

  view.BufferLocation = nativeBuffer->gpuAddress + offset;
  view.SizeInBytes    = (UINT)viewSize;
  view.Format         = encoder->_indexType == GPU_INDEX_TYPE_UINT32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;

  native->commandList->lpVtbl->IASetIndexBuffer(native->commandList, &view);

  native->indexBuffer = buffer;
  native->indexOffset = offset;
  native->indexType   = encoder->_indexType;
  native->indexBound  = true;

  return true;
}

static void
dx12__transitionView(RenderEncoderDX12    *encoder,
                     TextureViewDX12      *view,
                     D3D12_RESOURCE_STATES nextState) {
  D3D12_TEXTURE_BARRIER  enhancedBarrier;
  D3D12_BARRIER_GROUP    group;
  D3D12_RESOURCE_BARRIER legacyBarrier;
  D3D12_RESOURCE_STATES  previousState;

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
    enhancedBarrier = (D3D12_TEXTURE_BARRIER){0};
    group           = (D3D12_BARRIER_GROUP){0};

    enhancedBarrier.SyncBefore = previousState == D3D12_RESOURCE_STATE_RENDER_TARGET
                                   ? D3D12_BARRIER_SYNC_RENDER_TARGET
                                   : D3D12_BARRIER_SYNC_NONE;
    enhancedBarrier.SyncAfter  = nextState == D3D12_RESOURCE_STATE_RENDER_TARGET
                                   ? D3D12_BARRIER_SYNC_RENDER_TARGET
                                   : D3D12_BARRIER_SYNC_NONE;

    enhancedBarrier.AccessBefore = previousState == D3D12_RESOURCE_STATE_RENDER_TARGET
                                     ? D3D12_BARRIER_ACCESS_RENDER_TARGET
                                     : D3D12_BARRIER_ACCESS_NO_ACCESS;
    enhancedBarrier.AccessAfter  = nextState == D3D12_RESOURCE_STATE_RENDER_TARGET
                                     ? D3D12_BARRIER_ACCESS_RENDER_TARGET
                                     : D3D12_BARRIER_ACCESS_NO_ACCESS;

    enhancedBarrier.LayoutBefore = previousState == D3D12_RESOURCE_STATE_PRESENT
                                     ? D3D12_BARRIER_LAYOUT_PRESENT
                                     : D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    enhancedBarrier.LayoutAfter  = nextState == D3D12_RESOURCE_STATE_PRESENT
                                     ? D3D12_BARRIER_LAYOUT_PRESENT
                                     : D3D12_BARRIER_LAYOUT_RENDER_TARGET;

    enhancedBarrier.pResource                         = view->resource;
    enhancedBarrier.Subresources.IndexOrFirstMipLevel = 0u;
    enhancedBarrier.Subresources.NumMipLevels         = 1u;
    enhancedBarrier.Subresources.FirstArraySlice      = 0u;
    enhancedBarrier.Subresources.NumArraySlices       = 1u;
    enhancedBarrier.Subresources.FirstPlane           = 0u;
    enhancedBarrier.Subresources.NumPlanes            = 1u;
    enhancedBarrier.Flags                             = D3D12_TEXTURE_BARRIER_FLAG_NONE;

    group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
    group.NumBarriers      = 1u;
    group.pTextureBarriers = &enhancedBarrier;

    encoder->commandList7->lpVtbl->Barrier(encoder->commandList7, 1u, &group);
  } else {
    legacyBarrier = (D3D12_RESOURCE_BARRIER){0};

    legacyBarrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    legacyBarrier.Transition.pResource   = view->resource;
    legacyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    legacyBarrier.Transition.StateBefore = previousState;
    legacyBarrier.Transition.StateAfter  = nextState;

    encoder->commandList->lpVtbl->ResourceBarrier(encoder->commandList,
                                                  1u,
                                                  &legacyBarrier);
  }

  *view->state = nextState;
}

static bool
dx12__setShadingRate(RenderEncoderDX12        *encoder,
                     GPUShadingRateEXT         rate,
                     GPUShadingRateCombinerEXT primitiveCombiner,
                     GPUShadingRateCombinerEXT attachmentCombiner) {
  D3D12_SHADING_RATE_COMBINER nativeCombiners[2];

  if (!encoder || !encoder->device || !encoder->commandList5) {
    return false;
  }

  nativeCombiners[0] = dx12_shadingCombiners[primitiveCombiner];
  nativeCombiners[1] = dx12_shadingCombiners[attachmentCombiner];

  encoder->commandList5->lpVtbl->RSSetShadingRate(encoder->commandList5,
                                                  dx12_shadingRates[rate],
                                                  nativeCombiners);

  return true;
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

static bool
dx12__drawIndirect(GPURenderPassEncoder *encoder,
                   GPUBuffer            *argsBuffer,
                   uint64_t              argsOffset,
                   uint32_t              drawCount,
                   uint32_t              strideBytes,
                   bool                  indexed) {
  RenderEncoderDX12      *native;
  BufferDX12             *buffer;
  ID3D12CommandSignature *signature;
  uint32_t                commandSize;

  native = encoder ? encoder->_priv : NULL;
  buffer = argsBuffer ? argsBuffer->_priv : NULL;

  if (!native || !native->device || !native->commandList || !buffer
      || !buffer->resource
      || !dx12_transitionBuffer(native->commandList,
                                buffer,
                                D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)) {
    return false;
  }

  signature   = indexed ? native->device->drawIndexedSignature : native->device->drawSignature;
  commandSize = indexed ? (uint32_t)sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
                  : (uint32_t)sizeof(D3D12_DRAW_ARGUMENTS);

  if (!signature || strideBytes != commandSize
      || (indexed && !dx12__bindIndexBuffer(encoder, native, 0u, 0u))) {
    return false;
  }

  native->commandList->lpVtbl->ExecuteIndirect(native->commandList,
                                               signature,
                                               drawCount,
                                               buffer->resource,
                                               argsOffset,
                                               NULL,
                                               0u);

  return true;
}

GPU_HIDE
GPURenderPassEncoder*
dx12_renderCommandEncoder(GPUCommandBuffer *cmdb, RenderPassDesc    *pass) {
  D3D12_CPU_DESCRIPTOR_HANDLE rtvs[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = {0};
  D3D12_VIEWPORT              viewport;
  D3D12_RECT                  scissor;
  DeviceDX12                 *device;
  CommandBufferDX12          *command;
  RenderPassDX12             *renderPass;
  GPURenderPassEncoder       *encoder;
  RenderEncoderDX12          *native;
  TextureViewDX12            *view;
  uint32_t                    viewIndex;
  uint32_t                    clearIndex;
  D3D12_CLEAR_FLAGS           flags;

  device     = cmdb && cmdb->_queue && cmdb->_queue->_device ? cmdb->_queue->_device->_priv : NULL;
  command    = cmdb ? cmdb->_priv : NULL;
  renderPass = pass ? pass->_priv : NULL;

  if (!device || !command || !command->commandList || !renderPass
      || (renderPass->colorCount == 0u && !renderPass->depthStencilView)) {
    return NULL;
  }

  encoder = &command->renderEncoder;
  native  = &command->renderState;
  memset(encoder, 0, sizeof(*encoder));
  memset(native, 0, sizeof(*native));

  native->device       = device;
  native->commandList  = command->commandList;
  native->commandList5 = command->commandList5;
  native->commandList6 = command->commandList6;
  native->commandList7 = command->commandList7;
  native->renderPass   = renderPass;

  native->debugEventActive = dx12_beginDebugEvent(commandBufferDevice(cmdb),
                                                  native->commandList,
                                                  pass->label);

  for (viewIndex = 0u; viewIndex < renderPass->colorCount; viewIndex++) {
    view            = renderPass->colorViews[viewIndex];
    rtvs[viewIndex] = view->rtv;

    dx12__transitionView(native, view, D3D12_RESOURCE_STATE_RENDER_TARGET);
  }

  if (renderPass->depthStencilView) {
    dsv = renderPass->depthStencilView->dsv;

    dx12__transitionView(native,
                         renderPass->depthStencilView,
                         D3D12_RESOURCE_STATE_DEPTH_WRITE);
  }

  if (native->commandList5) {
    (void)dx12__setShadingRate(native,
                               GPU_SHADING_RATE_1X1_EXT,
                               GPU_SHADING_RATE_COMBINER_KEEP_EXT,
                               renderPass->shadingRateView
                                 ? GPU_SHADING_RATE_COMBINER_REPLACE_EXT
                                 : GPU_SHADING_RATE_COMBINER_KEEP_EXT);

    if (renderPass->shadingRateView) {
      dx12__transitionView(native,
                           renderPass->shadingRateView,
                           D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
      native->commandList5->lpVtbl->RSSetShadingRateImage(native->commandList5,
                                                          renderPass->shadingRateView->resource);
    }
  }

  native->commandList->lpVtbl->OMSetRenderTargets(native->commandList,
                                                  renderPass->colorCount,
                                                  renderPass->colorCount > 0u ? rtvs : NULL,
                                                  FALSE,
                                                  renderPass->depthStencilView ? &dsv : NULL);

  for (clearIndex = 0u; clearIndex < renderPass->colorCount; clearIndex++) {
    if (renderPass->loadOps[clearIndex] == GPU_LOAD_OP_CLEAR) {
      native->commandList->lpVtbl->ClearRenderTargetView(native->commandList,
                                                         rtvs[clearIndex],
                                                         renderPass->clearColors[clearIndex],
                                                         0u,
                                                         NULL);
    }
  }

  if (renderPass->depthStencilView) {
    flags = (D3D12_CLEAR_FLAGS)0;

    if (renderPass->depthLoadOp == GPU_LOAD_OP_CLEAR) {
      flags |= D3D12_CLEAR_FLAG_DEPTH;
    }

    if (renderPass->depthHasStencil
        && renderPass->stencilLoadOp == GPU_LOAD_OP_CLEAR) {
      flags |= D3D12_CLEAR_FLAG_STENCIL;
    }

    if (flags != 0) {
      native->commandList->lpVtbl->ClearDepthStencilView(native->commandList,
                                                         dsv,
                                                         flags,
                                                         renderPass->clearDepth,
                                                         (UINT8)renderPass->clearStencil,
                                                         0u,
                                                         NULL);
    }
  }

  memset(&viewport, 0, sizeof(viewport));
  viewport.Width    = (float)renderPass->width;
  viewport.Height   = (float)renderPass->height;
  viewport.MaxDepth = 1.0f;

  scissor.left   = 0;
  scissor.top    = 0;
  scissor.right  = (LONG)renderPass->width;
  scissor.bottom = (LONG)renderPass->height;

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
dx12_setRenderPipelineState(GPURenderPassEncoder   *encoder,
                            RenderPipelineState    *pipelineState,
                            GPUCullMode             cullMode,
                            GPUFrontFace            frontFace) {
  RenderEncoderDX12     *native;
  RenderPipelineDX12    *pipeline;
  uint32_t               mask;
  bool                   rootChanged;

  GPU__UNUSED(cullMode);
  GPU__UNUSED(frontFace);

  native   = encoder ? encoder->_priv : NULL;
  pipeline = pipelineState ? pipelineState->_priv : NULL;

  if (!native || !native->commandList || !pipeline
      || !pipeline->pipelineState || !pipeline->rootSignature) {
    return;
  }

  rootChanged = native->rootSignature != pipeline->rootSignature;

  if (rootChanged) {
    native->commandList->lpVtbl->SetGraphicsRootSignature(native->commandList,
                                                          pipeline->rootSignature);
  }

  native->commandList->lpVtbl->SetPipelineState(native->commandList,
                                                pipeline->pipelineState);

  if (!pipeline->mesh) {
    native->commandList->lpVtbl->IASetPrimitiveTopology(native->commandList,
                                                        pipeline->topology);
  }

  native->rootSignature = pipeline->rootSignature;
  native->pipeline      = pipeline;

  if (rootChanged) {
    dx12_rebindRenderGroups(encoder);
  }

  for (mask = native->vertexBufferMask; mask != 0u; mask &= mask - 1u) {
    dx12__emitVertexBuffer(native, dx12__firstSetBit(mask));
  }
}

GPU_HIDE
void
dx12_vertexBuffer(GPURenderPassEncoder *encoder,
                  GPUBuffer            *buffer,
                  uint64_t              offset,
                  uint32_t              index) {
  RenderEncoderDX12    *native;

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
dx12_viewport(GPURenderPassEncoder *encoder, const GPUViewport *value) {
  D3D12_VIEWPORT        viewport;
  RenderEncoderDX12    *native;

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

GPU_HIDE
void
dx12_scissor(GPURenderPassEncoder *encoder, const GPUScissorRect *value) {
  D3D12_RECT            scissor;
  RenderEncoderDX12    *native;

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
dx12_blendConstant(GPURenderPassEncoder *encoder, const float rgba[4]) {
  RenderEncoderDX12    *native;

  native = encoder ? encoder->_priv : NULL;

  if (native && native->commandList && rgba) {
    native->commandList->lpVtbl->OMSetBlendFactor(native->commandList, rgba);
  }
}

GPU_HIDE
void
dx12_stencilReference(GPURenderPassEncoder *encoder, uint32_t reference) {
  RenderEncoderDX12    *native;

  native = encoder ? encoder->_priv : NULL;

  if (native && native->commandList) {
    native->commandList->lpVtbl->OMSetStencilRef(native->commandList, reference);
  }
}

GPU_HIDE
void
dx12_renderPushConstants(GPURenderPassEncoder *encoder,
                         GPUShaderStageFlags   stages,
                         const void           *data,
                         uint32_t              sizeBytes) {
  RenderEncoderDX12     *native;
  PipelineLayoutDX12    *layout;

  GPU__UNUSED(stages);

  native = encoder ? encoder->_priv : NULL;
  layout = encoder && encoder->_pipelineLayout ? encoder->_pipelineLayout->_native : NULL;

  if (!native || !native->commandList || !layout || !data
      || layout->pushConstantRootParameter == UINT32_MAX
      || sizeBytes != layout->pushConstantDwordCount * 4u) {
    return;
  }

  native->commandList->lpVtbl->SetGraphicsRoot32BitConstants(native->commandList,
                                                             layout->pushConstantRootParameter,
                                                             layout->pushConstantDwordCount,
                                                             data,
                                                             0u);
}

GPU_HIDE
void
dx12_drawPrimitives(GPURenderPassEncoder *encoder,
                    PrimitiveType         type,
                    size_t                start,
                    size_t                count,
                    uint32_t              instanceCount,
                    uint32_t              firstInstance) {
  RenderEncoderDX12    *native;

  GPU__UNUSED(type);

  native = encoder ? encoder->_priv : NULL;

  if (!native || !native->commandList || start > UINT32_MAX
      || count > UINT32_MAX) {
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
dx12_drawIndexedPrims(GPURenderPassEncoder *encoder,
                      uint32_t              indexCount,
                      uint32_t              instanceCount,
                      uint32_t              firstIndex,
                      int32_t               vertexOffset,
                      uint32_t              firstInstance) {
  RenderEncoderDX12    *native;

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
dx12_drawMesh(GPURenderPassEncoder *encoder,
              uint32_t              groupCountX,
              uint32_t              groupCountY,
              uint32_t              groupCountZ,
              const uint32_t        taskWorkgroupSize[3],
              const uint32_t        meshWorkgroupSize[3]) {
  RenderEncoderDX12    *native;

  GPU__UNUSED(taskWorkgroupSize);
  GPU__UNUSED(meshWorkgroupSize);

  native = encoder ? encoder->_priv : NULL;

  if (!native || !native->commandList6 || !native->pipeline
      || !native->pipeline->mesh) {
    return;
  }

  native->commandList6->lpVtbl->DispatchMesh(native->commandList6,
                                             groupCountX,
                                             groupCountY,
                                             groupCountZ);
}

GPU_HIDE
void
dx12_setFragmentShadingRate(GPURenderPassEncoder     *encoder,
                            GPUShadingRateEXT         rate,
                            GPUShadingRateCombinerEXT primitiveCombiner,
                            GPUShadingRateCombinerEXT attachmentCombiner) {
  RenderEncoderDX12    *native;

  native = encoder ? encoder->_priv : NULL;

  (void)dx12__setShadingRate(native,
                             rate,
                             primitiveCombiner,
                             attachmentCombiner);
}

GPU_HIDE
void
dx12_drawPrimitivesIndirect(GPURenderPassEncoder *encoder,
                            PrimitiveType         type,
                            GPUBuffer            *argsBuffer,
                            uint64_t              argsOffset) {
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
dx12_drawIndexedPrimsIndirect(GPURenderPassEncoder *encoder,
                              GPUBuffer            *argsBuffer,
                              uint64_t              argsOffset) {
  (void)dx12__drawIndirect(encoder,
                           argsBuffer,
                           argsOffset,
                           1u,
                           (uint32_t)sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
                           true);
}

GPU_HIDE
bool
dx12_multiDrawPrimitivesIndirect(GPURenderPassEncoder *encoder,
                                 PrimitiveType         type,
                                 GPUBuffer            *argsBuffer,
                                 uint64_t              argsOffset,
                                 uint32_t              drawCount,
                                 uint32_t              strideBytes) {
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
dx12_multiDrawIndexedPrimsIndirect(GPURenderPassEncoder *encoder,
                                   GPUBuffer            *argsBuffer,
                                   uint64_t              argsOffset,
                                   uint32_t              drawCount,
                                   uint32_t              strideBytes) {
  return dx12__drawIndirect(encoder,
                            argsBuffer,
                            argsOffset,
                            drawCount,
                            strideBytes,
                            true);
}

GPU_HIDE
void
dx12_endRenderEncoding(GPURenderPassEncoder *encoder) {
  RenderEncoderDX12    *native;
  RenderPassDX12       *renderPass;
  TextureViewDX12      *view;
  TextureViewDX12      *resolveView;
  uint32_t              i;

  native     = encoder ? encoder->_priv : NULL;
  renderPass = native ? native->renderPass : NULL;

  if (!native || !renderPass) {
    return;
  }

  for (i = 0u; i < renderPass->colorCount; i++) {
    view        = renderPass->colorViews[i];
    resolveView = renderPass->resolveViews[i];

    if (view && resolveView) {
      dx12__transitionView(native, view, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
      dx12__transitionView(native,
                           resolveView,
                           D3D12_RESOURCE_STATE_RESOLVE_DEST);
      native->commandList->lpVtbl->ResolveSubresource(native->commandList,
                                                      resolveView->resource,
                                                      resolveView->subresource,
                                                      view->resource,
                                                      view->subresource,
                                                      renderPass->resolveFormats[i]);
    }

    view = resolveView ? resolveView : view;

    if (view && view->swapchain) {
      dx12__transitionView(native, view, D3D12_RESOURCE_STATE_PRESENT);
    }
  }

  if (renderPass->shadingRateView && native->commandList5) {
    native->commandList5->lpVtbl->RSSetShadingRateImage(native->commandList5,
                                                        NULL);
  }

  if (native->debugEventActive) {
    dx12_endDebugEvent(commandBufferDevice(encoder->_cmdb),
                       native->commandList);
    native->debugEventActive = false;
  }
}

GPU_HIDE
void
dx12_initRCE(ApiRCE    *api) {
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
  api->setFragmentShadingRate   = dx12_setFragmentShadingRate;
  api->drawPrimitivesIndirect   = dx12_drawPrimitivesIndirect;
  api->drawIndexedPrimsIndirect = dx12_drawIndexedPrimsIndirect;

  api->multiDrawPrimitivesIndirect   = dx12_multiDrawPrimitivesIndirect;
  api->multiDrawIndexedPrimsIndirect = dx12_multiDrawIndexedPrimsIndirect;

  api->endEncoding = dx12_endRenderEncoding;
}
