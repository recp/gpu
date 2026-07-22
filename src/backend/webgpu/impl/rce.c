/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static GPUCommandWebGPU *
webgpu_renderCommand(GPURenderPassEncoder *encoder) {
  return encoder ? encoder->_priv : NULL;
}

static void
webgpu_renderPushConstants(GPURenderPassEncoder *encoder,
                           GPUShaderStageFlags    stages,
                           const void            *data,
                           uint32_t               sizeBytes) {
  GPUCommandWebGPU *command;
  uint32_t          dynamicOffset;

  GPU__UNUSED(stages);
  command = webgpu_renderCommand(encoder);
  if (!command || !command->renderEncoder ||
      !gpu_webgpuUploadPushConstants(command,
                                     data,
                                     sizeBytes,
                                     &dynamicOffset)) {
    return;
  }
  wgpuRenderPassEncoderSetBindGroup(command->renderEncoder,
                                    GPU_WEBGPU_PUSH_CONSTANT_GROUP,
                                    command->pushConstantGroup,
                                    1u,
                                    &dynamicOffset);
}

static GPURenderPassEncoder *
webgpu_renderCommandEncoder(GPUCommandBuffer *cmdb, GPURenderPassDesc *pass) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command || pass->_priv != command || !command->encoder) {
    return NULL;
  }

  memset(&command->render, 0, sizeof(command->render));
  command->renderEncoder = wgpuCommandEncoderBeginRenderPass(
    command->encoder,
    &command->renderPassDesc
  );
  if (!command->renderEncoder) {
    return NULL;
  }

  command->render._priv          = command;
  command->render._primitiveType = GPUPrimitiveTypeTriangle;
  command->boundIndexBuffer      = NULL;
  command->boundIndexOffset      = 0u;
  command->boundIndexFormat      = WGPUIndexFormat_Undefined;
  return &command->render;
}

static void
webgpu_setPipeline(GPURenderPassEncoder *encoder,
                   GPURenderPipelineState *pipeline) {
  GPURenderPipelineWebGPU *state;
  GPUCommandWebGPU        *command;

  command = webgpu_renderCommand(encoder);
  state   = pipeline ? pipeline->_priv : NULL;
  if (command && command->renderEncoder && state && state->pipeline) {
    static const uint8_t zero[GPU_WEBGPU_PUSH_CONSTANT_ALIGNMENT];

    wgpuRenderPassEncoderSetPipeline(command->renderEncoder, state->pipeline);
    gpu_webgpuBindRenderAutomaticGroups(encoder, &state->layout);
    if (state->layout.pushConstantSizeBytes > 0u) {
      webgpu_renderPushConstants(encoder,
                                 0u,
                                 zero,
                                 state->layout.pushConstantSizeBytes);
    }
  }
}

static void
webgpu_viewport(GPURenderPassEncoder *encoder, const GPUViewport *viewport) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (command && command->renderEncoder && viewport) {
    wgpuRenderPassEncoderSetViewport(command->renderEncoder,
                                     viewport->x,
                                     viewport->y,
                                     viewport->width,
                                     viewport->height,
                                     viewport->minDepth,
                                     viewport->maxDepth);
  }
}

static void
webgpu_scissor(GPURenderPassEncoder *encoder, const GPUScissorRect *scissor) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (command && command->renderEncoder && scissor) {
    wgpuRenderPassEncoderSetScissorRect(command->renderEncoder,
                                        scissor->x,
                                        scissor->y,
                                        scissor->width,
                                        scissor->height);
  }
}

static void
webgpu_blendConstant(GPURenderPassEncoder *encoder, const float rgba[4]) {
  GPUCommandWebGPU *command;
  WGPUColor         color;

  command = webgpu_renderCommand(encoder);
  if (command && command->renderEncoder && rgba) {
    color.r = rgba[0];
    color.g = rgba[1];
    color.b = rgba[2];
    color.a = rgba[3];
    wgpuRenderPassEncoderSetBlendConstant(command->renderEncoder, &color);
  }
}

