#include "MRTBlend.h"

#include <string.h>

static void
set_alpha_blend(GPUBlendState *blend) {
  blend->color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
  blend->color.dstFactor = GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend->color.op        = GPU_BLEND_OP_ADD;
  blend->alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
  blend->alpha.dstFactor = GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend->alpha.op        = GPU_BLEND_OP_ADD;
  blend->writeMask       = GPU_COLOR_WRITE_ALL;
  blend->enabled         = true;
}

static void
set_additive_blend(GPUBlendState *blend) {
  blend->color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
  blend->color.dstFactor = GPU_BLEND_FACTOR_ONE;
  blend->color.op        = GPU_BLEND_OP_ADD;
  blend->alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
  blend->alpha.dstFactor = GPU_BLEND_FACTOR_ONE;
  blend->alpha.op        = GPU_BLEND_OP_ADD;
  blend->writeMask       = GPU_COLOR_WRITE_ALL;
  blend->enabled         = true;
}

static GPUResult
create_pipelines(GPUSampleMRTBlend *state) {
  GPUColorTargetState         mrtTargets[2]   = {0};
  GPUColorTargetState         compositeTarget = {0};
  GPURenderPipelineCreateInfo mrtInfo          = {0};
  GPURenderPipelineCreateInfo compositeInfo    = {0};
  GPUResult                   result;

  mrtTargets[0].format = GPU_FORMAT_RGBA8_UNORM;
  mrtTargets[1].format = GPU_FORMAT_RGBA8_UNORM;
  set_alpha_blend(&mrtTargets[0].blend);
  set_additive_blend(&mrtTargets[1].blend);

  mrtInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  mrtInfo.chain.structSize        = sizeof(mrtInfo);
  mrtInfo.label                   = "mrt-blend-mrt-pipeline";
  mrtInfo.layout                  = state->shaderLayout->pipelineLayout;
  mrtInfo.library                 = state->library;
  mrtInfo.vertexEntry             = "fullscreen_vs";
  mrtInfo.fragmentEntry           = "mrt_fs";
  mrtInfo.pColorTargets           = mrtTargets;
  mrtInfo.colorTargetCount        = GPU_ARRAY_LEN(mrtTargets);
  mrtInfo.depthStencilFormat      = GPU_FORMAT_UNDEFINED;
  mrtInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  mrtInfo.cullMode                = GPU_CULL_MODE_NONE;
  mrtInfo.frontFace               = GPU_FRONT_FACE_CCW;
  mrtInfo.multisample.sampleCount = 1u;
  mrtInfo.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device,
                                   &mrtInfo,
                                   &state->mrtPipeline);
  if (result != GPU_OK || !state->mrtPipeline) {
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  compositeTarget.format          = GPUGetSwapchainFormat(state->swapchain);
  compositeTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  compositeInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  compositeInfo.chain.structSize        = sizeof(compositeInfo);
  compositeInfo.label                   = "mrt-blend-composite-pipeline";
  compositeInfo.layout                  = state->shaderLayout->pipelineLayout;
  compositeInfo.library                 = state->library;
  compositeInfo.vertexEntry             = "fullscreen_vs";
  compositeInfo.fragmentEntry           = "composite_fs";
  compositeInfo.pColorTargets           = &compositeTarget;
  compositeInfo.colorTargetCount        = 1u;
  compositeInfo.depthStencilFormat      = GPU_FORMAT_UNDEFINED;
  compositeInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  compositeInfo.cullMode                = GPU_CULL_MODE_NONE;
  compositeInfo.frontFace               = GPU_FRONT_FACE_CCW;
  compositeInfo.multisample.sampleCount = 1u;
  compositeInfo.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device,
                                   &compositeInfo,
                                   &state->compositePipeline);
  return result == GPU_OK && state->compositePipeline
           ? GPU_OK
           : (result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE);
}

static GPUResult
create_sampler(GPUSampleMRTBlend *state) {
  GPUSamplerCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "mrt-blend-linear-sampler";
  info.desc.minFilter   = GPU_FILTER_LINEAR;
  info.desc.magFilter   = GPU_FILTER_LINEAR;
  info.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  info.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  return GPUCreateSampler(state->device,
                          &info,
                          false,
                          &state->sampler);
}

