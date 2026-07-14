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
#include "../../../api/buffer_internal.h"
#include "../../../api/render/pipeline_internal.h"

static GPURenderEncoderVk*
vk__renderEncoder(GPURenderCommandEncoder *encoder) {
  return encoder ? encoder->_priv : NULL;
}

static VkViewport
vk__viewport(float x,
             float y,
             float width,
             float height,
             float minDepth,
             float maxDepth) {
  VkViewport viewport;

  viewport.x        = x;
  viewport.y        = y + height;
  viewport.width    = width;
  viewport.height   = -height;
  viewport.minDepth = minDepth;
  viewport.maxDepth = maxDepth;
  return viewport;
}

static void
vk__scissorAxis(int32_t   origin,
                 uint32_t  extent,
                 uint32_t  limit,
                 int32_t  *outOrigin,
                 uint32_t *outExtent) {
  uint64_t clipped;
  uint64_t maxExtent;

  if (origin < 0) {
    clipped  = (uint64_t)-(int64_t)origin;
    extent   = clipped >= extent ? 0u : extent - (uint32_t)clipped;
    origin   = 0;
  }

  if ((uint32_t)origin >= limit) {
    *outOrigin = (int32_t)limit;
    *outExtent = 0u;
    return;
  }

  maxExtent  = limit - (uint32_t)origin;
  *outOrigin = origin;
  *outExtent = extent > maxExtent ? (uint32_t)maxExtent : extent;
}

static bool
vk__bindIndexBuffer(GPURenderCommandEncoder *encoder,
                    GPURenderEncoderVk      *native) {
  GPUBuffer    *buffer;
  GPUBufferVk  *bufferVk;
  VkDeviceSize  offset;
  VkIndexType   indexType;
  uint64_t      indexSize;

  buffer    = encoder ? encoder->_indexBuffer : NULL;
  bufferVk  = buffer ? buffer->_priv : NULL;
  offset    = encoder ? encoder->_indexBufferOffset : 0u;
  indexSize = encoder && encoder->_indexType == GPU_INDEX_TYPE_UINT32
                ? 4u
                : 2u;
  indexType = indexSize == 4u ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
  if (!native || !native->command || !buffer || !bufferVk ||
      !bufferVk->buffer || offset >= buffer->sizeBytes ||
      buffer->sizeBytes - offset < indexSize ||
      (offset & (indexSize - 1u)) != 0u) {
    return false;
  }

  if (native->indexBound && native->indexBuffer == buffer &&
      native->indexOffset == offset &&
      native->indexType == encoder->_indexType) {
    return true;
  }

  vkCmdBindIndexBuffer(native->command, bufferVk->buffer, offset, indexType);
  native->indexBuffer = buffer;
  native->indexOffset = offset;
  native->indexType   = encoder->_indexType;
  native->indexBound  = true;
  return true;
}

