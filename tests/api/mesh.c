#include <gpu/gpu.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  MESH_TARGET_WIDTH  = 8u,
  MESH_TARGET_HEIGHT = 8u,
  MESH_PIXEL_BYTES   = MESH_TARGET_WIDTH * MESH_TARGET_HEIGHT * 4u
};

typedef struct TaskParams {
  uint32_t meshGroups[4];
  float    offset[4];
  float    tint[4];
} TaskParams;

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) fclose(file);
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

static int
mesh_layout_matches(GPUShaderLayout *layout) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;

  if (!layout || layout->bindGroupLayoutCount != 1u ||
      !layout->bindGroupLayouts || !layout->bindGroupLayouts[0] ||
      !layout->pipelineLayout) {
    return 0;
  }

  entryCount = 0u;
  entries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0],
                                         &entryCount);
  for (uint32_t i = 0u; entries && i < entryCount; i++) {
    if (entries[i].binding == 0u &&
        entries[i].bindingType == GPU_BINDING_UNIFORM_BUFFER &&
        entries[i].visibility == GPU_SHADER_STAGE_TASK_BIT) {
      return 1;
    }
  }
  return 0;
}

static int
mesh_pixels_match(const uint8_t pixels[MESH_PIXEL_BYTES]) {
  uint32_t coloredPixels;
  uint32_t leftPixels;
  uint32_t rightPixels;

  coloredPixels = 0u;
  leftPixels    = 0u;
  rightPixels   = 0u;
  for (uint32_t i = 0u; i < MESH_TARGET_WIDTH * MESH_TARGET_HEIGHT; i++) {
    const uint8_t *pixel;
    uint32_t       rgb;
    uint32_t       x;

    pixel = &pixels[i * 4u];
    rgb   = (uint32_t)pixel[0] + pixel[1] + pixel[2];
    if (rgb > 48u && pixel[3] > 240u) {
      coloredPixels++;
      x = i % MESH_TARGET_WIDTH;
      if (x < MESH_TARGET_WIDTH / 2u) {
        leftPixels++;
      } else {
        rightPixels++;
      }
    }
  }
  return coloredPixels >= 2u && leftPixels > 0u && rightPixels == 0u;
}

