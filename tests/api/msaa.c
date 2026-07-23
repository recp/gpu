#include "test.h"

enum {
  MSAA_WIDTH        = 4u,
  MSAA_HEIGHT       = 4u,
  MSAA_SAMPLE_COUNT = 4u,
  MSAA_ROW_PITCH    = 256u
};

static int
create_msaa_texture(GPUDevice           *device,
                    const char          *label,
                    uint32_t             sampleCount,
                    GPUTextureUsageFlags usage,
                    GPUTexture         **outTexture,
                    GPUTextureView     **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  *outTexture = NULL;
  *outView    = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = MSAA_WIDTH;
  textureInfo.height           = MSAA_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = sampleCount;
  textureInfo.usage            = usage;
  if (GPUCreateTexture(device, &textureInfo, outTexture) != GPU_OK ||
      !*outTexture) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(*outTexture, &viewInfo, outView) != GPU_OK ||
      !*outView) {
    GPUDestroyTexture(*outTexture);
    *outTexture = NULL;
    return 0;
  }
  return 1;
}

static int
msaa_half_red(const uint8_t *pixels, size_t offset) {
  return pixels[offset + 0u] <= 2u &&
         pixels[offset + 1u] <= 2u &&
         pixels[offset + 2u] >= 96u &&
         pixels[offset + 2u] <= 160u;
}

static int
msaa_pixels_match(const uint8_t *pixels, size_t a, size_t b) {
  for (uint32_t i = 0u; i < 4u; i++) {
    int delta = (int)pixels[a + i] - (int)pixels[b + i];

    if (delta < -2 || delta > 2) {
      return 0;
    }
  }
  return 1;
}

