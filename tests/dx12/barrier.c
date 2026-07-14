#include <gpu/gpu.h>

#include "../../src/backend/dx12/common.h"

#include <d3d12sdklayers.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  DX12_OFFSCREEN_WARM_ITERATIONS = 64u
};

static GPUAdapter*
first_adapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  uint32_t    count;
  GPUResult   result;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      count == 0u) {
    return NULL;
  }
  return adapter;
}

static void
texture_barrier(GPUCommandBuffer    *cmdb,
                GPUTexture          *texture,
                GPUAccessMask        srcAccess,
                GPUAccessMask        dstAccess,
                GPUPipelineStageMask srcStages,
                GPUPipelineStageMask dstStages,
                uint32_t             baseMip,
                uint32_t             mipCount,
                uint32_t             baseLayer,
                uint32_t             layerCount) {
  GPUTextureBarrier barrier = {0};
  GPUBarrierBatch   batch = {0};

  barrier.texture    = texture;
  barrier.srcAccess  = srcAccess;
  barrier.dstAccess  = dstAccess;
  barrier.baseMip    = baseMip;
  barrier.mipCount   = mipCount;
  barrier.baseLayer  = baseLayer;
  barrier.layerCount = layerCount;
  batch.srcStages           = srcStages;
  batch.dstStages           = dstStages;
  batch.textureBarrierCount = 1u;
  batch.pTextureBarriers     = &barrier;
  GPUEncodeBarriers(cmdb, &batch);
}

static bool
texture_states_are(const GPUTextureDX12     *texture,
                   D3D12_RESOURCE_STATES     expected) {
  if (!texture || !texture->states) {
    return false;
  }
  if (texture->stateUniform) {
    return texture->state == expected;
  }

  for (uint32_t i = 0u; i < texture->subresourceCount; i++) {
    if (texture->states[i] != expected) {
      return false;
    }
  }
  return true;
}

static bool
has_debug_errors(GPUDeviceDX12 *device) {
  ID3D12InfoQueue *infoQueue;
  D3D12_MESSAGE   *message;
  UINT64           messageCount;
  bool             foundError;

  infoQueue  = NULL;
  foundError = false;
  if (!device || !device->d3dDevice ||
      FAILED(device->d3dDevice->lpVtbl->QueryInterface(
        device->d3dDevice,
        &IID_ID3D12InfoQueue,
        (void **)&infoQueue
      ))) {
    return false;
  }

  messageCount = infoQueue->lpVtbl->GetNumStoredMessages(infoQueue);
  for (UINT64 i = 0u; i < messageCount; i++) {
    SIZE_T messageBytes;

    messageBytes = 0u;
    if (FAILED(infoQueue->lpVtbl->GetMessage(infoQueue,
                                             i,
                                             NULL,
                                             &messageBytes)) ||
        messageBytes == 0u || !(message = malloc(messageBytes))) {
      continue;
    }
    if (SUCCEEDED(infoQueue->lpVtbl->GetMessage(infoQueue,
                                                i,
                                                message,
                                                &messageBytes)) &&
        (message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION ||
         message->Severity == D3D12_MESSAGE_SEVERITY_ERROR)) {
      fprintf(stderr, "DX12 validation: %s\n", message->pDescription);
      foundError = true;
    }
    free(message);
  }
  infoQueue->lpVtbl->ClearStoredMessages(infoQueue);
  infoQueue->lpVtbl->Release(infoQueue);
  return foundError;
}