static GPUResult
create_targets(GPUSampleMRTBlend *state, uint32_t width, uint32_t height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUBindGroupEntry        entries[2]  = {0};
  GPUBindGroupCreateInfo   groupInfo   = {0};
  GPUTexture              *targets[2]  = {0};
  GPUTextureView          *views[2]    = {0};
  GPUBindGroup            *groups[2]   = {0};
  GPUResult                result      = GPU_OK;

  if (width == 0u || height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_SAMPLED;
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.format              = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(targets); i++) {
    textureInfo.label = i == 0u ? "mrt-blend-alpha-target"
                                : "mrt-blend-additive-target";
    viewInfo.label    = i == 0u ? "mrt-blend-alpha-view"
                                : "mrt-blend-additive-view";
    result = GPUCreateTexture(state->device, &textureInfo, &targets[i]);
    if (result != GPU_OK) {
      goto cleanup;
    }
    result = GPUCreateTextureView(targets[i], &viewInfo, &views[i]);
    if (result != GPU_OK) {
      goto cleanup;
    }
  }

  entries[0].binding     = 0u;
  entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entries[1].sampler     = state->sampler;
  entries[1].binding     = 1u;
  entries[1].bindingType = GPU_BINDING_SAMPLER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(groups); i++) {
    entries[0].textureView = views[i];
    groupInfo.label = i == 0u ? "mrt-blend-alpha-group"
                              : "mrt-blend-additive-group";
    result = GPUCreateBindGroup(state->device, &groupInfo, &groups[i]);
    if (result != GPU_OK) {
      goto cleanup;
    }
  }

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(targets); i++) {
    GPUDestroyBindGroup(state->compositeGroups[i]);
    GPUDestroyTextureView(state->targetViews[i]);
    GPUDestroyTexture(state->targets[i]);
    state->targets[i]         = targets[i];
    state->targetViews[i]     = views[i];
    state->compositeGroups[i] = groups[i];
  }
  state->width      = width;
  state->height     = height;
  state->frameCount = 0u;
  return GPU_OK;

cleanup:
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(targets); i++) {
    GPUDestroyBindGroup(groups[i]);
    GPUDestroyTextureView(views[i]);
    GPUDestroyTexture(targets[i]);
  }
  return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
}

GPUResult
GPUSampleMRTBlendInit(GPUSampleMRTBlend *state,
                      GPUDevice         *device,
                      GPUQueue          *queue,
                      GPUSwapchain      *swapchain,
                      GPUShaderLibrary  *library,
                      GPUShaderLayout   *shaderLayout,
                      uint32_t           width,
                      uint32_t           height) {
  GPUResult result;

  if (!state || !device || !queue || !swapchain || !library || !shaderLayout ||
      !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts || !shaderLayout->bindGroupLayouts[0]) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(state, 0, sizeof(*state));
  state->device       = device;
  state->queue        = queue;
  state->swapchain    = swapchain;
  state->library      = library;
  state->shaderLayout = shaderLayout;
  result = create_pipelines(state);
  if (result == GPU_OK) {
    result = create_sampler(state);
  }
  if (result == GPU_OK) {
    result = create_targets(state, width, height);
  }
  if (result != GPU_OK) {
    GPUSampleMRTBlendDestroy(state);
  }
  return result;
}