GPU_HIDE
GPURenderCommandEncoder*
vk_renderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  GPUCommandBufferVk     *command;
  GPURenderPassVk        *renderPass;
  GPURenderCommandEncoder *encoder;
  GPURenderEncoderVk     *native;
  VkRenderPassBeginInfo   beginInfo = {0};
  VkViewport              viewport = {0};
  VkRect2D                scissor = {0};

  command    = cmdb ? cmdb->_priv : NULL;
  renderPass = pass ? pass->_priv : NULL;
  if (!command || !renderPass ||
      (renderPass->dynamic
         ? (!renderPass->renderingInfo.pColorAttachments &&
            !renderPass->renderingInfo.pDepthAttachment)
         : (!renderPass->renderPass || !renderPass->framebuffer))) {
    return NULL;
  }

  encoder = &command->renderEncoder;
  native  = &command->renderState;
  memset(encoder, 0, sizeof(*encoder));
  memset(native, 0, sizeof(*native));
  native->device  = cmdb && cmdb->_queue && cmdb->_queue->_device ?
    cmdb->_queue->_device->_priv :
    NULL;
  native->renderPass = renderPass;
  native->command    = command->command;
  native->extent     = renderPass->extent;

  if (renderPass->dynamic &&
      (!native->device || !native->device->beginRendering)) {
    return NULL;
  }
  native->debugLabelActive = vk_beginDebugLabel(
    gpuCommandBufferDevice(cmdb),
    native->command,
    pass->label
  );
  if (renderPass->dynamic) {
    native->device->beginRendering(native->command,
                                   &renderPass->renderingInfo);
  } else {
    beginInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    beginInfo.renderPass        = renderPass->renderPass;
    beginInfo.framebuffer       = renderPass->framebuffer;
    beginInfo.renderArea.extent = renderPass->extent;
    beginInfo.clearValueCount   = 1u;
    beginInfo.pClearValues      = &renderPass->clearValue;
    vkCmdBeginRenderPass(native->command,
                         &beginInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
  }

  viewport = vk__viewport(0.0f,
                          0.0f,
                          (float)native->extent.width,
                          (float)native->extent.height,
                          0.0f,
                          1.0f);
  scissor.extent    = native->extent;
  vkCmdSetViewport(native->command, 0u, 1u, &viewport);
  vkCmdSetScissor(native->command, 0u, 1u, &scissor);
  vkCmdSetStencilReference(native->command,
                           VK_STENCIL_FACE_FRONT_AND_BACK,
                           0u);

  encoder->_priv          = native;
  encoder->_primitiveType = GPUPrimitiveTypeTriangle;
  return encoder;
}

GPU_HIDE
void
vk_setRenderPipelineState(GPURenderCommandEncoder *encoder,
                          GPURenderPipelineState  *pipelineState) {
  GPURenderEncoderVk  *native;
  GPURenderPipelineVk *pipeline;

  native   = vk__renderEncoder(encoder);
  pipeline = pipelineState ? pipelineState->_priv : NULL;
  if (!native || !pipeline) {
    return;
  }

  vkCmdBindPipeline(native->command,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->pipeline);
  vk_bindShaderSamplers(native->command,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        &pipeline->shaderLayout);
  native->pipelineLayout = pipeline->shaderLayout.layout;
}

GPU_HIDE
void
vk_viewport(GPURenderCommandEncoder *encoder, const GPUViewport *value) {
  GPURenderEncoderVk *native;
  VkViewport          viewport;

  native = vk__renderEncoder(encoder);
  if (!native || !value) {
    return;
  }

  viewport = vk__viewport(value->x,
                          value->y,
                          value->width,
                          value->height,
                          value->minDepth,
                          value->maxDepth);
  vkCmdSetViewport(native->command, 0u, 1u, &viewport);
}

GPU_HIDE
void
vk_scissor(GPURenderCommandEncoder *encoder, const GPUScissorRect *value) {
  GPURenderEncoderVk *native;
  VkRect2D            scissor;

  native = vk__renderEncoder(encoder);
  if (!native || !value) {
    return;
  }

  vk__scissorAxis(value->x,
                   value->width,
                   native->extent.width,
                   &scissor.offset.x,
                   &scissor.extent.width);
  vk__scissorAxis(value->y,
                   value->height,
                   native->extent.height,
                   &scissor.offset.y,
                   &scissor.extent.height);
  vkCmdSetScissor(native->command, 0u, 1u, &scissor);
}

GPU_HIDE
void
vk_blendConstant(GPURenderCommandEncoder *encoder, const float rgba[4]) {
  GPURenderEncoderVk *native;

  native = vk__renderEncoder(encoder);
  if (!native || !rgba) {
    return;
  }

  vkCmdSetBlendConstants(native->command, rgba);
}

GPU_HIDE
void
vk_stencilReference(GPURenderCommandEncoder *encoder, uint32_t reference) {
  GPURenderEncoderVk *native;

  native = vk__renderEncoder(encoder);
  if (!native) {
    return;
  }

  vkCmdSetStencilReference(native->command,
                           VK_STENCIL_FACE_FRONT_AND_BACK,
                           reference);
}

