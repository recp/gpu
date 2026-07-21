#include "BindlessTexture.h"

#include <string.h>

enum {
  kSelectionBufferSize = 256u
};

static const uint8_t texturePixels[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT][16] = {
  {
    255u,  32u,  32u, 255u,   32u,  64u, 255u, 255u,
     32u,  64u, 255u, 255u,  255u,  32u,  32u, 255u
  },
  {
     32u, 255u,  96u, 255u,  255u, 224u,  32u, 255u,
    255u, 224u,  32u, 255u,   32u, 255u,  96u, 255u
  }
};

static const char *textureLabels[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT] = {
  "bindless-texture-0",
  "bindless-texture-1"
};

static GPUResult
create_layouts(GPUSampleBindlessTexture *state) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBindlessLayoutEXT           bindlessInfo       = {0};
  GPUBindGroupLayoutCreateInfo   layoutInfo         = {0};
  GPUPipelineLayoutCreateInfo    pipelineLayoutInfo = {0};
  GPUResult                      result;
  uint32_t                       entryCount;

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 3u ||
      entries[0].binding != 0u ||
      entries[0].arrayCount != GPU_SAMPLE_BINDLESS_RESOURCE_COUNT ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[1].binding != 2u ||
      entries[1].arrayCount != GPU_SAMPLE_BINDLESS_RESOURCE_COUNT ||
      entries[1].bindingType != GPU_BINDING_SAMPLER ||
      entries[2].binding != 4u || entries[2].arrayCount != 1u ||
      entries[2].bindingType != GPU_BINDING_UNIFORM_BUFFER) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  bindlessInfo.chain.sType      = GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT;
  bindlessInfo.chain.structSize = sizeof(bindlessInfo);
  bindlessInfo.sourceLayout     = state->shaderLayout->bindGroupLayouts[0];
  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.chain.pNext      = &bindlessInfo;
  layoutInfo.label            = "bindless-texture-layout";
  result = GPUCreateBindGroupLayout(state->device,
                                    &layoutInfo,
                                    &state->bindlessLayout);
  if (result != GPU_OK || !state->bindlessLayout) {
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  pipelineLayoutInfo.chain.sType =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize     = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label                = "bindless-texture-pipeline-layout";
  pipelineLayoutInfo.ppBindGroupLayouts   = &state->bindlessLayout;
  pipelineLayoutInfo.bindGroupLayoutCount = 1u;
  result = GPUCreatePipelineLayout(state->device,
                                   &pipelineLayoutInfo,
                                   &state->pipelineLayout);
  return result == GPU_OK && state->pipelineLayout
           ? GPU_OK
           : (result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE);
}

static GPUResult
create_pipeline(GPUSampleBindlessTexture *state) {
  GPUColorTargetState          colorTarget = {0};
  GPURenderPipelineCreateInfo  info        = {0};
  GPUResult                    result;

  colorTarget.format          = GPUGetSwapchainFormat(state->swapchain);
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "bindless-texture-pipeline";
  info.layout                  = state->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "bindless_vs";
  info.fragmentEntry           = "bindless_fs";
  info.pColorTargets           = &colorTarget;
  info.colorTargetCount        = 1u;
  info.depthStencilFormat      = GPU_FORMAT_UNDEFINED;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  return result == GPU_OK && state->pipeline
           ? GPU_OK
           : (result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE);
}