static void
webgpu_stencilReference(GPURenderPassEncoder *encoder, uint32_t reference) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (command && command->renderEncoder) {
    wgpuRenderPassEncoderSetStencilReference(command->renderEncoder,
                                              reference);
  }
}

static void
webgpu_vertexInputBuffer(GPURenderPassEncoder *encoder,
                         GPUBuffer            *buffer,
                         uint64_t              offset,
                         uint32_t              index) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (!command || !command->renderEncoder || !buffer || !buffer->_priv ||
      offset > buffer->sizeBytes) {
    return;
  }
  wgpuRenderPassEncoderSetVertexBuffer(command->renderEncoder,
                                       index,
                                       buffer->_priv,
                                       offset,
                                       buffer->sizeBytes - offset);
}

static void
webgpu_draw(GPURenderPassEncoder *encoder,
            GPUPrimitiveType      primitive,
            size_t                firstVertex,
            size_t                vertexCount,
            uint32_t              instanceCount,
            uint32_t              firstInstance) {
  GPUCommandWebGPU *command;

  GPU__UNUSED(primitive);
  command = webgpu_renderCommand(encoder);
  if (!command || !command->renderEncoder ||
      firstVertex > UINT32_MAX || vertexCount > UINT32_MAX) {
    return;
  }
  wgpuRenderPassEncoderDraw(command->renderEncoder,
                            (uint32_t)vertexCount,
                            instanceCount,
                            (uint32_t)firstVertex,
                            firstInstance);
}

static bool
webgpu_bindIndexBuffer(GPURenderPassEncoder *encoder,
                       GPUCommandWebGPU     *command) {
  GPUBuffer      *buffer;
  WGPUIndexFormat format;

  buffer = encoder ? encoder->_indexBuffer : NULL;
  format = encoder && encoder->_indexType == GPU_INDEX_TYPE_UINT32
             ? WGPUIndexFormat_Uint32
             : WGPUIndexFormat_Uint16;
  if (!command || !command->renderEncoder || !buffer || !buffer->_priv ||
      encoder->_indexBufferOffset >= buffer->sizeBytes) {
    return false;
  }

  if (command->boundIndexBuffer != buffer->_priv ||
      command->boundIndexOffset != encoder->_indexBufferOffset ||
      command->boundIndexFormat != format) {
    wgpuRenderPassEncoderSetIndexBuffer(
      command->renderEncoder,
      buffer->_priv,
      format,
      encoder->_indexBufferOffset,
      buffer->sizeBytes - encoder->_indexBufferOffset
    );
    command->boundIndexBuffer = buffer->_priv;
    command->boundIndexOffset = encoder->_indexBufferOffset;
    command->boundIndexFormat = format;
  }
  return true;
}

static void
webgpu_drawIndexed(GPURenderPassEncoder *encoder,
                   uint32_t              indexCount,
                   uint32_t              instanceCount,
                   uint32_t              firstIndex,
                   int32_t               vertexOffset,
                   uint32_t              firstInstance) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (!webgpu_bindIndexBuffer(encoder, command)) {
    return;
  }

  wgpuRenderPassEncoderDrawIndexed(command->renderEncoder,
                                   indexCount,
                                   instanceCount,
                                   firstIndex,
                                   vertexOffset,
                                   firstInstance);
}

static bool
webgpu_indirectEnabled(const GPURenderPassEncoder *encoder) {
  GPUDevice *device;

  device = encoder && encoder->_cmdb && encoder->_cmdb->_queue
             ? encoder->_cmdb->_queue->_device
             : NULL;
  return GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW);
}

static void
webgpu_drawIndirect(GPURenderPassEncoder *encoder,
                    GPUPrimitiveType      primitive,
                    GPUBuffer            *argsBuffer,
                    uint64_t              argsOffset) {
  GPUCommandWebGPU *command;

  GPU__UNUSED(primitive);
  command = webgpu_renderCommand(encoder);
  if (!webgpu_indirectEnabled(encoder) ||
      !command || !command->renderEncoder ||
      !argsBuffer || !argsBuffer->_priv) {
    return;
  }
  wgpuRenderPassEncoderDrawIndirect(command->renderEncoder,
                                    argsBuffer->_priv,
                                    argsOffset);
}

