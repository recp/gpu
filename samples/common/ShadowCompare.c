#include "ShadowCompare.h"

#include <string.h>

static GPUResult
create_shadow_group(GPUSampleShadowCompare *state,
                    GPUTextureView         *view,
                    GPUBindGroup          **outGroup) {
  GPUBindGroupEntry      entry = {0};
  GPUBindGroupCreateInfo info  = {0};

  entry.textureView      = view;
  entry.binding          = 0u;
  entry.bindingType      = GPU_BINDING_SAMPLED_TEXTURE;
  info.chain.sType       = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize  = sizeof(info);
  info.label             = "shadow-compare-texture-group";
  info.layout            = state->shaderLayout->bindGroupLayouts[0];
  info.pEntries          = &entry;
  info.entryCount        = 1u;
  return GPUCreateBindGroup(state->device, &info, outGroup);
}

static GPUResult
create_depth_target(GPUSampleShadowCompare *state,
                    uint32_t                width,
                    uint32_t                height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;
  GPUBindGroup            *group;
  GPUResult                result;

  texture = NULL;
  view    = NULL;
  group   = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "shadow-compare-depth";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL |
                                 GPU_TEXTURE_USAGE_SAMPLED;
  result = GPUCreateTexture(state->device, &textureInfo, &texture);
  if (result != GPU_OK) {
    return result;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "shadow-compare-depth-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  result = GPUCreateTextureView(texture, &viewInfo, &view);
  if (result == GPU_OK) {
    result = create_shadow_group(state, view, &group);
  }
  if (result != GPU_OK) {
    GPUDestroyBindGroup(group);
    GPUDestroyTextureView(view);
    GPUDestroyTexture(texture);
    return result;
  }

  GPUDestroyBindGroup(state->shadowGroup);
  GPUDestroyTextureView(state->depthView);
  GPUDestroyTexture(state->depthTexture);
  state->shadowGroup  = group;
  state->depthView    = view;
  state->depthTexture = texture;
  state->width        = width;
  state->height       = height;
  return GPU_OK;
}

static GPUResult
create_pipelines(GPUSampleShadowCompare *state) {
  GPUColorTargetState         color = {0};
  GPUDepthStencilState        depth = {0};
  GPURenderPipelineCreateInfo info  = {0};
  GPUResult                   result;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "shadow-compare-depth-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "shadow_depth_vs";
  info.fragmentEntry           = "shadow_depth_fs";
  info.pColorTargets           = &color;
  info.pDepthStencilState      = &depth;
  info.colorTargetCount        = 1u;
  info.depthStencilFormat      = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device,
                                   &info,
                                   &state->depthPipeline);
  if (result != GPU_OK) {
    return result;
  }

  info.label              = "shadow-compare-preview-pipeline";
  info.vertexEntry        = "shadow_preview_vs";
  info.fragmentEntry      = "shadow_preview_fs";
  info.pDepthStencilState = NULL;
  info.depthStencilFormat = GPU_FORMAT_UNDEFINED;
  result = GPUCreateRenderPipeline(state->device,
                                   &info,
                                   &state->previewPipeline);
  if (result != GPU_OK) {
    return result;
  }

  info.label         = "shadow-compare-preview-cool-pipeline";
  info.fragmentEntry = "shadow_preview_cool_fs";
  return GPUCreateRenderPipeline(state->device,
                                 &info,
                                 &state->previewCoolPipeline);
}