int
gpu_test_msaa_resolve_sample(GPUDevice *device, const char *bytecodePath) {
  static const float kFullscreenTriangle[] = {
    -1.0f, -1.0f,
     3.0f, -1.0f,
    -1.0f,  3.0f
  };
  const uint64_t imageBytes = (uint64_t)MSAA_ROW_PITCH * MSAA_HEIGHT;
  const uint64_t readbackBytes = imageBytes * 2u;
  const size_t centerOffset = (size_t)2u * MSAA_ROW_PITCH + 2u * 4u;
  GPUQueue                       *queue;
  GPUShaderLibrary               *library           = NULL;
  GPUShaderLayout                *shaderLayout       = NULL;
  GPURenderPipeline              *sourcePipeline     = NULL;
  GPURenderPipeline              *comparePipeline    = NULL;
  GPUBindGroup                   *compareGroup       = NULL;
  GPUBuffer                      *vertexBuffer       = NULL;
  GPUBuffer                      *readbackBuffer     = NULL;
  GPUTexture                     *sourceTexture      = NULL;
  GPUTexture                     *resolveTexture     = NULL;
  GPUTexture                     *manualTexture      = NULL;
  GPUTexture                     *resolvedTexture    = NULL;
  GPUTextureView                 *sourceView         = NULL;
  GPUTextureView                 *resolveView        = NULL;
  GPUTextureView                 *manualView         = NULL;
  GPUTextureView                 *resolvedView       = NULL;
  GPUCommandBuffer               *cmdb               = NULL;
  GPURenderPassEncoder           *renderPass         = NULL;
  GPUCopyPassEncoder             *copyPass           = NULL;
  GPUFence                       *fence              = NULL;
  void                           *bytecode;
  uint64_t                        bytecodeSize;
  GPUCommandBuffer               *buffers[1];
  GPUColorTargetState             sourceColor        = {0};
  GPUColorTargetState             compareColors[2]   = {{0}};
  GPUVertexAttribute              attribute          = {0};
  GPUVertexBufferLayout           vertexLayout       = {0};
  GPURenderPipelineCreateInfo     sourceInfo         = {0};
  GPURenderPipelineCreateInfo     compareInfo        = {0};
  GPUBufferCreateInfo             bufferInfo         = {0};
  GPUBindGroupEntry               groupEntries[2]    = {{0}};
  GPUBindGroupCreateInfo          groupInfo          = {0};
  GPURenderPassColorAttachment    sourceColorPass    = {0};
  GPURenderPassColorAttachment    compareColorPass[2] = {{0}};
  GPURenderPassCreateInfo         passInfo           = {0};
  GPUBufferBinding                vertexBinding      = {0};
  GPUTextureBarrier               textureBarriers[2] = {{0}};
  GPUBarrierBatch                 barrierBatch       = {0};
  GPUBufferTextureCopyRegion      copyRegion         = {0};
  GPUQueueSubmitInfo              submitInfo         = {0};
  uint8_t                         pixels[2u * MSAA_ROW_PITCH * MSAA_HEIGHT] = {0};
  int                             ok                 = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for MSAA conformance\n");
    return 0;
  }

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!bytecode ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library) {
    fprintf(stderr, "failed to create MSAA conformance shader library\n");
    free(bytecode);
    return 0;
  }
  free(bytecode);
  if (GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0]) {
    fprintf(stderr, "failed to create MSAA conformance shader layout\n");
    goto cleanup;
  }

  attribute.shaderLocation        = 0u;
  attribute.format                = GPU_VERTEX_FORMAT_FLOAT32X2;
  vertexLayout.strideBytes        = 8u;
  vertexLayout.stepMode           = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount     = 1u;
  vertexLayout.pAttributes        = &attribute;
  sourceColor.format              = GPU_FORMAT_BGRA8_UNORM;
  sourceColor.blend.writeMask     = GPU_COLOR_WRITE_ALL;
  compareColors[0].format         = GPU_FORMAT_BGRA8_UNORM;
  compareColors[0].blend.writeMask = GPU_COLOR_WRITE_ALL;
  compareColors[1]                = compareColors[0];

  sourceInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  sourceInfo.chain.structSize        = sizeof(sourceInfo);
  sourceInfo.label                   = "api-msaa-source";
  sourceInfo.layout                  = shaderLayout->pipelineLayout;
  sourceInfo.library                 = library;
  sourceInfo.vertexEntry             = "msaa_source_vs";
  sourceInfo.fragmentEntry           = "msaa_alpha_fs";
  sourceInfo.vertex.bufferLayoutCount = 1u;
  sourceInfo.vertex.pBufferLayouts   = &vertexLayout;
  sourceInfo.colorTargetCount        = 1u;
  sourceInfo.pColorTargets           = &sourceColor;
  sourceInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  sourceInfo.cullMode                = GPU_CULL_MODE_NONE;
  sourceInfo.frontFace               = GPU_FRONT_FACE_CCW;
  sourceInfo.multisample.sampleCount = MSAA_SAMPLE_COUNT;
  sourceInfo.multisample.sampleMask  = UINT32_MAX;
  sourceInfo.multisample.alphaToCoverageEnable = true;
  if (GPUCreateRenderPipeline(device,
                              &sourceInfo,
                              &sourcePipeline) != GPU_OK ||
      !sourcePipeline) {
    fprintf(stderr, "failed to create MSAA source pipeline\n");
    goto cleanup;
  }

  compareInfo                         = sourceInfo;
  compareInfo.label                   = "api-msaa-compare";
  compareInfo.vertexEntry             = "msaa_preview_vs";
  compareInfo.fragmentEntry           = "msaa_compare_fs";
  compareInfo.colorTargetCount        = 2u;
  compareInfo.pColorTargets           = compareColors;
  compareInfo.multisample.sampleCount = 1u;
  compareInfo.multisample.sampleMask  = UINT32_MAX;
  compareInfo.multisample.alphaToCoverageEnable = false;
  if (GPUCreateRenderPipeline(device,
                              &compareInfo,
                              &comparePipeline) != GPU_OK ||
      !comparePipeline) {
    fprintf(stderr, "failed to create MSAA compare pipeline\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-msaa-vertex";
  bufferInfo.sizeBytes        = sizeof(kFullscreenTriangle);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &vertexBuffer) != GPU_OK ||
      !vertexBuffer ||
      GPUQueueWriteBuffer(queue,
                          vertexBuffer,
                          0u,
                          kFullscreenTriangle,
                          sizeof(kFullscreenTriangle)) != GPU_OK) {
    fprintf(stderr, "failed to create MSAA vertex buffer\n");
    goto cleanup;
  }

  bufferInfo.label     = "api-msaa-readback";
  bufferInfo.sizeBytes = readbackBytes;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST |
                         GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "failed to create MSAA readback buffer\n");
    goto cleanup;
  }

  if (!create_msaa_texture(device,
                           "api-msaa-source",
                           MSAA_SAMPLE_COUNT,
                           GPU_TEXTURE_USAGE_COLOR_TARGET |
                             GPU_TEXTURE_USAGE_SAMPLED,
                           &sourceTexture,
                           &sourceView) ||
      !create_msaa_texture(device,
                           "api-msaa-resolve",
                           1u,
                           GPU_TEXTURE_USAGE_COLOR_TARGET |
                             GPU_TEXTURE_USAGE_SAMPLED,
                           &resolveTexture,
                           &resolveView) ||
      !create_msaa_texture(device,
                           "api-msaa-manual-result",
                           1u,
                           GPU_TEXTURE_USAGE_COLOR_TARGET |
                             GPU_TEXTURE_USAGE_COPY_SRC,
                           &manualTexture,
                           &manualView) ||
      !create_msaa_texture(device,
                           "api-msaa-resolve-result",
                           1u,
                           GPU_TEXTURE_USAGE_COLOR_TARGET |
                             GPU_TEXTURE_USAGE_COPY_SRC,
                           &resolvedTexture,
                           &resolvedView)) {
    fprintf(stderr, "failed to create MSAA conformance textures\n");
    goto cleanup;
  }

  groupEntries[0].textureView = sourceView;
  groupEntries[0].binding     = 0u;
  groupEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntries[1].textureView = resolveView;
  groupEntries[1].binding     = 1u;
  groupEntries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-msaa-compare";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = groupEntries;
  groupInfo.entryCount       = 2u;
  if (GPUCreateBindGroup(device, &groupInfo, &compareGroup) != GPU_OK ||
      !compareGroup) {
    fprintf(stderr, "failed to create MSAA compare bind group\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "api-msaa-conformance", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire MSAA command buffer\n");
    goto cleanup;
  }

  sourceColorPass.view                  = sourceView;
  sourceColorPass.resolveView           = resolveView;
  sourceColorPass.loadOp                = GPU_LOAD_OP_CLEAR;
  sourceColorPass.storeOp               = GPU_STORE_OP_STORE;
  sourceColorPass.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "api-msaa-source";
  passInfo.pColorAttachments    = &sourceColorPass;
  passInfo.colorAttachmentCount = 1u;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "failed to begin MSAA source pass\n");
    goto cleanup;
  }
  vertexBinding.buffer = vertexBuffer;
  GPUBindRenderPipeline(renderPass, sourcePipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarriers[0].texture    = sourceTexture;
  textureBarriers[0].srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarriers[0].dstAccess  = GPU_ACCESS_SHADER_READ;
  textureBarriers[0].mipCount   = 1u;
  textureBarriers[0].layerCount = 1u;
  textureBarriers[1]             = textureBarriers[0];
  textureBarriers[1].texture     = resolveTexture;
  barrierBatch.pTextureBarriers    = textureBarriers;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.textureBarrierCount = 2u;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  compareColorPass[0].view                  = manualView;
  compareColorPass[0].loadOp                = GPU_LOAD_OP_CLEAR;
  compareColorPass[0].storeOp               = GPU_STORE_OP_STORE;
  compareColorPass[0].clearColor.float32[3] = 1.0f;
  compareColorPass[1]                        = compareColorPass[0];
  compareColorPass[1].view                   = resolvedView;
  passInfo.label                = "api-msaa-compare";
  passInfo.pColorAttachments    = compareColorPass;
  passInfo.colorAttachmentCount = 2u;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "failed to begin MSAA compare pass\n");
    goto cleanup;
  }
  GPUBindRenderPipeline(renderPass, comparePipeline);
  GPUBindRenderGroup(renderPass, 0u, compareGroup, 0u, NULL);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarriers[0].texture   = manualTexture;
  textureBarriers[0].srcAccess = GPU_ACCESS_COLOR_WRITE;
  textureBarriers[0].dstAccess = GPU_ACCESS_TRANSFER_READ;
  textureBarriers[1]            = textureBarriers[0];
  textureBarriers[1].texture    = resolvedTexture;
  barrierBatch.dstStages        = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "api-msaa-readback");
  if (!copyPass) {
    fprintf(stderr, "failed to begin MSAA readback pass\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow       = MSAA_ROW_PITCH;
  copyRegion.rowsPerImage      = MSAA_HEIGHT;
  copyRegion.texture.width     = MSAA_WIDTH;
  copyRegion.texture.height    = MSAA_HEIGHT;
  copyRegion.texture.depth     = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass,
                         manualTexture,
                         readbackBuffer,
                         &copyRegion);
  copyRegion.bufferOffset = imageBytes;
  GPUCopyTextureToBuffer(copyPass,
                         resolvedTexture,
                         readbackBuffer,
                         &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create MSAA fence\n");
    goto cleanup;
  }
  buffers[0] = cmdb;
  submitInfo.chain.sType         = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize    = sizeof(submitInfo);
  submitInfo.ppCommandBuffers    = buffers;
  submitInfo.commandBufferCount  = 1u;
  submitInfo.fence               = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "MSAA conformance submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         readbackBytes) != GPU_OK ||
      !msaa_half_red(pixels, centerOffset) ||
      !msaa_half_red(pixels, (size_t)imageBytes + centerOffset) ||
      !msaa_pixels_match(pixels,
                         centerOffset,
                         (size_t)imageBytes + centerOffset)) {
    fprintf(stderr,
            "MSAA store/resolve/sample mismatch: %u %u %u %u vs "
            "%u %u %u %u\n",
            (unsigned)pixels[centerOffset + 0u],
            (unsigned)pixels[centerOffset + 1u],
            (unsigned)pixels[centerOffset + 2u],
            (unsigned)pixels[centerOffset + 3u],
            (unsigned)pixels[imageBytes + centerOffset + 0u],
            (unsigned)pixels[imageBytes + centerOffset + 1u],
            (unsigned)pixels[imageBytes + centerOffset + 2u],
            (unsigned)pixels[imageBytes + centerOffset + 3u]);
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
  if (cmdb) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(compareGroup);
  GPUDestroyTextureView(resolvedView);
  GPUDestroyTexture(resolvedTexture);
  GPUDestroyTextureView(manualView);
  GPUDestroyTexture(manualTexture);
  GPUDestroyTextureView(resolveView);
  GPUDestroyTexture(resolveTexture);
  GPUDestroyTextureView(sourceView);
  GPUDestroyTexture(sourceTexture);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyRenderPipeline(comparePipeline);
  GPUDestroyRenderPipeline(sourcePipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}
