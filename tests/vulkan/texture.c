#include <gpu/gpu.h>

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct VulkanTextureVertex {
  float position[4];
  float uv[2];
} VulkanTextureVertex;

static int
pixel_matches(const uint8_t pixel[4],
              uint8_t       red,
              uint8_t       green,
              uint8_t       blue) {
  return abs((int)pixel[0] - red) <= 3 &&
         abs((int)pixel[1] - green) <= 3 &&
         abs((int)pixel[2] - blue) <= 3 &&
         pixel[3] >= 250u;
}

static const GPUBindGroupLayoutEntry*
find_layout_entry(GPUBindGroupLayout *layout,
                  uint32_t            binding,
                  GPUBindingType      type) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       count;

  entries = GPUGetBindGroupLayoutEntries(layout, &count);
  for (uint32_t i = 0u; entries && i < count; i++) {
    if (entries[i].binding == binding && entries[i].bindingType == type) {
      return &entries[i];
    }
  }
  return NULL;
}

int
gpu_test_vulkan_texture(GPUDevice  *device,
                        const void *artifact,
                        uint64_t    artifactSize) {
  static const VulkanTextureVertex kVertices[] = {
    {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{ 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}}
  };
  static const uint8_t kCheckerPixels[] = {
    255u,   0u,   0u, 255u,   0u, 255u,   0u, 255u,
      0u,   0u, 255u, 255u, 255u, 255u, 255u, 255u
  };
  static const float kTint[] = {0.9f, 0.95f, 1.0f, 1.0f};
  const uint32_t width  = 4u;
  const uint32_t height = 4u;
  GPUQueue                        *queue;
  GPUShaderLibrary                *library        = NULL;
  GPUShaderLayout                 *shaderLayout   = NULL;
  GPURenderPipeline               *pipeline       = NULL;
  GPUBuffer                       *vertexBuffer   = NULL;
  GPUBuffer                       *uniformBuffer  = NULL;
  GPUBuffer                       *readbackBuffer = NULL;
  GPUTexture                      *sampleTexture  = NULL;
  GPUTexture                      *target         = NULL;
  GPUTextureView                  *sampleView     = NULL;
  GPUTextureView                  *targetView     = NULL;
  GPUSampler                      *sampler        = NULL;
  GPUBindGroup                    *fragmentGroup  = NULL;
  GPUBindGroup                    *samplerGroup   = NULL;
  GPUCommandBuffer                *cmdb           = NULL;
  GPUCommandBuffer                *buffers[1];
  GPURenderPassEncoder            *renderPass     = NULL;
  GPUCopyPassEncoder              *copyPass       = NULL;
  GPUFence                        *fence          = NULL;
  GPUVertexAttribute               attributes[2]  = {{0}};
  GPUVertexBufferLayout            vertexLayout   = {0};
  GPUColorTargetState              colorTarget    = {0};
  GPURenderPipelineCreateInfo      pipelineInfo   = {0};
  GPUBufferCreateInfo              bufferInfo     = {0};
  GPUTextureCreateInfo             textureInfo    = {0};
  GPUTextureViewCreateInfo         viewInfo       = {0};
  GPUTextureWriteRegion            writeRegion    = {0};
  GPUSamplerCreateInfo             samplerInfo    = {0};
  GPUBindGroupEntry                fragmentEntries[2] = {0};
  GPUBindGroupEntry                samplerEntry   = {0};
  GPUBindGroupCreateInfo           groupInfo      = {0};
  GPURenderPassColorAttachment     color          = {0};
  GPURenderPassCreateInfo          passInfo       = {0};
  GPUViewport                      viewport       = {0};
  GPUScissorRect                   scissor        = {0};
  GPUBufferBinding                 vertexBinding  = {0};
  GPUTextureBarrier                textureBarrier = {0};
  GPUBarrierBatch                  barrierBatch   = {0};
  GPUBufferTextureCopyRegion       copyRegion     = {0};
  GPUQueueSubmitInfo               submitInfo     = {0};
  uint8_t                          pixels[4u * 4u * 4u] = {0};
  const GPUBindGroupLayoutEntry   *textureEntry;
  const GPUBindGroupLayoutEntry   *uniformEntry;
  const GPUBindGroupLayoutEntry   *samplerLayoutEntry;
  size_t                           bottomLeftOffset;
  size_t                           bottomRightOffset;
  size_t                           topLeftOffset;
  size_t                           topRightOffset;
  int                              ok = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue ||
      GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "failed to create Vulkan texture shader layout\n");
    goto cleanup;
  }

  textureEntry = find_layout_entry(shaderLayout->bindGroupLayouts[0],
                                   0u,
                                   GPU_BINDING_SAMPLED_TEXTURE);
  uniformEntry = find_layout_entry(shaderLayout->bindGroupLayouts[0],
                                   1u,
                                   GPU_BINDING_UNIFORM_BUFFER);
  samplerLayoutEntry = find_layout_entry(shaderLayout->bindGroupLayouts[1],
                                         0u,
                                         GPU_BINDING_SAMPLER);
  if (!textureEntry || !uniformEntry || !samplerLayoutEntry ||
      textureEntry->visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      uniformEntry->visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      samplerLayoutEntry->visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    fprintf(stderr, "Vulkan texture reflection layout mismatch\n");
    goto cleanup;
  }

  attributes[0].shaderLocation = 0u;
  attributes[0].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[0].offset         = offsetof(VulkanTextureVertex, position);
  attributes[1].shaderLocation = 1u;
  attributes[1].format         = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[1].offset         = offsetof(VulkanTextureVertex, uv);
  vertexLayout.strideBytes     = sizeof(VulkanTextureVertex);
  vertexLayout.stepMode        = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount  = 2u;
  vertexLayout.pAttributes     = attributes;
  colorTarget.format            = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask   = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "vulkan-usl-texture";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.vertexEntry      = "quad_vs";
  pipelineInfo.fragmentEntry    = "quad_fs";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts     = &vertexLayout;
  pipelineInfo.colorTargetCount          = 1u;
  pipelineInfo.pColorTargets             = &colorTarget;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_BACK;
  pipelineInfo.frontFace         = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "failed to create Vulkan texture pipeline\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-usl-texture-vertices";
  bufferInfo.sizeBytes        = sizeof(kVertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &vertexBuffer) != GPU_OK ||
      !vertexBuffer ||
      GPUQueueWriteBuffer(queue,
                          vertexBuffer,
                          0u,
                          kVertices,
                          sizeof(kVertices)) != GPU_OK) {
    fprintf(stderr, "failed to create Vulkan texture vertex buffer\n");
    goto cleanup;
  }

  bufferInfo.label     = "vulkan-usl-texture-uniforms";
  bufferInfo.sizeBytes = sizeof(kTint);
  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &uniformBuffer) != GPU_OK ||
      !uniformBuffer ||
      GPUQueueWriteBuffer(queue,
                          uniformBuffer,
                          0u,
                          kTint,
                          sizeof(kTint)) != GPU_OK) {
    fprintf(stderr, "failed to create Vulkan texture uniform buffer\n");
    goto cleanup;
  }

  bufferInfo.label     = "vulkan-usl-texture-readback";
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "failed to create Vulkan texture readback buffer\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "vulkan-usl-sampled-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 2u;
  textureInfo.height           = 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &sampleTexture) != GPU_OK ||
      !sampleTexture) {
    fprintf(stderr, "failed to create Vulkan sampled texture\n");
    goto cleanup;
  }

  writeRegion.width        = 2u;
  writeRegion.height       = 2u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 2u * 4u;
  writeRegion.rowsPerImage = 2u;
  if (GPUQueueWriteTexture(queue,
                           sampleTexture,
                           &writeRegion,
                           kCheckerPixels,
                           sizeof(kCheckerPixels)) != GPU_OK) {
    fprintf(stderr, "failed to upload Vulkan sampled texture\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "vulkan-usl-sampled-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(sampleTexture, &viewInfo, &sampleView) != GPU_OK ||
      !sampleView) {
    fprintf(stderr, "failed to create Vulkan sampled view\n");
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "vulkan-usl-texture-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(device, &samplerInfo, false, &sampler) != GPU_OK ||
      !sampler) {
    fprintf(stderr, "failed to create Vulkan texture sampler\n");
    goto cleanup;
  }

  fragmentEntries[0].binding       = 0u;
  fragmentEntries[0].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  fragmentEntries[0].textureView   = sampleView;
  fragmentEntries[1].binding       = 1u;
  fragmentEntries[1].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  fragmentEntries[1].buffer.buffer = uniformBuffer;
  fragmentEntries[1].buffer.size   = sizeof(kTint);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "vulkan-usl-texture-group0";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = fragmentEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &fragmentGroup) != GPU_OK ||
      !fragmentGroup) {
    fprintf(stderr, "failed to create Vulkan texture group 0\n");
    goto cleanup;
  }

  samplerEntry.binding     = 0u;
  samplerEntry.bindingType = GPU_BINDING_SAMPLER;
  samplerEntry.sampler     = sampler;
  groupInfo.label          = "vulkan-usl-texture-group1";
  groupInfo.layout         = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount     = 1u;
  groupInfo.pEntries       = &samplerEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &samplerGroup) != GPU_OK ||
      !samplerGroup) {
    fprintf(stderr, "failed to create Vulkan texture group 1\n");
    goto cleanup;
  }

  textureInfo.label         = "vulkan-usl-texture-target";
  textureInfo.width         = width;
  textureInfo.height        = height;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COLOR_TARGET |
                              GPU_TEXTURE_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &target) != GPU_OK || !target) {
    fprintf(stderr, "failed to create Vulkan texture target\n");
    goto cleanup;
  }
  viewInfo.label = "vulkan-usl-texture-target-view";
  if (GPUCreateTextureView(target, &viewInfo, &targetView) != GPU_OK ||
      !targetView) {
    fprintf(stderr, "failed to create Vulkan texture target view\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "vulkan-usl-texture", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire Vulkan texture command buffer\n");
    goto cleanup;
  }
  color.view                  = targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "vulkan-usl-texture";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "failed to begin Vulkan texture render pass\n");
    goto cleanup;
  }

  vertexBinding.buffer = vertexBuffer;
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUBindRenderGroup(renderPass, 0u, fragmentGroup, 0u, NULL);
  GPUBindRenderGroup(renderPass, 1u, samplerGroup, 0u, NULL);
  viewport.width    = (float)width;
  viewport.height   = (float)height;
  viewport.maxDepth = 1.0f;
  scissor.x      = -2;
  scissor.y      = -3;
  scissor.width  = width + 2u;
  scissor.height = height + 3u;
  GPUSetViewport(renderPass, &viewport);
  GPUSetScissor(renderPass, &scissor);
  GPUDraw(renderPass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarrier.texture    = target;
  textureBarrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "vulkan-usl-texture-readback");
  if (!copyPass) {
    fprintf(stderr, "failed to begin Vulkan texture copy pass\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow        = width * 4u;
  copyRegion.rowsPerImage       = height;
  copyRegion.texture.width      = width;
  copyRegion.texture.height     = height;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, target, readbackBuffer, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create Vulkan texture fence\n");
    goto cleanup;
  }
  buffers[0]                       = cmdb;
  submitInfo.chain.sType           = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize      = sizeof(submitInfo);
  submitInfo.commandBufferCount    = 1u;
  submitInfo.ppCommandBuffers      = buffers;
  submitInfo.fence                 = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "Vulkan texture submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "Vulkan texture readback failed\n");
    goto cleanup;
  }

  topLeftOffset     = 0u;
  topRightOffset    = ((size_t)width - 1u) * 4u;
  bottomLeftOffset  = ((size_t)height - 1u) * width * 4u;
  bottomRightOffset = ((size_t)width * height - 1u) * 4u;
  if (!pixel_matches(&pixels[topLeftOffset], 230u, 0u, 0u) ||
      !pixel_matches(&pixels[topRightOffset], 0u, 242u, 0u) ||
      !pixel_matches(&pixels[bottomLeftOffset], 0u, 0u, 255u) ||
      !pixel_matches(&pixels[bottomRightOffset], 230u, 242u, 255u)) {
    fprintf(stderr,
            "Vulkan texture orientation mismatch: "
            "tl=%u,%u,%u tr=%u,%u,%u bl=%u,%u,%u br=%u,%u,%u\n",
            pixels[topLeftOffset + 0u],
            pixels[topLeftOffset + 1u],
            pixels[topLeftOffset + 2u],
            pixels[topRightOffset + 0u],
            pixels[topRightOffset + 1u],
            pixels[topRightOffset + 2u],
            pixels[bottomLeftOffset + 0u],
            pixels[bottomLeftOffset + 1u],
            pixels[bottomLeftOffset + 2u],
            pixels[bottomRightOffset + 0u],
            pixels[bottomRightOffset + 1u],
            pixels[bottomRightOffset + 2u]);
    goto cleanup;
  }

  ok = 1;

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBindGroup(samplerGroup);
  GPUDestroyBindGroup(fragmentGroup);
  GPUDestroySampler(sampler);
  GPUDestroyTextureView(sampleView);
  GPUDestroyTexture(sampleTexture);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(uniformBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}