GPUResult
GPUSampleMRTBlendResize(GPUSampleMRTBlend *state,
                        uint32_t           width,
                        uint32_t           height) {
  if (!state || width == 0u || height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (state->width == width && state->height == height) {
    return GPU_OK;
  }
  return create_targets(state, width, height);
}

static void
render_composite(GPURenderPassEncoder *pass, GPUSampleMRTBlend *state) {
  GPUViewport    viewport = {0};
  GPUScissorRect scissor  = {0};
  uint32_t       leftWidth;

  leftWidth         = state->width / 2u;
  viewport.y        = 0.0f;
  viewport.width    = (float)leftWidth;
  viewport.height   = (float)state->height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  scissor.width     = leftWidth;
  scissor.height    = state->height;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderGroup(pass, 0u, state->compositeGroups[0], 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);

  viewport.x     = (float)leftWidth;
  viewport.width = (float)(state->width - leftWidth);
  scissor.x      = (int32_t)leftWidth;
  scissor.width  = state->width - leftWidth;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderGroup(pass, 0u, state->compositeGroups[1], 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
}

GPUResult
GPUSampleMRTBlendRender(GPUSampleMRTBlend           *state,
                        void                        *completionSender,
                        GPUCommandBufferCompletionFn completion) {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  mrtColors[2]     = {0};
  GPURenderPassColorAttachment  compositeColor   = {0};
  GPURenderPassCreateInfo       mrtPassInfo       = {0};
  GPURenderPassCreateInfo       compositePassInfo = {0};
  GPUTextureBarrier             targetBarriers[2] = {0};
  GPUBarrierBatch               barrierBatch      = {0};
  GPUResult                     result;

  if (!state || !state->swapchain || !state->mrtPipeline ||
      !state->compositePipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  cmdb = NULL;
  result = GPUAcquireCommandBuffer(state->queue, "mrt-blend-frame", &cmdb);
  if (result != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  if (completion) {
    GPUSetCommandBufferCompletionHandler(cmdb, completionSender, completion);
  }

  mrtColors[0].view                  = state->targetViews[0];
  mrtColors[0].loadOp                = GPU_LOAD_OP_CLEAR;
  mrtColors[0].storeOp               = GPU_STORE_OP_STORE;
  mrtColors[0].clearColor.float32[0] = 0.055f;
  mrtColors[0].clearColor.float32[1] = 0.012f;
  mrtColors[0].clearColor.float32[2] = 0.008f;
  mrtColors[0].clearColor.float32[3] = 1.0f;
  mrtColors[1].view                  = state->targetViews[1];
  mrtColors[1].loadOp                = GPU_LOAD_OP_CLEAR;
  mrtColors[1].storeOp               = GPU_STORE_OP_STORE;
  mrtColors[1].clearColor.float32[0] = 0.004f;
  mrtColors[1].clearColor.float32[1] = 0.018f;
  mrtColors[1].clearColor.float32[2] = 0.060f;
  mrtColors[1].clearColor.float32[3] = 1.0f;
  mrtPassInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  mrtPassInfo.chain.structSize     = sizeof(mrtPassInfo);
  mrtPassInfo.label                = "mrt-blend-offscreen-pass";
  mrtPassInfo.pColorAttachments    = mrtColors;
  mrtPassInfo.colorAttachmentCount = GPU_ARRAY_LEN(mrtColors);
  pass = GPUBeginRenderPass(cmdb, &mrtPassInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  GPUBindRenderPipeline(pass, state->mrtPipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(targetBarriers); i++) {
    targetBarriers[i].texture    = state->targets[i];
    targetBarriers[i].srcAccess  = GPU_ACCESS_COLOR_WRITE;
    targetBarriers[i].dstAccess  = GPU_ACCESS_SHADER_READ;
    targetBarriers[i].mipCount   = 1u;
    targetBarriers[i].layerCount = 1u;
  }
  barrierBatch.pTextureBarriers    = targetBarriers;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.textureBarrierCount = GPU_ARRAY_LEN(targetBarriers);
  GPUEncodeBarriers(cmdb, &barrierBatch);

  compositeColor.view                  = GPUFrameGetTargetView(frame);
  compositeColor.loadOp                = GPU_LOAD_OP_CLEAR;
  compositeColor.storeOp               = GPU_STORE_OP_STORE;
  compositeColor.clearColor.float32[0] = 0.003f;
  compositeColor.clearColor.float32[1] = 0.008f;
  compositeColor.clearColor.float32[2] = 0.020f;
  compositeColor.clearColor.float32[3] = 1.0f;
  compositePassInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  compositePassInfo.chain.structSize     = sizeof(compositePassInfo);
  compositePassInfo.label                = "mrt-blend-composite-pass";
  compositePassInfo.pColorAttachments    = &compositeColor;
  compositePassInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &compositePassInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  GPUBindRenderPipeline(pass, state->compositePipeline);
  render_composite(pass, state);
  GPUEndRenderPass(pass);

  result = GPUFinishFrame(state->queue, cmdb, frame);
  if (result == GPU_OK) {
    state->frameCount++;
  }
  return result;
}

void
GPUSampleMRTBlendDestroy(GPUSampleMRTBlend *state) {
  if (!state) {
    return;
  }

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(state->targets); i++) {
    GPUDestroyBindGroup(state->compositeGroups[i]);
  }
  GPUDestroySampler(state->sampler);
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(state->targets); i++) {
    GPUDestroyTextureView(state->targetViews[i]);
    GPUDestroyTexture(state->targets[i]);
  }
  GPUDestroyRenderPipeline(state->compositePipeline);
  GPUDestroyRenderPipeline(state->mrtPipeline);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  memset(state, 0, sizeof(*state));
}