GPUResult
GPUSampleShadowCompareInit(GPUSampleShadowCompare *state,
                           GPUDevice               *device,
                           GPUQueue                *queue,
                           GPUSwapchain            *swapchain,
                           GPUShaderLibrary        *library,
                           GPUShaderLayout         *shaderLayout,
                           uint32_t                 width,
                           uint32_t                 height) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;
  GPUResult                      result;

  if (!state || !device || !queue || !swapchain || !library || !shaderLayout ||
      !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts || !shaderLayout->bindGroupLayouts[0] ||
      width == 0u || height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(state, 0, sizeof(*state));
  state->device       = device;
  state->queue        = queue;
  state->swapchain    = swapchain;
  state->library      = library;
  state->shaderLayout = shaderLayout;

  entries = GPUGetBindGroupLayoutEntries(shaderLayout->bindGroupLayouts[0],
                                         &entryCount);
  if (!entries || entryCount != 1u || entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[0].sampledTexture.sampleType != GPU_TEXTURE_SAMPLE_TYPE_DEPTH ||
      entries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    GPUSampleShadowCompareDestroy(state);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = create_pipelines(state);
  if (result == GPU_OK) {
    result = create_depth_target(state, width, height);
  }
  if (result != GPU_OK) {
    GPUSampleShadowCompareDestroy(state);
  }
  return result;
}

GPUResult
GPUSampleShadowCompareResize(GPUSampleShadowCompare *state,
                             uint32_t                 width,
                             uint32_t                 height) {
  if (!state || width == 0u || height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (state->width == width && state->height == height) {
    return GPU_OK;
  }
  return create_depth_target(state, width, height);
}

static void
render_preview(GPURenderPassEncoder *pass, GPUSampleShadowCompare *state) {
  GPUViewport    viewport = {0};
  GPUScissorRect scissor  = {0};
  uint32_t       leftWidth;

  leftWidth         = state->width / 2u;
  viewport.width    = (float)leftWidth;
  viewport.height   = (float)state->height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  scissor.width     = leftWidth;
  scissor.height    = state->height;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderPipeline(pass, state->previewPipeline);
  GPUBindRenderGroup(pass, 0u, state->shadowGroup, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);

  viewport.x     = (float)leftWidth;
  viewport.width = (float)(state->width - leftWidth);
  scissor.x      = (int32_t)leftWidth;
  scissor.width  = state->width - leftWidth;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderPipeline(pass, state->previewCoolPipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
}

GPUResult
GPUSampleShadowCompareRender(GPUSampleShadowCompare        *state,
                             void                          *completionSender,
                             GPUCommandBufferCompletionFn   completion) {
  GPUFrame                            *frame;
  GPUCommandBuffer                    *cmdb;
  GPURenderPassEncoder                *pass;
  GPURenderPassColorAttachment         color = {0};
  GPURenderPassDepthStencilAttachment  depth = {0};
  GPURenderPassCreateInfo              passInfo     = {0};
  GPUTextureBarrier                    depthBarrier = {0};
  GPUBarrierBatch                      barriers     = {0};
  GPUResult                            result;

  if (!state || !state->swapchain || !state->depthPipeline ||
      !state->previewPipeline || !state->previewCoolPipeline ||
      !state->shadowGroup) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  cmdb   = NULL;
  result = GPUAcquireCommandBuffer(state->queue, "shadow-compare-frame", &cmdb);
  if (result != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  if (completion) {
    GPUSetCommandBufferCompletionHandler(cmdb, completionSender, completion);
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.04f;
  color.clearColor.float32[1] = 0.07f;
  color.clearColor.float32[2] = 0.12f;
  color.clearColor.float32[3] = 1.0f;
  depth.view                  = state->depthView;
  depth.depthLoadOp           = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp          = GPU_STORE_OP_STORE;
  depth.stencilLoadOp         = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp        = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth            = 1.0f;
  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "shadow-compare-depth-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depth;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  GPUBindRenderPipeline(pass, state->depthPipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  depthBarrier.texture    = state->depthTexture;
  depthBarrier.srcAccess  = GPU_ACCESS_DEPTH_WRITE;
  depthBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  depthBarrier.mipCount   = 1u;
  depthBarrier.layerCount = 1u;
  barriers.pTextureBarriers    = &depthBarrier;
  barriers.srcStages           = GPU_STAGE_FRAGMENT;
  barriers.dstStages           = GPU_STAGE_FRAGMENT;
  barriers.textureBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &barriers);

  color.loadOp                     = GPU_LOAD_OP_LOAD;
  passInfo.label                   = "shadow-compare-preview-pass";
  passInfo.pDepthStencilAttachment = NULL;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }
  render_preview(pass, state);
  GPUEndRenderPass(pass);

  result = GPUFinishFrame(state->queue, cmdb, frame);
  if (result == GPU_OK) {
    state->frameCount++;
  }
  return result;
}

void
GPUSampleShadowCompareDestroy(GPUSampleShadowCompare *state) {
  if (!state) {
    return;
  }

  GPUDestroyBindGroup(state->shadowGroup);
  GPUDestroyTextureView(state->depthView);
  GPUDestroyTexture(state->depthTexture);
  GPUDestroyRenderPipeline(state->previewCoolPipeline);
  GPUDestroyRenderPipeline(state->previewPipeline);
  GPUDestroyRenderPipeline(state->depthPipeline);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  memset(state, 0, sizeof(*state));
}