static int
test_mesh_draw(GPUDevice  *device,
               const void *artifact,
               uint64_t    artifactSize) {
  const TaskParams taskParams = {
    .meshGroups = {1u, 1u, 1u, 0u},
    .offset     = {0.0f, 0.0f, 0.0f, 0.0f},
    .tint       = {1.0f, 0.75f, 0.5f, 1.0f}
  };
  GPUQueue                     *queue;
  GPUShaderLibrary             *library;
  GPUShaderLayout              *shaderLayout;
  GPUPipelineCache             *pipelineCache;
  GPURenderPipeline            *pipeline;
  GPURenderPipeline            *asyncPipeline;
  GPURenderPipeline            *cachedPipeline;
  GPUBuffer                    *taskBuffer;
  GPUBuffer                    *readbackBuffer;
  GPUBindGroup                 *taskGroup;
  GPUTexture                   *target;
  GPUTextureView               *targetView;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *renderPass;
  GPUCopyPassEncoder           *copyPass;
  GPUFence                     *fence;
  GPUMeshPipelineEXT            meshInfo      = {0};
  GPUPipelineCacheCreateInfo    cacheInfo     = {0};
  GPUPipelineCompileHandle      compileHandle = {0};
  GPUPipelineCompileStatus      compileStatus = GPU_PIPELINE_COMPILE_PENDING;
  GPUCacheStats                 cacheStats     = {0};
  GPUColorTargetState           colorTarget   = {0};
  GPURenderPipelineCreateInfo   pipelineInfo  = {0};
  GPUBufferCreateInfo           bufferInfo    = {0};
  GPUBindGroupEntry             taskEntry     = {0};
  GPUBindGroupCreateInfo        groupInfo     = {0};
  GPUTextureCreateInfo          textureInfo   = {0};
  GPUTextureViewCreateInfo      viewInfo      = {0};
  GPURenderPassColorAttachment  color         = {0};
  GPURenderPassCreateInfo       passInfo      = {0};
  GPUViewport                   viewport      = {0};
  GPUScissorRect                scissor       = {0};
  GPUTextureBarrier             textureBarrier = {0};
  GPUBarrierBatch               barrierBatch  = {0};
  GPUBufferTextureCopyRegion    copyRegion    = {0};
  GPUQueueSubmitInfo            submitInfo    = {0};
  uint8_t                       pixels[MESH_PIXEL_BYTES] = {0};
  int                           ok;

  queue          = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library        = NULL;
  shaderLayout   = NULL;
  pipelineCache  = NULL;
  pipeline       = NULL;
  asyncPipeline  = NULL;
  cachedPipeline = NULL;
  taskBuffer     = NULL;
  readbackBuffer = NULL;
  taskGroup      = NULL;
  target         = NULL;
  targetView     = NULL;
  cmdb           = NULL;
  renderPass     = NULL;
  copyPass       = NULL;
  fence          = NULL;
  ok             = 0;

  if (!queue ||
      GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !mesh_layout_matches(shaderLayout)) {
    fprintf(stderr, "failed to create mesh shader layout\n");
    goto cleanup;
  }

  meshInfo.chain.sType      = GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT;
  meshInfo.chain.structSize = sizeof(meshInfo);
  meshInfo.taskEntry        = "task_main";
  meshInfo.meshEntry        = "mesh_main";
  colorTarget.format        = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.chain.pNext      = &meshInfo.chain;
  pipelineInfo.label            = "usl-mesh";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.fragmentEntry    = "fragment_main";
  pipelineInfo.colorTargetCount = 1u;
  pipelineInfo.pColorTargets    = &colorTarget;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace         = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_LINE_LIST;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) !=
        GPU_ERROR_INVALID_ARGUMENT || pipeline) {
    fprintf(stderr, "mesh pipeline accepted mismatched output topology\n");
    goto cleanup;
  }
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "failed to create mesh pipeline\n");
    goto cleanup;
  }

  cacheInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);
  cacheInfo.label            = "usl-mesh-async";
  GPUResetStats(device);
  if (GPUCreatePipelineCache(device, &cacheInfo, &pipelineCache) != GPU_OK ||
      !pipelineCache ||
      GPUCompileRenderPipelineAsync(device,
                                    pipelineCache,
                                    &pipelineInfo,
                                    &compileHandle) != GPU_OK ||
      compileHandle.id == 0u) {
    fprintf(stderr, "failed to enqueue async mesh pipeline\n");
    goto cleanup;
  }
  for (uint32_t i = 0u; i < 1000000u; i++) {
    if (GPUPollRenderPipelineCompile(device,
                                     compileHandle,
                                     &compileStatus,
                                     &asyncPipeline) != GPU_OK) {
      fprintf(stderr, "failed to poll async mesh pipeline\n");
      goto cleanup;
    }
    if (compileStatus != GPU_PIPELINE_COMPILE_PENDING) {
      break;
    }
  }
  if (compileStatus != GPU_PIPELINE_COMPILE_READY || !asyncPipeline) {
    fprintf(stderr, "async mesh pipeline did not become ready\n");
    goto cleanup;
  }
  pipelineInfo.cache = pipelineCache;
  if (GPUCreateRenderPipeline(device, &pipelineInfo, &cachedPipeline) != GPU_OK ||
      !cachedPipeline ||
      GPUGetCacheStats(device, &cacheStats) != GPU_OK ||
      cacheStats.pipelineMisses != 1u || cacheStats.pipelineHits != 1u) {
    fprintf(stderr, "mesh pipeline cache did not produce a hit\n");
    goto cleanup;
  }
  GPUDestroyRenderPipeline(cachedPipeline);
  cachedPipeline     = NULL;
  pipelineInfo.cache = NULL;
  GPUDestroyRenderPipeline(pipeline);
  pipeline      = asyncPipeline;
  asyncPipeline = NULL;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "mesh-task-params";
  bufferInfo.sizeBytes        = sizeof(taskParams);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &taskBuffer) != GPU_OK ||
      !taskBuffer ||
      GPUQueueWriteBuffer(queue,
                          taskBuffer,
                          0u,
                          &taskParams,
                          sizeof(taskParams)) != GPU_OK) {
    fprintf(stderr, "failed to create mesh task buffer\n");
    goto cleanup;
  }

  bufferInfo.label     = "mesh-readback";
  bufferInfo.sizeBytes = sizeof(pixels);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST |
                         GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "failed to create mesh readback buffer\n");
    goto cleanup;
  }

  taskEntry.binding       = 0u;
  taskEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  taskEntry.buffer.buffer = taskBuffer;
  taskEntry.buffer.size   = sizeof(taskParams);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "mesh-task-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &taskEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &taskGroup) != GPU_OK ||
      !taskGroup) {
    fprintf(stderr, "failed to create mesh task group\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "mesh-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = MESH_TARGET_WIDTH;
  textureInfo.height           = MESH_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &target) != GPU_OK || !target) {
    fprintf(stderr, "failed to create mesh target\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "mesh-target-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(target, &viewInfo, &targetView) != GPU_OK ||
      !targetView) {
    fprintf(stderr, "failed to create mesh target view\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "usl-mesh", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "failed to acquire mesh command buffer\n");
    goto cleanup;
  }

  color.view                  = targetView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "usl-mesh";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "failed to begin mesh render pass\n");
    goto cleanup;
  }

  viewport.width    = (float)MESH_TARGET_WIDTH;
  viewport.height   = (float)MESH_TARGET_HEIGHT;
  viewport.maxDepth = 1.0f;
  scissor.width     = MESH_TARGET_WIDTH;
  scissor.height    = MESH_TARGET_HEIGHT;
  GPUBindRenderPipeline(renderPass, pipeline);
  GPUBindRenderGroup(renderPass, 0u, taskGroup, 0u, NULL);
  GPUSetViewport(renderPass, &viewport);
  GPUSetScissor(renderPass, &scissor);
  GPUDrawMeshEXT(renderPass, 1u, 1u, 1u);
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

  copyPass = GPUBeginCopyPass(cmdb, "mesh-readback");
  if (!copyPass) {
    fprintf(stderr, "failed to begin mesh copy pass\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow        = MESH_TARGET_WIDTH * 4u;
  copyRegion.rowsPerImage       = MESH_TARGET_HEIGHT;
  copyRegion.texture.width      = MESH_TARGET_WIDTH;
  copyRegion.texture.height     = MESH_TARGET_HEIGHT;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass, target, readbackBuffer, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create mesh fence\n");
    goto cleanup;
  }
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "mesh submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK ||
      !mesh_pixels_match(pixels)) {
    fprintf(stderr, "mesh readback mismatch\n");
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (copyPass) GPUEndCopyPass(copyPass);
  if (renderPass) GPUEndRenderPass(renderPass);
  GPUDestroyFence(fence);
  GPUDestroyTextureView(targetView);
  GPUDestroyTexture(target);
  GPUDestroyBindGroup(taskGroup);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(taskBuffer);
  GPUDestroyRenderPipeline(cachedPipeline);
  GPUDestroyRenderPipeline(asyncPipeline);
  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyPipelineCache(pipelineCache);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  return ok;
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUDeviceCreateInfo   deviceInfo   = {0};
  GPUFeature            requiredFeature;
  GPUBackend            backend;
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUResult             result;
  void                 *artifact;
  uint64_t              artifactSize;
  uint32_t              adapterCount;
  int                   ok;

  if (argc != 3) {
    fprintf(stderr, "usage: mesh <metal|vulkan|dx12> mesh_triangle.us\n");
    return 1;
  }

  if (strcmp(argv[1], "metal") == 0) {
    backend = GPU_BACKEND_METAL;
  } else if (strcmp(argv[1], "vulkan") == 0) {
    backend = GPU_BACKEND_VULKAN;
  } else if (strcmp(argv[1], "dx12") == 0) {
    backend = GPU_BACKEND_DX12;
  } else {
    fprintf(stderr, "mesh backend must be metal, vulkan, or dx12\n");
    return 1;
  }

  artifactSize = 0u;
  artifact     = read_file(argv[2], &artifactSize);
  if (!artifact) {
    fprintf(stderr, "mesh artifact read failed\n");
    return 1;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = backend;
  instanceInfo.enableValidation = true;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "mesh instance failed\n");
    free(artifact);
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    fprintf(stderr, "mesh adapter failed\n");
    GPUDestroyInstance(instance);
    free(artifact);
    return 1;
  }
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_MESH_SHADER)) {
    puts("mesh shader test skipped: unsupported adapter");
    GPUDestroyInstance(instance);
    free(artifact);
    return 77;
  }

  requiredFeature = GPU_FEATURE_MESH_SHADER;
  deviceInfo.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize = sizeof(deviceInfo);
  deviceInfo.label            = "mesh-test-device";
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &requiredFeature;
  device = NULL;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "mesh device failed\n");
    GPUDestroyInstance(instance);
    free(artifact);
    return 1;
  }
  if (!GPUGetProcAddr(device, "GPUDrawMeshEXT")) {
    fprintf(stderr, "mesh extension lookup failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    free(artifact);
    return 1;
  }

  ok = test_mesh_draw(device, artifact, artifactSize);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (!ok) {
    fprintf(stderr, "mesh shader validation failed\n");
    return 1;
  }

  puts("mesh shader validation passed");
  return 0;
}