static bool
run_barrier_case(GPUAdapter *adapter, bool forceLegacy) {
  const uint8_t pixels[16] = {
    255u, 0u, 0u, 255u,
    0u, 255u, 0u, 255u,
    0u, 0u, 255u, 255u,
    255u, 255u, 255u, 255u
  };
  const uint64_t         vertexBufferSize   = 48u;
  const uint64_t         indexBufferSize    = 6u;
  const uint64_t         indirectBufferSize = 20u;
  GPUDevice             *device;
  GPUDeviceDX12         *deviceDX12;
  GPUQueue              *queue;
  GPUCommandBuffer      *cmdb;
  GPUCommandBuffer      *buffers[1];
  GPUCommandBufferDX12  *command;
  GPUBuffer             *vertexBuffer;
  GPUBuffer             *indexBuffer;
  GPUBuffer             *indirectBuffer;
  GPUBufferDX12         *nativeVertex;
  GPUBufferDX12         *nativeIndex;
  GPUBufferDX12         *nativeIndirect;
  GPUTexture            *texture;
  GPUTextureDX12        *native;
  GPUFence              *fence;
  GPUTextureCreateInfo   textureInfo = {0};
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUBufferBarrier       bufferBarriers[2] = {0};
  GPUBarrierBatch        bufferBatch = {0};
  GPUTextureWriteRegion  writeRegion = {0};
  GPUFenceCreateInfo     fenceInfo = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  bool                   ok;

  device         = GPUCreateDeviceWithDefaultQueues(adapter);
  queue          = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  vertexBuffer   = NULL;
  indexBuffer    = NULL;
  indirectBuffer = NULL;
  texture        = NULL;
  fence          = NULL;
  cmdb           = NULL;
  ok             = false;
  if (!device || !queue) {
    fprintf(stderr, "DX12 barrier device creation failed\n");
    goto cleanup;
  }

  deviceDX12 = device->_priv;
  if (!deviceDX12 || !deviceDX12->d3dDevice) {
    fprintf(stderr, "DX12 barrier native device missing\n");
    goto cleanup;
  }
  if (forceLegacy) {
    deviceDX12->enhancedBarriers = false;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = forceLegacy
                                  ? "dx12-vertex-barrier-legacy"
                                  : "dx12-vertex-barrier-enhanced";
  bufferInfo.sizeBytes        = vertexBufferSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_STORAGE;
  if (GPUCreateBuffer(device, &bufferInfo, &vertexBuffer) != GPU_OK ||
      !vertexBuffer) {
    fprintf(stderr, "DX12 vertex barrier buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label            = forceLegacy
                                  ? "dx12-index-barrier-legacy"
                                  : "dx12-index-barrier-enhanced";
  bufferInfo.sizeBytes        = indexBufferSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_INDEX |
                                GPU_BUFFER_USAGE_STORAGE;
  if (GPUCreateBuffer(device, &bufferInfo, &indexBuffer) != GPU_OK ||
      !indexBuffer) {
    fprintf(stderr, "DX12 index barrier buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = forceLegacy
                           ? "dx12-indirect-barrier-legacy"
                           : "dx12-indirect-barrier-enhanced";
  bufferInfo.sizeBytes = indirectBufferSize;
  bufferInfo.usage     = GPU_BUFFER_USAGE_INDIRECT |
                         GPU_BUFFER_USAGE_STORAGE;
  if (GPUCreateBuffer(device, &bufferInfo, &indirectBuffer) != GPU_OK ||
      !indirectBuffer) {
    fprintf(stderr, "DX12 indirect barrier buffer creation failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = forceLegacy
                                   ? "dx12-barrier-legacy"
                                   : "dx12-barrier-enhanced";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 2u;
  textureInfo.mipLevelCount    = 2u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK ||
      !texture) {
    fprintf(stderr, "DX12 barrier texture creation failed\n");
    goto cleanup;
  }

  writeRegion.width          = 2u;
  writeRegion.height         = 2u;
  writeRegion.depth          = 1u;
  writeRegion.mipLevel       = 1u;
  writeRegion.baseArrayLayer = 1u;
  writeRegion.layerCount     = 1u;
  writeRegion.bytesPerRow    = 8u;
  writeRegion.rowsPerImage   = 2u;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           pixels,
                           sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "DX12 barrier partial texture upload failed\n");
    goto cleanup;
  }

  native = texture->_priv;
  if (!native || native->subresourceCount != 4u || native->stateUniform ||
      native->states[3] != (D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)) {
    fprintf(stderr, "DX12 barrier initial subresource state mismatch\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              forceLegacy
                                ? "dx12-barrier-legacy"
                                : "dx12-barrier-enhanced",
                              &cmdb) != GPU_OK || !cmdb) {
    fprintf(stderr, "DX12 barrier command acquire failed\n");
    goto cleanup;
  }
  command = cmdb->_priv;
  if (!command || (forceLegacy && command->commandList7)) {
    fprintf(stderr,
            "DX12 barrier command path mismatch: legacy=%u enhanced=%u list7=%p\n",
            forceLegacy ? 1u : 0u,
            deviceDX12->enhancedBarriers ? 1u : 0u,
            (void *)command->commandList7);
    goto cleanup;
  }

  bufferBarriers[0].buffer    = vertexBuffer;
  bufferBarriers[0].srcAccess = GPU_ACCESS_SHADER_WRITE;
  bufferBarriers[0].dstAccess = GPU_ACCESS_SHADER_READ;
  bufferBarriers[0].sizeBytes = vertexBufferSize;
  bufferBarriers[1].buffer    = indirectBuffer;
  bufferBarriers[1].srcAccess = GPU_ACCESS_SHADER_WRITE;
  bufferBarriers[1].dstAccess = GPU_ACCESS_INDIRECT_READ;
  bufferBarriers[1].sizeBytes = indirectBufferSize;
  bufferBatch.srcStages       = GPU_STAGE_COMPUTE;
  bufferBatch.dstStages       = GPU_STAGE_VERTEX;
  bufferBatch.bufferBarrierCount = 2u;
  bufferBatch.pBufferBarriers     = bufferBarriers;
  GPUEncodeBarriers(cmdb, &bufferBatch);
  nativeVertex   = vertexBuffer->_priv;
  nativeIndirect = indirectBuffer->_priv;
  if (!nativeVertex ||
      nativeVertex->state != D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER ||
      !nativeIndirect ||
      nativeIndirect->state != D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT) {
    fprintf(stderr, "DX12 vertex/indirect barrier state mismatch\n");
    goto cleanup;
  }

  bufferBarriers[0].buffer    = indexBuffer;
  bufferBarriers[0].sizeBytes = indexBufferSize;
  bufferBatch.bufferBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &bufferBatch);
  nativeIndex = indexBuffer->_priv;
  if (!nativeIndex || nativeIndex->state != D3D12_RESOURCE_STATE_INDEX_BUFFER) {
    fprintf(stderr, "DX12 index barrier state mismatch\n");
    goto cleanup;
  }

  texture_barrier(cmdb,
                  texture,
                  GPU_ACCESS_SHADER_READ,
                  GPU_ACCESS_TRANSFER_WRITE,
                  GPU_STAGE_FRAGMENT,
                  GPU_STAGE_TRANSFER,
                  1u,
                  1u,
                  1u,
                  1u);
  if (!texture_states_are(native, D3D12_RESOURCE_STATE_COPY_DEST)) {
    fprintf(stderr, "DX12 barrier partial write transition mismatch\n");
    goto cleanup;
  }

  texture_barrier(cmdb,
                  texture,
                  GPU_ACCESS_TRANSFER_WRITE,
                  GPU_ACCESS_SHADER_READ,
                  GPU_STAGE_TRANSFER,
                  GPU_STAGE_FRAGMENT,
                  0u,
                  2u,
                  0u,
                  2u);
  if (!native->stateUniform ||
      !texture_states_are(native,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) {
    fprintf(stderr, "DX12 barrier full shader transition mismatch\n");
    goto cleanup;
  }

  texture_barrier(cmdb,
                  texture,
                  GPU_ACCESS_SHADER_READ,
                  GPU_ACCESS_TRANSFER_READ,
                  GPU_STAGE_FRAGMENT,
                  GPU_STAGE_TRANSFER,
                  0u,
                  1u,
                  0u,
                  1u);
  if (native->stateUniform ||
      native->states[0] != D3D12_RESOURCE_STATE_COPY_SOURCE) {
    fprintf(stderr, "DX12 barrier partial read transition mismatch\n");
    goto cleanup;
  }

  texture_barrier(cmdb,
                  texture,
                  GPU_ACCESS_TRANSFER_READ,
                  GPU_ACCESS_SHADER_READ,
                  GPU_STAGE_TRANSFER,
                  GPU_STAGE_FRAGMENT,
                  0u,
                  1u,
                  0u,
                  1u);
  texture_barrier(cmdb,
                  texture,
                  GPU_ACCESS_SHADER_READ,
                  GPU_ACCESS_TRANSFER_READ,
                  GPU_STAGE_FRAGMENT,
                  GPU_STAGE_TRANSFER,
                  0u,
                  2u,
                  0u,
                  2u);
  texture_barrier(cmdb,
                  texture,
                  GPU_ACCESS_TRANSFER_READ,
                  GPU_ACCESS_SHADER_READ,
                  GPU_STAGE_TRANSFER,
                  GPU_STAGE_FRAGMENT,
                  0u,
                  2u,
                  0u,
                  2u);
  if (!native->stateUniform ||
      !texture_states_are(native,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) {
    fprintf(stderr, "DX12 barrier final texture state mismatch\n");
    goto cleanup;
  }

  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "DX12 barrier fence creation failed\n");
    goto cleanup;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "DX12 barrier submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;
  if (has_debug_errors(deviceDX12)) {
    goto cleanup;
  }
  ok = true;

cleanup:
  GPUDestroyFence(fence);
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(indirectBuffer);
  GPUDestroyBuffer(indexBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyDevice(device);
  return ok;
}

static bool
run_occlusion_case(GPUAdapter *adapter) {
  GPUDevice                   *device;
  GPUDeviceDX12               *deviceDX12;
  GPUQueue                    *queue;
  GPUCommandBuffer            *cmdb;
  GPUCommandBuffer            *buffers[1];
  GPUTexture                  *target;
  GPUTexture                  *target2;
  GPUTexture                  *resolveTarget;
  GPUTexture                  *resolveTarget2;
  GPUTexture                  *depthTarget;
  GPUTextureView              *targetView;
  GPUTextureView              *targetView2;
  GPUTextureView              *resolveView;
  GPUTextureView              *resolveView2;
  GPUTextureView              *depthView;
  GPUQuerySet                 *querySet;
  GPUBuffer                   *resultBuffer;
  GPUFence                    *fence;
  GPURenderPassEncoder        *pass;
  GPUTextureCreateInfo         textureInfo = {0};
  GPUTextureCreateInfo         resolveTextureInfo = {0};
  GPUTextureCreateInfo         depthTextureInfo = {0};
  GPUTextureViewCreateInfo     viewInfo = {0};
  GPUTextureViewCreateInfo     depthViewInfo = {0};
  GPUQuerySetCreateInfo        queryInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPURenderPassColorAttachment colors[2] = {{0}};
  GPURenderPassDepthStencilAttachment depthStencil = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPUFenceCreateInfo           fenceInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  GPUResult                    queryResult;
  uint64_t                     resultValue;
  bool                         ok;

  device         = GPUCreateDeviceWithDefaultQueues(adapter);
  deviceDX12     = device ? device->_priv : NULL;
  queue          = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  cmdb           = NULL;
  target         = NULL;
  target2        = NULL;
  resolveTarget  = NULL;
  resolveTarget2 = NULL;
  depthTarget    = NULL;
  targetView     = NULL;
  targetView2    = NULL;
  resolveView    = NULL;
  resolveView2   = NULL;
  depthView      = NULL;
  querySet       = NULL;
  resultBuffer   = NULL;
  fence          = NULL;
  pass           = NULL;
  resultValue    = UINT64_MAX;
  ok             = false;
  if (!device || !deviceDX12 || !queue) {
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "dx12-occlusion-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 4u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET;
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.label               = "dx12-occlusion-target-view";
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.format              = GPU_FORMAT_BGRA8_UNORM;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;
  resolveTextureInfo             = textureInfo;
  resolveTextureInfo.label       = "dx12-resolve-target";
  resolveTextureInfo.sampleCount = 1u;
  depthTextureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  depthTextureInfo.chain.structSize = sizeof(depthTextureInfo);
  depthTextureInfo.label            = "dx12-depth-stencil-target";
  depthTextureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  depthTextureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  depthTextureInfo.width            = 4u;
  depthTextureInfo.height           = 4u;
  depthTextureInfo.depthOrLayers    = 1u;
  depthTextureInfo.mipLevelCount    = 1u;
  depthTextureInfo.sampleCount      = 4u;
  depthTextureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  depthViewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  depthViewInfo.chain.structSize = sizeof(depthViewInfo);
  depthViewInfo.label            = "dx12-depth-stencil-view";
  depthViewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  depthViewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  depthViewInfo.mipLevelCount    = 1u;
  depthViewInfo.arrayLayerCount  = 1u;
  queryInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
  queryInfo.chain.structSize   = sizeof(queryInfo);
  queryInfo.label              = "dx12-occlusion";
  queryInfo.type               = GPU_QUERY_OCCLUSION;
  queryInfo.count              = 1u;
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "dx12-occlusion-result";
  bufferInfo.sizeBytes        = sizeof(resultValue);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  queryResult = GPUCreateQuerySet(device, &queryInfo, &querySet);
  if (queryResult == GPU_ERROR_UNSUPPORTED && !querySet) {
    ok = true;
    goto cleanup;
  }
  if (queryResult != GPU_OK || !querySet) {
    goto cleanup;
  }
  if (GPUCreateTexture(device, &textureInfo, &target) != GPU_OK || !target ||
      GPUCreateTextureView(target, &viewInfo, &targetView) != GPU_OK ||
      !targetView ||
      GPUCreateTexture(device, &textureInfo, &target2) != GPU_OK || !target2 ||
      GPUCreateTextureView(target2, &viewInfo, &targetView2) != GPU_OK ||
      !targetView2 ||
      GPUCreateTexture(device, &resolveTextureInfo, &resolveTarget) != GPU_OK ||
      !resolveTarget ||
      GPUCreateTextureView(resolveTarget, &viewInfo, &resolveView) != GPU_OK ||
      !resolveView ||
      GPUCreateTexture(device,
                       &resolveTextureInfo,
                       &resolveTarget2) != GPU_OK ||
      !resolveTarget2 ||
      GPUCreateTextureView(resolveTarget2,
                           &viewInfo,
                           &resolveView2) != GPU_OK ||
      !resolveView2 ||
      GPUCreateTexture(device, &depthTextureInfo, &depthTarget) != GPU_OK ||
      !depthTarget ||
      GPUCreateTextureView(depthTarget, &depthViewInfo, &depthView) != GPU_OK ||
      !depthView ||
      GPUCreateBuffer(device, &bufferInfo, &resultBuffer) != GPU_OK ||
      !resultBuffer ||
      GPUAcquireCommandBuffer(queue, "dx12-occlusion", &cmdb) != GPU_OK ||
      !cmdb) {
    goto cleanup;
  }

  colors[0].view                = targetView;
  colors[0].resolveView         = resolveView;
  colors[0].loadOp              = GPU_LOAD_OP_CLEAR;
  colors[0].storeOp             = GPU_STORE_OP_STORE;
  colors[1]                     = colors[0];
  colors[1].view                = targetView2;
  colors[1].resolveView         = resolveView2;
  depthStencil.view             = depthView;
  depthStencil.depthLoadOp      = GPU_LOAD_OP_CLEAR;
  depthStencil.depthStoreOp     = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp    = GPU_LOAD_OP_CLEAR;
  depthStencil.stencilStoreOp   = GPU_STORE_OP_STORE;
  depthStencil.clearDepth       = 1.0f;
  depthStencil.clearStencil     = 7u;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "dx12-occlusion";
  passInfo.occlusionQuerySet    = querySet;
  passInfo.colorAttachmentCount = 2u;
  passInfo.pColorAttachments    = colors;
  passInfo.pDepthStencilAttachment = &depthStencil;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    goto cleanup;
  }

  GPUBeginOcclusionQuery(pass, querySet, 0u);
  if (!pass->_occlusionQueryActive) {
    goto cleanup;
  }
  GPUEndOcclusionQuery(pass);
  GPUEndRenderPass(pass);
  pass = NULL;
  GPUResolveQuerySet(cmdb, querySet, 0u, 1u, resultBuffer, 0u);

  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    goto cleanup;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         resultBuffer,
                         0u,
                         &resultValue,
                         sizeof(resultValue)) != GPU_OK ||
      resultValue != 0u || has_debug_errors(deviceDX12)) {
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  passInfo.occlusionQuerySet = NULL;
  GPUResetStats(device);
  for (uint32_t i = 0u; i < DX12_OFFSCREEN_WARM_ITERATIONS; i++) {
    colors[0].loadOp  = (GPULoadOp)(i % 3u);
    colors[0].storeOp = (GPUStoreOp)((i / 3u) % 2u);
    colors[1].loadOp  = (GPULoadOp)((i / 2u) % 3u);
    colors[1].storeOp = (GPUStoreOp)((i / 5u) % 2u);
    depthStencil.depthLoadOp    = (GPULoadOp)((i / 2u) % 3u);
    depthStencil.depthStoreOp   = (GPUStoreOp)((i / 5u) % 2u);
    depthStencil.stencilLoadOp  = (GPULoadOp)((i / 3u) % 3u);
    depthStencil.stencilStoreOp = (GPUStoreOp)((i / 7u) % 2u);
    passInfo.colorAttachmentCount = i % 3u;
    if (GPUAcquireCommandBuffer(queue, "dx12-offscreen-warm", &cmdb) != GPU_OK ||
        !cmdb || !(pass = GPUBeginRenderPass(cmdb, &passInfo))) {
      goto cleanup;
    }
    GPUEndRenderPass(pass);
    pass = NULL;

    buffers[0]                  = cmdb;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
        GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
      cmdb = NULL;
      goto cleanup;
    }
    cmdb = NULL;
  }
  passInfo.colorAttachmentCount = 2u;
  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u ||
      has_debug_errors(deviceDX12)) {
    goto cleanup;
  }
  ok = true;

cleanup:
  if (pass) {
    GPUEndOcclusionQuery(pass);
    GPUEndRenderPass(pass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBuffer(resultBuffer);
  GPUDestroyQuerySet(querySet);
  GPUDestroyTextureView(depthView);
  GPUDestroyTexture(depthTarget);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyTextureView(targetView2);
  GPUDestroyTexture(target2);
  GPUDestroyTextureView(resolveView);
  GPUDestroyTexture(resolveTarget);
  GPUDestroyTextureView(resolveView2);
  GPUDestroyTexture(resolveTarget2);
  GPUDestroyDevice(device);
  return ok;
}

int
main(void) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  bool                  ok;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "DX12 barrier instance creation failed\n");
    return 1;
  }

  adapter = first_adapter(instance);
  if (!adapter) {
    fprintf(stderr, "DX12 barrier adapter enumeration failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  ok = run_barrier_case(adapter, false) &&
       run_barrier_case(adapter, true) &&
       run_occlusion_case(adapter);
  GPUDestroyInstance(instance);
  if (!ok) {
    return 1;
  }

  printf("DX12 texture barrier validation passed\n");
  return 0;
}
