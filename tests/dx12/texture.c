#include <gpu/gpu.h>

#include "../../src/api/device_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

int
main(int argc, char **argv) {
  static const uint8_t kTexturePixel[] = {64u, 128u, 192u, 255u};
  const uint32_t width                 = 4u;
  const uint32_t height                = 4u;
  const uint32_t rowPitch              = 256u;
  GPUInstance          *instance       = NULL;
  GPUAdapter           *adapter        = NULL;
  GPUDevice            *device         = NULL;
  GPUQueue             *queue          = NULL;
  GPUShaderLibrary     *library        = NULL;
  GPUShaderLayout      *shaderLayout   = NULL;
  GPURenderPipeline    *pipeline       = NULL;
  GPUTexture           *sampleTexture  = NULL;
  GPUTexture           *copyTexture    = NULL;
  GPUTexture           *targetTexture  = NULL;
  GPUTextureView       *sampleView     = NULL;
  GPUTextureView       *targetView     = NULL;
  GPUSampler           *sampler        = NULL;
  GPUBindGroup         *textureGroup   = NULL;
  GPUBindGroup         *samplerGroup   = NULL;
  GPUBuffer            *uploadBuffer   = NULL;
  GPUBuffer            *copiedBuffer   = NULL;
  GPUBuffer            *readbackBuffer = NULL;
  GPUCommandBuffer     *cmdb           = NULL;
  GPUCommandBuffer     *buffers[1];
  GPURenderPassEncoder *renderPass     = NULL;
  GPUCopyPassEncoder   *copyPass       = NULL;
  GPUFence             *fence          = NULL;
  void                 *artifact       = NULL;
  GPUInstanceCreateInfo       instanceInfo = {0};
  GPUColorTargetState         colorTarget = {0};
  GPURenderPipelineCreateInfo pipelineInfo = {0};
  GPUTextureCreateInfo        textureInfo = {0};
  GPUTextureViewCreateInfo    viewInfo = {0};
  GPUTextureWriteRegion       writeRegion = {0};
  GPUSamplerCreateInfo        samplerInfo = {0};
  GPUBindGroupEntry           groupEntry = {0};
  GPUBindGroupCreateInfo      groupInfo = {0};
  GPUBufferCreateInfo         bufferInfo = {0};
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPUTextureBarrier            textureBarrier = {0};
  GPUBarrierBatch              barrierBatch = {0};
  GPUBufferCopyRegion          bufferCopy = {0};
  GPUBufferTextureCopyRegion   copyRegion = {0};
  GPUTextureToTextureCopyRegion textureCopy = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  uint8_t                        pixels[256u * 4u] = {0};
  uint8_t                        copiedPixel[4] = {0};
  uint64_t                       artifactSize;
  size_t                         centerOffset;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  GPUResult                      result;
  int                            ok;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-dx12-texture-test artifact.us\n");
    return 1;
  }

  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  ok           = 0;
  if (!artifact) {
    fprintf(stderr, "DX12 texture artifact read failed\n");
    return 1;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "DX12 texture instance creation failed\n");
    goto cleanup;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter || !(device = GPUCreateDeviceWithDefaultQueues(adapter)) ||
      !(queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u))) {
    fprintf(stderr, "DX12 texture device creation failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "DX12 texture shader layout creation failed\n");
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    fprintf(stderr, "DX12 texture reflection layout mismatch\n");
    goto cleanup;
  }
  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[1],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_SAMPLER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    fprintf(stderr, "DX12 sampler reflection layout mismatch\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "dx12-usl-sampled-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &sampleTexture) != GPU_OK ||
      !sampleTexture) {
    fprintf(stderr, "DX12 sampled texture creation failed\n");
    goto cleanup;
  }

  writeRegion.width        = 1u;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = (uint32_t)sizeof(kTexturePixel);
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(queue,
                           sampleTexture,
                           &writeRegion,
                           kTexturePixel,
                           sizeof(kTexturePixel)) != GPU_OK) {
    fprintf(stderr, "DX12 sampled texture upload failed\n");
    goto cleanup;
  }

  textureInfo.label = "dx12-usl-copy-texture";
  textureInfo.usage = GPU_TEXTURE_USAGE_COPY_SRC |
                      GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &copyTexture) != GPU_OK ||
      !copyTexture) {
    fprintf(stderr, "DX12 copy texture creation failed\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "dx12-usl-sampled-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(sampleTexture, &viewInfo, &sampleView) != GPU_OK ||
      !sampleView) {
    fprintf(stderr, "DX12 sampled texture view creation failed\n");
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "dx12-usl-texture-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(device, &samplerInfo, false, &sampler) != GPU_OK ||
      !sampler) {
    fprintf(stderr, "DX12 sampler creation failed\n");
    goto cleanup;
  }

  groupEntry.binding     = 0u;
  groupEntry.bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntry.textureView = sampleView;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "dx12-usl-texture-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &textureGroup) != GPU_OK ||
      !textureGroup) {
    fprintf(stderr, "DX12 texture bind group creation failed\n");
    goto cleanup;
  }

  groupEntry.bindingType = GPU_BINDING_SAMPLER;
  groupEntry.textureView = NULL;
  groupEntry.sampler     = sampler;
  groupInfo.label        = "dx12-usl-sampler-group";
  groupInfo.layout       = shaderLayout->bindGroupLayouts[1];
  if (GPUCreateBindGroup(device, &groupInfo, &samplerGroup) != GPU_OK ||
      !samplerGroup) {
    fprintf(stderr, "DX12 sampler bind group creation failed\n");
    goto cleanup;
  }

  colorTarget.format          = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "dx12-usl-texture-pipeline";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.vertexEntry      = "quad_vs";
  pipelineInfo.fragmentEntry    = "quad_fs";
  pipelineInfo.colorTargetCount = 1u;
  pipelineInfo.pColorTargets    = &colorTarget;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace         = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "DX12 texture pipeline creation failed\n");
    goto cleanup;
  }

  textureInfo.label  = "dx12-usl-texture-target";
  textureInfo.width  = width;
  textureInfo.height = height;
  textureInfo.usage  = GPU_TEXTURE_USAGE_COLOR_TARGET |
                       GPU_TEXTURE_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &targetTexture) != GPU_OK ||
      !targetTexture) {
    fprintf(stderr, "DX12 target texture creation failed\n");
    goto cleanup;
  }
  viewInfo.label = "dx12-usl-texture-target-view";
  if (GPUCreateTextureView(targetTexture, &viewInfo, &targetView) != GPU_OK ||
      !targetView) {
    fprintf(stderr, "DX12 target texture view creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "dx12-usl-texture-readback";
  bufferInfo.sizeBytes        = sizeof(pixels);
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "DX12 texture readback buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "dx12-usl-copy-upload";
  bufferInfo.sizeBytes = rowPitch;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &uploadBuffer) != GPU_OK ||
      !uploadBuffer ||
      GPUQueueWriteBuffer(queue,
                          uploadBuffer,
                          0u,
                          kTexturePixel,
                          sizeof(kTexturePixel)) != GPU_OK) {
    fprintf(stderr, "DX12 copy upload buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "dx12-usl-copied-buffer";
  bufferInfo.sizeBytes = sizeof(copiedPixel);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &copiedBuffer) != GPU_OK ||
      !copiedBuffer) {
    fprintf(stderr, "DX12 copied buffer creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "dx12-usl-texture", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "DX12 texture command buffer acquisition failed\n");
    goto cleanup;
  }

  copyPass = GPUBeginCopyPass(cmdb, "dx12-usl-copy-chain");
  if (!copyPass) {
    fprintf(stderr, "DX12 initial copy pass creation failed\n");
    goto cleanup;
  }
  bufferCopy.sizeBytes          = sizeof(copiedPixel);
  copyRegion.bytesPerRow        = rowPitch;
  copyRegion.rowsPerImage       = 1u;
  copyRegion.texture.width      = 1u;
  copyRegion.texture.height     = 1u;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  textureCopy.width             = 1u;
  textureCopy.height            = 1u;
  textureCopy.depth             = 1u;
  textureCopy.layerCount        = 1u;
  GPUCopyBufferToBuffer(copyPass,
                        uploadBuffer,
                        copiedBuffer,
                        &bufferCopy);
  GPUCopyBufferToTexture(copyPass,
                         uploadBuffer,
                         copyTexture,
                         &copyRegion);
  GPUCopyTextureToTexture(copyPass,
                          copyTexture,
                          sampleTexture,
                          &textureCopy);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  textureBarrier.texture    = sampleTexture;
  textureBarrier.srcAccess  = GPU_ACCESS_TRANSFER_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_TRANSFER;
  barrierBatch.dstStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  color.view                  = targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "dx12-usl-texture-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "DX12 texture render pass creation failed\n");
    goto cleanup;
  }
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindRenderGroup(renderPass, 0u, textureGroup, 0u, NULL);
  GPUBindRenderGroup(renderPass, 1u, samplerGroup, 0u, NULL);
  GPUDraw(renderPass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarrier.texture    = targetTexture;
  textureBarrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "dx12-usl-texture-readback");
  if (!copyPass) {
    fprintf(stderr, "DX12 texture copy pass creation failed\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow        = rowPitch;
  copyRegion.rowsPerImage       = height;
  copyRegion.texture.width      = width;
  copyRegion.texture.height     = height;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass,
                         targetTexture,
                         readbackBuffer,
                         &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "DX12 texture fence creation failed\n");
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
    fprintf(stderr, "DX12 texture submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         copiedBuffer,
                         0u,
                         copiedPixel,
                         sizeof(copiedPixel)) != GPU_OK ||
      copiedPixel[0] != kTexturePixel[0] ||
      copiedPixel[1] != kTexturePixel[1] ||
      copiedPixel[2] != kTexturePixel[2] ||
      copiedPixel[3] != kTexturePixel[3]) {
    fprintf(stderr, "DX12 buffer copy readback failed\n");
    goto cleanup;
  }

  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "DX12 texture readback failed\n");
    goto cleanup;
  }

  centerOffset = (size_t)2u * rowPitch + 2u * 4u;
  if (pixels[centerOffset + 0u] != kTexturePixel[0] ||
      pixels[centerOffset + 1u] != kTexturePixel[1] ||
      pixels[centerOffset + 2u] != kTexturePixel[2] ||
      pixels[centerOffset + 3u] != kTexturePixel[3]) {
    fprintf(stderr,
            "DX12 texture readback mismatch: %u %u %u %u\n",
            (unsigned)pixels[centerOffset + 0u],
            (unsigned)pixels[centerOffset + 1u],
            (unsigned)pixels[centerOffset + 2u],
            (unsigned)pixels[centerOffset + 3u]);
    goto cleanup;
  }

  GPUResetStats(device);
  for (uint32_t i = 0u; i < 16u; i++) {
    if (GPUAcquireCommandBuffer(queue, "dx12-copy-warm", &cmdb) != GPU_OK ||
        !cmdb || !(copyPass = GPUBeginCopyPass(cmdb, "dx12-copy-warm"))) {
      goto cleanup;
    }
    GPUCopyBufferToBuffer(copyPass,
                          uploadBuffer,
                          copiedBuffer,
                          &bufferCopy);
    GPUEndCopyPass(copyPass);
    copyPass = NULL;

    buffers[0]                  = cmdb;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
        GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
      cmdb = NULL;
      goto cleanup;
    }
    cmdb = NULL;
  }
  if (device->currentFrameStats.hotPathAllocCount != 0u ||
      device->currentFrameStats.hotPathFreeCount != 0u) {
    fprintf(stderr, "DX12 copy hot path allocated after warm-up\n");
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
  GPUDestroyBuffer(copiedBuffer);
  GPUDestroyBuffer(uploadBuffer);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(targetTexture);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyBindGroup(samplerGroup);
  GPUDestroyBindGroup(textureGroup);
  GPUDestroySampler(sampler);
  GPUDestroyTextureView(sampleView);
  GPUDestroyTexture(copyTexture);
  GPUDestroyTexture(sampleTexture);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (!ok) {
    return 1;
  }
  puts("DX12 USL texture validation passed");
  return 0;
}
