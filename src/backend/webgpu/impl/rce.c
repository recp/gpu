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
  return &command->render;
}

static void
webgpu_setPipeline(GPURenderPassEncoder *encoder,
                   GPURenderPipelineState *pipeline) {
  GPUCommandWebGPU *command;

  command = webgpu_renderCommand(encoder);
  if (command && command->renderEncoder && pipeline && pipeline->_priv) {
    wgpuRenderPassEncoderSetPipeline(command->renderEncoder, pipeline->_priv);
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
  api->renderCommandEncoder   = webgpu_renderCommandEncoder;
  api->setRenderPipelineState = webgpu_setPipeline;
  api->viewport               = webgpu_viewport;
  api->scissor                = webgpu_scissor;
  api->blendConstant          = webgpu_blendConstant;
  api->stencilReference       = webgpu_stencilReference;
  api->drawPrimitives         = webgpu_draw;
  api->endEncoding            = webgpu_endEncoding;
}