static void
webgpu_drawIndexedIndirect(GPURenderPassEncoder *encoder,
                           GPUBuffer            *argsBuffer,
                           uint64_t              argsOffset) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (!webgpu_indirectEnabled(encoder) ||
      !argsBuffer || !argsBuffer->_priv ||
      !webgpu_bindIndexBuffer(encoder, command)) {
    return;
  }
  wgpuRenderPassEncoderDrawIndexedIndirect(command->renderEncoder,
                                           argsBuffer->_priv,
                                           argsOffset);
}

static bool
webgpu_multiDrawIndirect(GPURenderPassEncoder *encoder,
                         GPUPrimitiveType      primitive,
                         GPUBuffer            *argsBuffer,
                         uint64_t              argsOffset,
                         uint32_t              drawCount,
                         uint32_t              strideBytes) {
  GPUCommandWebGPU *command;
  GPUDevice        *device;

  GPU__UNUSED(primitive);
  command = webgpu_renderCommand(encoder);
  device  = encoder && encoder->_cmdb && encoder->_cmdb->_queue
              ? encoder->_cmdb->_queue->_device
              : NULL;
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW) ||
      strideBytes != 16u || !command || !command->renderEncoder ||
      !argsBuffer || !argsBuffer->_priv) {
    return false;
  }
  wgpuRenderPassEncoderMultiDrawIndirect(command->renderEncoder,
                                         argsBuffer->_priv,
                                         argsOffset,
                                         drawCount,
                                         NULL,
                                         0u);
  return true;
}

static bool
webgpu_multiDrawIndexedIndirect(GPURenderPassEncoder *encoder,
                                GPUBuffer            *argsBuffer,
                                uint64_t              argsOffset,
                                uint32_t              drawCount,
                                uint32_t              strideBytes) {
  GPUCommandWebGPU *command;
  GPUDevice        *device;

  command = webgpu_renderCommand(encoder);
  device  = encoder && encoder->_cmdb && encoder->_cmdb->_queue
              ? encoder->_cmdb->_queue->_device
              : NULL;
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW) ||
      strideBytes != 20u || !argsBuffer || !argsBuffer->_priv ||
      !webgpu_bindIndexBuffer(encoder, command)) {
    return false;
  }
  wgpuRenderPassEncoderMultiDrawIndexedIndirect(command->renderEncoder,
                                                argsBuffer->_priv,
                                                argsOffset,
                                                drawCount,
                                                NULL,
                                                0u);
  return true;
}

static void
webgpu_endEncoding(GPURenderPassEncoder *encoder) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (!command || !command->renderEncoder) {
    return;
  }
  wgpuRenderPassEncoderEnd(command->renderEncoder);
  wgpuRenderPassEncoderRelease(command->renderEncoder);
  command->renderEncoder = NULL;
}

void
webgpu_initRenderEncoder(GPUApiRCE *api) {
  api->renderCommandEncoder          = webgpu_renderCommandEncoder;
  api->setRenderPipelineState        = webgpu_setPipeline;
  api->viewport                      = webgpu_viewport;
  api->scissor                       = webgpu_scissor;
  api->blendConstant                 = webgpu_blendConstant;
  api->stencilReference              = webgpu_stencilReference;
  api->pushConstants                 = webgpu_renderPushConstants;
  api->vertexInputBuffer             = webgpu_vertexInputBuffer;
  api->drawPrimitives                = webgpu_draw;
  api->drawIndexedPrims              = webgpu_drawIndexed;
  api->drawPrimitivesIndirect        = webgpu_drawIndirect;
  api->drawIndexedPrimsIndirect      = webgpu_drawIndexedIndirect;
  api->multiDrawPrimitivesIndirect   = webgpu_multiDrawIndirect;
  api->multiDrawIndexedPrimsIndirect = webgpu_multiDrawIndexedIndirect;
  api->endEncoding                   = webgpu_endEncoding;
}