GPU_HIDE
void
vk_renderPushConstants(GPURenderCommandEncoder *encoder,
                       GPUShaderStageFlags       stages,
                       const void               *data,
                       uint32_t                  sizeBytes) {
  GPURenderEncoderVk *native;
  VkShaderStageFlags  stageFlags;

  native = vk__renderEncoder(encoder);
  if (!native || !native->pipelineLayout || !data || sizeBytes == 0u) {
    return;
  }

  stageFlags = 0u;
  if ((stages & GPU_SHADER_STAGE_VERTEX_BIT) != 0u) {
    stageFlags |= VK_SHADER_STAGE_VERTEX_BIT;
  }
  if ((stages & GPU_SHADER_STAGE_FRAGMENT_BIT) != 0u) {
    stageFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  if (stageFlags != 0u) {
    vkCmdPushConstants(native->command,
                       native->pipelineLayout,
                       stageFlags,
                       0u,
                       sizeBytes,
                       data);
  }
}

GPU_HIDE
void
vk_vertexBuffer(GPURenderCommandEncoder *encoder,
                GPUBuffer               *buffer,
                uint64_t                 offset,
                uint32_t                 index) {
  GPURenderEncoderVk *native;
  GPUBufferVk        *bufferVk;
  VkDeviceSize        nativeOffset;

  native   = vk__renderEncoder(encoder);
  bufferVk = buffer ? buffer->_priv : NULL;
  if (!native || !bufferVk || !bufferVk->buffer) {
    return;
  }

  nativeOffset = offset;
  vkCmdBindVertexBuffers(native->command,
                         index,
                         1u,
                         &bufferVk->buffer,
                         &nativeOffset);
}

GPU_HIDE
void
vk_drawPrimitives(GPURenderCommandEncoder *encoder,
                  GPUPrimitiveType         type,
                  size_t                   start,
                  size_t                   count,
                  uint32_t                 instanceCount,
                  uint32_t                 firstInstance) {
  GPURenderEncoderVk *native;

  GPU__UNUSED(type);

  native = vk__renderEncoder(encoder);
  if (!native || start > UINT32_MAX || count > UINT32_MAX) {
    return;
  }

  vkCmdDraw(native->command,
            (uint32_t)count,
            instanceCount,
            (uint32_t)start,
            firstInstance);
}

GPU_HIDE
void
vk_drawIndexedPrims(GPURenderCommandEncoder *encoder,
                    uint32_t                 indexCount,
                    uint32_t                 instanceCount,
                    uint32_t                 firstIndex,
                    int32_t                  vertexOffset,
                    uint32_t                 firstInstance) {
  GPURenderEncoderVk *native;

  native = vk__renderEncoder(encoder);
  if (!vk__bindIndexBuffer(encoder, native)) {
    return;
  }

  vkCmdDrawIndexed(native->command,
                   indexCount,
                   instanceCount,
                   firstIndex,
                   vertexOffset,
                   firstInstance);
}

GPU_HIDE
void
vk_drawPrimitivesIndirect(GPURenderCommandEncoder *encoder,
                          GPUPrimitiveType         type,
                          GPUBuffer               *argsBuffer,
                          uint64_t                 argsOffset) {
  GPURenderEncoderVk *native;
  GPUBufferVk        *buffer;

  GPU__UNUSED(type);

  native = vk__renderEncoder(encoder);
  buffer = argsBuffer ? argsBuffer->_priv : NULL;
  if (!native || !native->command || !buffer || !buffer->buffer) {
    return;
  }

  vkCmdDrawIndirect(native->command,
                    buffer->buffer,
                    argsOffset,
                    1u,
                    sizeof(VkDrawIndirectCommand));
}

GPU_HIDE
void
vk_drawIndexedPrimsIndirect(GPURenderCommandEncoder *encoder,
                            GPUBuffer               *argsBuffer,
                            uint64_t                 argsOffset) {
  GPURenderEncoderVk *native;
  GPUBufferVk        *buffer;

  native = vk__renderEncoder(encoder);
  buffer = argsBuffer ? argsBuffer->_priv : NULL;
  if (!vk__bindIndexBuffer(encoder, native) || !buffer || !buffer->buffer) {
    return;
  }

  vkCmdDrawIndexedIndirect(native->command,
                           buffer->buffer,
                           argsOffset,
                           1u,
                           sizeof(VkDrawIndexedIndirectCommand));
}

GPU_HIDE
bool
vk_multiDrawPrimitivesIndirect(GPURenderCommandEncoder *encoder,
                               GPUPrimitiveType         type,
                               GPUBuffer               *argsBuffer,
                               uint64_t                 argsOffset,
                               uint32_t                 drawCount,
                               uint32_t                 strideBytes) {
  GPURenderEncoderVk *native;
  GPUBufferVk        *buffer;

  GPU__UNUSED(type);

  native = vk__renderEncoder(encoder);
  buffer = argsBuffer ? argsBuffer->_priv : NULL;
  if (!native || !native->device || !native->command ||
      !buffer || !buffer->buffer ||
      drawCount > native->device->maxDrawIndirectCount ||
      (drawCount > 1u && !native->device->multiDrawIndirect)) {
    return false;
  }

  vkCmdDrawIndirect(native->command,
                    buffer->buffer,
                    argsOffset,
                    drawCount,
                    strideBytes);
  return true;
}

GPU_HIDE
bool
vk_multiDrawIndexedPrimsIndirect(GPURenderCommandEncoder *encoder,
                                 GPUBuffer               *argsBuffer,
                                 uint64_t                 argsOffset,
                                 uint32_t                 drawCount,
                                 uint32_t                 strideBytes) {
  GPURenderEncoderVk *native;
  GPUBufferVk        *buffer;

  native = vk__renderEncoder(encoder);
  buffer = argsBuffer ? argsBuffer->_priv : NULL;
  if (!native || !native->device || !native->command ||
      !buffer || !buffer->buffer ||
      drawCount > native->device->maxDrawIndirectCount ||
      (drawCount > 1u && !native->device->multiDrawIndirect) ||
      !vk__bindIndexBuffer(encoder, native)) {
    return false;
  }

  vkCmdDrawIndexedIndirect(native->command,
                           buffer->buffer,
                           argsOffset,
                           drawCount,
                           strideBytes);
  return true;
}

GPU_HIDE
void
vk_endRenderEncoding(GPURenderCommandEncoder *encoder) {
  GPURenderEncoderVk *native;

  native = vk__renderEncoder(encoder);
  if (native) {
    if (native->renderPass && native->renderPass->dynamic) {
      native->device->endRendering(native->command);
      for (uint32_t i = 0u; i < native->renderPass->colorCount; i++) {
        GPUTextureViewVk *view;

        view = native->renderPass->colorViews[i];
        if (view && view->swapchain) {
          vk_transitionView(native->command,
                            view,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
        view = native->renderPass->resolveViews[i];
        if (view && view->swapchain) {
          vk_transitionView(native->command,
                            view,
                            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
      }
    } else {
      vkCmdEndRenderPass(native->command);
    }
    if (native->debugLabelActive) {
      vk_endDebugLabel(gpuCommandBufferDevice(encoder->_cmdb), native->command);
      native->debugLabelActive = false;
    }
  }
}

GPU_HIDE
void
vk_initRCE(GPUApiRCE *api) {
  api->renderCommandEncoder     = vk_renderCommandEncoder;
  api->setRenderPipelineState   = vk_setRenderPipelineState;
  api->viewport                 = vk_viewport;
  api->scissor                  = vk_scissor;
  api->blendConstant            = vk_blendConstant;
  api->stencilReference         = vk_stencilReference;
  api->pushConstants            = vk_renderPushConstants;
  api->vertexBuffer             = vk_vertexBuffer;
  api->vertexInputBuffer        = vk_vertexBuffer;
  api->drawPrimitives           = vk_drawPrimitives;
  api->drawIndexedPrims         = vk_drawIndexedPrims;
  api->drawPrimitivesIndirect   = vk_drawPrimitivesIndirect;
  api->drawIndexedPrimsIndirect = vk_drawIndexedPrimsIndirect;

  api->multiDrawPrimitivesIndirect = vk_multiDrawPrimitivesIndirect;
  api->multiDrawIndexedPrimsIndirect = vk_multiDrawIndexedPrimsIndirect;

  api->endEncoding              = vk_endRenderEncoding;
}
