/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static WGPULoadOp
webgpu_loadOp(GPULoadOp op) {
  switch (op) {
    case GPU_LOAD_OP_CLEAR:
      return WGPULoadOp_Clear;
    case GPU_LOAD_OP_DONT_CARE:
      return WGPULoadOp_Clear;
    case GPU_LOAD_OP_LOAD:
    default:
      return WGPULoadOp_Load;
  }
}

static WGPUStoreOp
webgpu_storeOp(GPUStoreOp op) {
  return op == GPU_STORE_OP_DONT_CARE
           ? WGPUStoreOp_Discard
           : WGPUStoreOp_Store;
}

static GPURenderPassDesc *
webgpu_beginRenderPass(GPUCommandBuffer              *cmdb,
                       const GPURenderPassCreateInfo *info) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command || !command->encoder ||
      info->colorAttachmentCount > GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS ||
      info->pDepthStencilAttachment) {
    return NULL;
  }

  command->renderPassDesc =
    (WGPURenderPassDescriptor)WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  memset(command->colorAttachments, 0, sizeof(command->colorAttachments));
  for (uint32_t i = 0u; i < info->colorAttachmentCount; i++) {
    const GPURenderPassColorAttachment *source;
    WGPURenderPassColorAttachment      *target;

    source  = &info->pColorAttachments[i];
    target  = &command->colorAttachments[i];
    *target = (WGPURenderPassColorAttachment)
      WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    target->view          = source->view->_priv;
    target->resolveTarget = source->resolveView
                              ? source->resolveView->_priv
                              : NULL;
    target->loadOp        = webgpu_loadOp(source->loadOp);
    target->storeOp       = webgpu_storeOp(source->storeOp);
    target->clearValue.r  = source->clearColor.float32[0];
    target->clearValue.g  = source->clearColor.float32[1];
    target->clearValue.b  = source->clearColor.float32[2];
    target->clearValue.a  = source->clearColor.float32[3];
  }

  command->renderPassDesc.label = gpu_webgpuString(info->label);
  command->renderPassDesc.colorAttachmentCount = info->colorAttachmentCount;
  command->renderPassDesc.colorAttachments = command->colorAttachments;
  command->renderPass._priv = command;
  command->renderPass.label = info->label;
  return &command->renderPass;
}

static void
webgpu_destroyRenderPass(GPURenderPassDesc *pass) {
  GPU__UNUSED(pass);
}

void
webgpu_initRenderPass(GPUApiRenderPass *api) {
  api->beginRenderPass   = webgpu_beginRenderPass;
  api->destroyRenderPass = webgpu_destroyRenderPass;
}