static GPUResult
create_texture(GPUSampleBindlessTexture *state, uint32_t index) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUResult                result;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = textureLabels[index];
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 2u;
  textureInfo.height           = 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  result = GPUCreateTexture(state->device,
                            &textureInfo,
                            &state->textures[index]);
  if (result != GPU_OK || !state->textures[index]) {
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  writeRegion.width        = 2u;
  writeRegion.height       = 2u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 8u;
  writeRegion.rowsPerImage = 2u;
  result = GPUQueueWriteTexture(state->queue,
                                state->textures[index],
                                &writeRegion,
                                texturePixels[index],
                                sizeof(texturePixels[index]));
  if (result != GPU_OK) {
    return result;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = textureLabels[index];
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  result = GPUCreateTextureView(state->textures[index],
                                &viewInfo,
                                &state->textureViews[index]);
  return result == GPU_OK && state->textureViews[index]
           ? GPU_OK
           : (result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE);
}

static GPUResult
create_resources(GPUSampleBindlessTexture *state) {
  GPUSamplerCreateInfo samplerInfo = {0};
  GPUBufferCreateInfo  bufferInfo  = {0};
  GPUResult            result;
  uint32_t             selection[64] = {0};

  for (uint32_t i = 0u; i < GPU_SAMPLE_BINDLESS_RESOURCE_COUNT; i++) {
    result = create_texture(state, i);
    if (result != GPU_OK) {
      return result;
    }
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "bindless-texture-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  result = GPUCreateSampler(state->device,
                            &samplerInfo,
                            false,
                            &state->sampler);
  if (result != GPU_OK || !state->sampler) {
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "bindless-texture-selection";
  bufferInfo.sizeBytes        = kSelectionBufferSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t i = 0u; i < GPU_SAMPLE_BINDLESS_RESOURCE_COUNT; i++) {
    result = GPUCreateBuffer(state->device,
                             &bufferInfo,
                             &state->selectionBuffers[i]);
    if (result != GPU_OK || !state->selectionBuffers[i]) {
      return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
    }
    selection[0] = i;
    result = GPUQueueWriteBuffer(state->queue,
                                 state->selectionBuffers[i],
                                 0u,
                                 selection,
                                 sizeof(selection));
    if (result != GPU_OK) {
      return result;
    }
  }
  return GPU_OK;
}

static GPUResult
create_groups(GPUSampleBindlessTexture *state) {
  GPUBindGroupEntry      entries[5] = {0};
  GPUBindGroupCreateInfo info       = {0};
  GPUResult              result;

  for (uint32_t i = 0u; i < GPU_SAMPLE_BINDLESS_RESOURCE_COUNT; i++) {
    entries[i].textureView = state->textureViews[i];
    entries[i].binding     = 0u;
    entries[i].arrayIndex  = i;
    entries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;

    entries[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT + i].sampler = state->sampler;
    entries[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT + i].binding = 2u;
    entries[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT + i].arrayIndex = i;
    entries[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT + i].bindingType =
      GPU_BINDING_SAMPLER;
  }
  entries[4].binding       = 4u;
  entries[4].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entries[4].buffer.size   = kSelectionBufferSize;
  info.chain.sType         = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize    = sizeof(info);
  info.label               = "bindless-texture-group";
  info.layout              = state->bindlessLayout;

  for (uint32_t i = 0u; i < GPU_SAMPLE_BINDLESS_RESOURCE_COUNT; i++) {
    result = GPUCreateBindGroup(state->device, &info, &state->groups[i]);
    if (result != GPU_OK || !state->groups[i]) {
      return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
    }
    entries[4].buffer.buffer = state->selectionBuffers[i];
    result = GPUUpdateBindGroupEXT(state->groups[i],
                                   GPU_ARRAY_LEN(entries),
                                   entries);
    if (result != GPU_OK) {
      return result;
    }
  }
  return GPU_OK;
}

GPUResult
GPUSampleBindlessTextureInit(GPUSampleBindlessTexture *state,
                             GPUDevice                *device,
                             GPUQueue                 *queue,
                             GPUSwapchain             *swapchain,
                             GPUShaderLibrary         *library,
                             GPUShaderLayout          *shaderLayout,
                             uint32_t                  width,
                             uint32_t                  height) {
  GPUResult result;

  if (!state || !device || !queue || !swapchain || !library || !shaderLayout ||
      !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts || !shaderLayout->bindGroupLayouts[0] ||
      width == 0u || height == 0u ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_BINDLESS)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(state, 0, sizeof(*state));
  state->device       = device;
  state->queue        = queue;
  state->swapchain    = swapchain;
  state->library      = library;
  state->shaderLayout = shaderLayout;
  state->width        = width;
  state->height       = height;
  result = create_layouts(state);
  if (result == GPU_OK) {
    result = create_pipeline(state);
  }
  if (result == GPU_OK) {
    result = create_resources(state);
  }
  if (result == GPU_OK) {
    result = create_groups(state);
  }
  if (result != GPU_OK) {
    GPUSampleBindlessTextureDestroy(state);
  }
  return result;
}

GPUResult
GPUSampleBindlessTextureResize(GPUSampleBindlessTexture *state,
                               uint32_t                  width,
                               uint32_t                  height) {
  if (!state || width == 0u || height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  state->width  = width;
  state->height = height;
  return GPU_OK;
}

static void
draw_halves(GPURenderPassEncoder *pass, GPUSampleBindlessTexture *state) {
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
  GPUBindRenderGroup(pass, 0u, state->groups[0], 0u, NULL);
  GPUDraw(pass, 6u, 1u, 0u, 0u);

  viewport.x     = (float)leftWidth;
  viewport.width = (float)(state->width - leftWidth);
  scissor.x      = (int32_t)leftWidth;
  scissor.width  = state->width - leftWidth;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderGroup(pass, 0u, state->groups[1], 0u, NULL);
  GPUDraw(pass, 6u, 1u, 0u, 0u);
}

GPUResult
GPUSampleBindlessTextureRender(GPUSampleBindlessTexture    *state,
                               void                        *completionSender,
                               GPUCommandBufferCompletionFn completion) {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color    = {0};
  GPURenderPassCreateInfo       passInfo = {0};
  GPUResult                     result;

  if (!state || !state->swapchain || !state->pipeline ||
      !state->groups[0] || !state->groups[1]) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  cmdb   = NULL;
  result = GPUAcquireCommandBuffer(state->queue, "bindless-frame", &cmdb);
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
  color.clearColor.float32[0] = 0.003f;
  color.clearColor.float32[1] = 0.008f;
  color.clearColor.float32[2] = 0.020f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "bindless-texture-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  draw_halves(pass, state);
  GPUEndRenderPass(pass);
  result = GPUFinishFrame(state->queue, cmdb, frame);
  if (result == GPU_OK) {
    state->frameCount++;
  }
  return result;
}

void
GPUSampleBindlessTextureDestroy(GPUSampleBindlessTexture *state) {
  if (!state) {
    return;
  }

  for (uint32_t i = 0u; i < GPU_SAMPLE_BINDLESS_RESOURCE_COUNT; i++) {
    GPUDestroyBindGroup(state->groups[i]);
  }
  GPUDestroyRenderPipeline(state->pipeline);
  for (uint32_t i = 0u; i < GPU_SAMPLE_BINDLESS_RESOURCE_COUNT; i++) {
    GPUDestroyBuffer(state->selectionBuffers[i]);
  }
  GPUDestroySampler(state->sampler);
  for (uint32_t i = 0u; i < GPU_SAMPLE_BINDLESS_RESOURCE_COUNT; i++) {
    GPUDestroyTextureView(state->textureViews[i]);
    GPUDestroyTexture(state->textures[i]);
  }
  GPUDestroyPipelineLayout(state->pipelineLayout);
  GPUDestroyBindGroupLayout(state->bindlessLayout);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  memset(state, 0, sizeof(*state));
}
