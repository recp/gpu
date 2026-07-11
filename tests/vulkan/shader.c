#include <gpu/gpu.h>

#include <stdio.h>
#include <stdlib.h>

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

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUShaderReflection   reflection   = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUShaderLibrary     *library;
  void                 *artifact;
  uint64_t              artifactSize;
  uint32_t              adapterCount;
  int                   ok;

  if (argc != 2) {
    fprintf(stderr, "usage: vulkan-shader artifact.us\n");
    return 1;
  }

  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  if (!artifact) {
    fprintf(stderr, "shader artifact read failed\n");
    return 1;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  instanceInfo.enableValidation = true;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "vulkan instance failed\n");
    free(artifact);
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
      !adapter) {
    fprintf(stderr, "vulkan adapter failed\n");
    GPUDestroyInstance(instance);
    free(artifact);
    return 1;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  if (!device) {
    fprintf(stderr, "vulkan device failed\n");
    GPUDestroyInstance(instance);
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
       test_texture_barriers(device);

  GPUFreeShaderReflection(&reflection);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (!ok) {
    fprintf(stderr, "Vulkan USL shader module validation failed\n");
    return 1;
  }

  puts("Vulkan USL shader module validation passed");
  return 0;
}
