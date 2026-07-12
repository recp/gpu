#include <gpu/gpu.h>

#include <stdio.h>
#include <stdlib.h>

int gpu_test_copy(GPUDevice *device);
int gpu_test_vulkan_texture(GPUDevice  *device,
                            const void *artifact,
                            uint64_t    artifactSize);

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long size;

  file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

static int
test_texture_barriers(GPUDevice *device) {
  GPUCommandBuffer     *cmdb;
  GPUCommandQueue      *queue;
  GPUFence             *fence;
  GPUTexture           *texture;
  GPUTextureCreateInfo  textureInfo   = {0};
  GPUTextureBarrier     textureBarrier = {0};
  GPUBarrierBatch       barrierBatch   = {0};
  GPUQueueSubmitInfo    submitInfo     = {0};
  int                   ok;

  cmdb    = NULL;
  queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  fence   = NULL;
  texture = NULL;
  if (!queue) {
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 2u;
  textureInfo.mipLevelCount    = 2u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK ||
      !texture ||
      GPUAcquireCommandBuffer(queue,
                              "vulkan-texture-barriers",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUDestroyTexture(texture);
    return 0;
  }

  textureBarrier.texture    = texture;
  textureBarrier.srcAccess  = GPU_ACCESS_NONE;
  textureBarrier.dstAccess  = GPU_ACCESS_TRANSFER_WRITE;
  textureBarrier.baseMip    = 1u;
  textureBarrier.mipCount   = 1u;
  textureBarrier.baseLayer  = 1u;
  textureBarrier.layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_TOP;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 1u;
  barrierBatch.pTextureBarriers    = &textureBarrier;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  textureBarrier.srcAccess = GPU_ACCESS_TRANSFER_WRITE;
  textureBarrier.dstAccess = GPU_ACCESS_SHADER_READ;
  barrierBatch.srcStages    = GPU_STAGE_TRANSFER;
  barrierBatch.dstStages    = GPU_STAGE_FRAGMENT;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    GPUDestroyTexture(texture);
    return 0;
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  ok = GPUQueueSubmit(queue, &submitInfo) == GPU_OK &&
       GPUWaitFence(fence, UINT64_MAX) == GPU_OK;

  GPUDestroyFence(fence);
  GPUDestroyTexture(texture);
  return ok;
}

static int
test_depth_pipeline(GPUDevice *device, GPUShaderLibrary *library) {
  GPUShaderLayout                *layout;
  GPURenderPipeline              *pipeline;
  GPUVertexAttribute              attribute = {0};
  GPUVertexBufferLayout           vertexLayout = {0};
  GPUColorTargetState             colorTargets[2] = {{0}};
  GPUDepthStencilState            depthStencil = {0};
  GPURenderPipelineCreateInfo     pipelineInfo = {0};
  int                             ok;

  layout   = NULL;
  pipeline = NULL;
  if (GPUCreateShaderLayout(device, library, &layout) != GPU_OK || !layout) {
    return 0;
  }

  attribute.shaderLocation           = 0u;
  attribute.format                   = GPUFloat2;
  vertexLayout.strideBytes           = 8u;
  vertexLayout.stepMode              = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount        = 1u;
  vertexLayout.pAttributes           = &attribute;
  colorTargets[0].format                 = GPU_FORMAT_BGRA8_UNORM;
  colorTargets[0].blend.enabled          = true;
  colorTargets[0].blend.color.srcFactor  = GPU_BLEND_FACTOR_SRC_ALPHA;
  colorTargets[0].blend.color.dstFactor  =
    GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorTargets[0].blend.color.op          = GPU_BLEND_OP_ADD;
  colorTargets[0].blend.alpha.srcFactor  = GPU_BLEND_FACTOR_ONE;
  colorTargets[0].blend.alpha.dstFactor  = GPU_BLEND_FACTOR_ZERO;
  colorTargets[0].blend.alpha.op          = GPU_BLEND_OP_ADD;
  colorTargets[0].blend.writeMask         = GPU_COLOR_WRITE_ALL;
  colorTargets[1].format                  = GPU_FORMAT_BGRA8_UNORM;
  colorTargets[1].blend.writeMask         = GPU_COLOR_WRITE_G;
  depthStencil.depthTestEnable       = true;
  depthStencil.depthWriteEnable      = true;
  depthStencil.depthCompare          = GPU_COMPARE_LESS;
  depthStencil.stencilTestEnable     = true;
  depthStencil.front.compare         = GPU_COMPARE_ALWAYS;
  depthStencil.front.failOp          = GPU_STENCIL_OP_REPLACE;
  depthStencil.front.depthFailOp     = GPU_STENCIL_OP_INCREMENT_CLAMP;
  depthStencil.front.passOp          = GPU_STENCIL_OP_KEEP;
  depthStencil.back.compare          = GPU_COMPARE_ALWAYS;
  depthStencil.back.failOp           = GPU_STENCIL_OP_ZERO;
  depthStencil.back.depthFailOp      = GPU_STENCIL_OP_DECREMENT_WRAP;
  depthStencil.back.passOp           = GPU_STENCIL_OP_INVERT;
  depthStencil.stencilReadMask       = UINT8_MAX;
  depthStencil.stencilWriteMask      = UINT8_MAX;
  pipelineInfo.chain.sType           =
    GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize      = sizeof(pipelineInfo);
  pipelineInfo.layout                = layout->pipelineLayout;
  pipelineInfo.library               = library;
  pipelineInfo.vertexEntry           = "tri_vs";
  pipelineInfo.fragmentEntry         = "tri_fs";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts = &vertexLayout;
  pipelineInfo.colorTargetCount      = 2u;
  pipelineInfo.pColorTargets         = colorTargets;
  pipelineInfo.depthStencilFormat    = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  pipelineInfo.pDepthStencilState    = &depthStencil;
  pipelineInfo.primitiveTopology     = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode              = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace             = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount           = 4u;
  pipelineInfo.multisample.sampleMask            = UINT32_MAX;
  pipelineInfo.multisample.alphaToCoverageEnable = true;
  ok = GPUCreateRenderPipeline(device,
                               &pipelineInfo,
                               &pipeline) == GPU_OK && pipeline;

  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyShaderLayout(layout);
  return ok;
}

static int
test_mrt_render(GPUDevice  *device,
                const void *artifact,
                uint64_t    artifactSize) {
  static const float kVertices[] = {
    -1.0f, -1.0f,
     3.0f, -1.0f,
    -1.0f,  3.0f
  };
  const uint32_t width      = 4u;
  const uint32_t height     = 4u;
  const uint64_t pixelBytes = (uint64_t)width * height * 4u;
  GPUCommandQueue                 *queue;
  GPUShaderLibrary                *library        = NULL;
  GPUPipelineLayout               *pipelineLayout = NULL;
  GPURenderPipeline               *pipeline       = NULL;
  GPUBuffer                       *vertexBuffer   = NULL;
  GPUBuffer                       *readbackBuffer = NULL;
  GPUTexture                      *targets[2]     = {NULL, NULL};
  GPUTextureView                  *views[2]       = {NULL, NULL};
  GPUCommandBuffer                *cmdb           = NULL;
  GPUCommandBuffer                *buffers[1];
  GPURenderPassEncoder            *renderPass     = NULL;
  GPUCopyPassEncoder              *copyPass       = NULL;
  GPUFence                        *fence          = NULL;
  GPUColorTargetState              colorTargets[2] = {{0}};
  GPUVertexAttribute               attribute       = {0};
  GPUVertexBufferLayout            vertexLayout    = {0};
  GPUPipelineLayoutCreateInfo      layoutInfo      = {0};
  GPURenderPipelineCreateInfo      pipelineInfo    = {0};
  GPUBufferCreateInfo              bufferInfo      = {0};
  GPUTextureCreateInfo             textureInfo     = {0};
  GPUTextureViewCreateInfo         viewInfo        = {0};
  GPURenderPassColorAttachment     colors[2]       = {{0}};
  GPURenderPassCreateInfo          passInfo        = {0};
  GPUBufferBinding                 vertexBinding   = {0};
  GPUTextureBarrier                textureBarriers[2] = {{0}};
  GPUBarrierBatch                  barrierBatch    = {0};
  GPUBufferTextureCopyRegion       copyRegion      = {0};
  GPUQueueSubmitInfo               submitInfo      = {0};
  uint8_t                          pixels[2u * 4u * 4u * 4u] = {0};
  size_t                           centerOffset;
  int                              ok = 0;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue ||
      GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library) {
    fprintf(stderr, "failed to create Vulkan MRT shader library\n");
    goto cleanup;
  }

  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label            = "vulkan-usl-mrt";
  if (GPUCreatePipelineLayout(device, &layoutInfo, &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    fprintf(stderr, "failed to create Vulkan MRT pipeline layout\n");
    goto cleanup;
  }

  attribute.shaderLocation      = 0u;
  attribute.format              = GPUFloat2;
  vertexLayout.strideBytes      = 8u;
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount   = 1u;
  vertexLayout.pAttributes      = &attribute;
  colorTargets[0].format        = GPU_FORMAT_BGRA8_UNORM;
  colorTargets[0].blend.enabled = true;
  colorTargets[0].blend.color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
  colorTargets[0].blend.color.dstFactor =
    GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorTargets[0].blend.color.op         = GPU_BLEND_OP_ADD;
  colorTargets[0].blend.alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
  colorTargets[0].blend.alpha.dstFactor = GPU_BLEND_FACTOR_ZERO;
  colorTargets[0].blend.alpha.op         = GPU_BLEND_OP_ADD;
  colorTargets[0].blend.writeMask        = GPU_COLOR_WRITE_ALL;
  colorTargets[1].format                 = GPU_FORMAT_BGRA8_UNORM;
  colorTargets[1].blend.writeMask        = GPU_COLOR_WRITE_G;

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "vulkan-usl-mrt";
  pipelineInfo.layout           = pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.vertexEntry      = "api_vs";
  pipelineInfo.fragmentEntry    = "api_mrt_fs";
  pipelineInfo.vertex.bufferLayoutCount = 1u;
  pipelineInfo.vertex.pBufferLayouts     = &vertexLayout;
  pipelineInfo.colorTargetCount          = 2u;
  pipelineInfo.pColorTargets             = colorTargets;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace         = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "failed to create Vulkan MRT pipeline\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "vulkan-usl-mrt-vertices";
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
    fprintf(stderr, "failed to create Vulkan MRT vertex buffer\n");
    goto cleanup;
  }

  bufferInfo.label     = "vulkan-usl-mrt-readback";
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "failed to create Vulkan MRT readback buffer\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "vulkan-usl-mrt-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_BGRA8_UNORM;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_COPY_SRC;
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.label               = "vulkan-usl-mrt-view";
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.format              = GPU_FORMAT_BGRA8_UNORM;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;
  for (uint32_t i = 0u; i < 2u; ++i) {
    if (GPUCreateTexture(device, &textureInfo, &targets[i]) != GPU_OK ||
        !targets[i] ||
        GPUCreateTextureView(targets[i], &viewInfo, &views[i]) != GPU_OK ||
        !views[i]) {
      fprintf(stderr, "failed to create Vulkan MRT target %u\n", i);
      goto cleanup;
    }
  }

  if (GPUAcquireCommandBuffer(queue, "vulkan-usl-mrt", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire Vulkan MRT command buffer\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < 2u; ++i) {
    colors[i].view                    = views[i];
    colors[i].loadOp                  = GPU_LOAD_OP_CLEAR;
    colors[i].storeOp                 = GPU_STORE_OP_STORE;
    colors[i].clearColor.float32[2]   = 1.0f;
    colors[i].clearColor.float32[3]   = 1.0f;
  }
  passInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize = sizeof(passInfo);
  passInfo.label            = "vulkan-usl-mrt";
  passInfo.colorAttachmentCount = 2u;
  passInfo.pColorAttachments     = colors;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "failed to begin Vulkan MRT render pass\n");
    goto cleanup;
  }

  vertexBinding.buffer = vertexBuffer;
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  for (uint32_t i = 0u; i < 2u; ++i) {
    textureBarriers[i].texture    = targets[i];
    textureBarriers[i].srcAccess  = GPU_ACCESS_COLOR_WRITE;
    textureBarriers[i].dstAccess  = GPU_ACCESS_TRANSFER_READ;
    textureBarriers[i].mipCount   = 1u;
    textureBarriers[i].layerCount = 1u;
  }
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 2u;
  barrierBatch.pTextureBarriers    = textureBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "vulkan-usl-mrt-readback");
  if (!copyPass) {
    fprintf(stderr, "failed to begin Vulkan MRT copy pass\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow       = width * 4u;
  copyRegion.rowsPerImage      = height;
  copyRegion.texture.width     = width;
  copyRegion.texture.height    = height;
  copyRegion.texture.depth     = 1u;
  copyRegion.texture.layerCount = 1u;
  for (uint32_t i = 0u; i < 2u; ++i) {
    copyRegion.bufferOffset = pixelBytes * i;
    GPUCopyTextureToBuffer(copyPass,
                           targets[i],
                           readbackBuffer,
                           &copyRegion);
  }
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create Vulkan MRT fence\n");
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
    fprintf(stderr, "Vulkan MRT submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "Vulkan MRT readback failed\n");
    goto cleanup;
  }

  centerOffset = ((size_t)2u * width + 2u) * 4u;
  if (pixels[centerOffset + 0u] < 96u ||
      pixels[centerOffset + 0u] > 160u ||
      pixels[centerOffset + 1u] > 2u ||
      pixels[centerOffset + 2u] < 96u ||
      pixels[centerOffset + 2u] > 160u ||
      pixels[centerOffset + 3u] < 96u ||
      pixels[centerOffset + 3u] > 160u ||
      pixels[pixelBytes + centerOffset + 0u] < 250u ||
      pixels[pixelBytes + centerOffset + 1u] < 250u ||
      pixels[pixelBytes + centerOffset + 2u] > 2u ||
      pixels[pixelBytes + centerOffset + 3u] < 250u) {
    fprintf(stderr, "Vulkan MRT blend/write-mask readback mismatch\n");
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
  GPUDestroyTextureView(views[1]);
  GPUDestroyTexture(targets[1]);
  GPUDestroyTextureView(views[0]);
  GPUDestroyTexture(targets[0]);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(vertexBuffer);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUShaderReflection   reflection   = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUShaderLibrary     *library;
  void                 *artifact;
  void                 *mrtArtifact;
  void                 *textureArtifact;
  uint64_t              artifactSize;
  uint64_t              mrtArtifactSize;
  uint64_t              textureArtifactSize;
  uint32_t              adapterCount;
  int                   ok;

  if (argc != 4) {
    fprintf(stderr,
            "usage: vulkan-shader artifact.us render_mrt.us textured_quad.us\n");
    return 1;
  }

  artifactSize        = 0u;
  mrtArtifactSize     = 0u;
  textureArtifactSize = 0u;
  artifact            = read_file(argv[1], &artifactSize);
  mrtArtifact         = read_file(argv[2], &mrtArtifactSize);
  textureArtifact     = read_file(argv[3], &textureArtifactSize);
  if (!artifact || !mrtArtifact || !textureArtifact) {
    fprintf(stderr, "shader artifact read failed\n");
    free(textureArtifact);
    free(mrtArtifact);
    free(artifact);
    return 1;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "vulkan instance failed\n");
    free(textureArtifact);
    free(mrtArtifact);
    free(artifact);
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
      !adapter) {
    fprintf(stderr, "vulkan adapter failed\n");
    GPUDestroyInstance(instance);
    free(textureArtifact);
    free(mrtArtifact);
    free(artifact);
    return 1;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "vulkan device failed\n");
    GPUDestroyInstance(instance);
    free(textureArtifact);
    free(mrtArtifact);
    free(artifact);
    return 1;
  }

  library = NULL;
  ok = GPUCreateShaderLibraryFromUSL(device,
                                     artifact,
                                     artifactSize,
                                     &library) == GPU_OK &&
       library &&
       GPUGetShaderReflection(library, &reflection) == GPU_OK &&
       reflection.resourceCount == 1u &&
       reflection.pResources &&
       reflection.pResources[0].groupIndex == 0u &&
       reflection.pResources[0].binding == 0u &&
       reflection.pResources[0].bindingType == GPU_BINDING_UNIFORM_BUFFER &&
       reflection.pResources[0].hasDynamicOffset &&
       test_depth_pipeline(device, library) &&
       test_texture_barriers(device) &&
       gpu_test_copy(device) &&
       test_mrt_render(device, mrtArtifact, mrtArtifactSize) &&
       gpu_test_vulkan_texture(device,
                               textureArtifact,
                               textureArtifactSize);

  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(textureArtifact);
  free(mrtArtifact);
  free(artifact);

  if (!ok) {
    fprintf(stderr, "Vulkan USL shader module validation failed\n");
    return 1;
  }

  puts("Vulkan USL shader module validation passed");
  return 0;
}
