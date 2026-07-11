#include <gpu/gpu.h>

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
  GPUInstanceCreateInfo          instanceInfo = {0};
  GPUColorTargetState            colorTargets[2] = {{0}};
  GPUDepthStencilState           depthStencil = {0};
  GPURenderPipelineCreateInfo    pipelineInfo = {0};
  GPUInstance                   *instance;
  GPUAdapter                    *adapter;
  GPUDevice                     *device;
  GPUShaderLibrary              *library;
  GPUShaderLayout               *layout;
  GPURenderPipeline             *pipeline;
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       adapterCount;
  int                            ok;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-dx12-depth-test artifact.us\n");
    return 1;
  }

  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  if (!artifact) {
    return 1;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    free(artifact);
    return 1;
  }

  adapter      = NULL;
  adapterCount = 1u;
  if (GPUEnumerateAdapters(instance, &adapterCount, &adapter) != GPU_OK ||
      !adapter || !(device = GPUCreateDeviceWithDefaultQueues(adapter))) {
    GPUDestroyInstance(instance);
    free(artifact);
    return 1;
  }

  library  = NULL;
  layout   = NULL;
  pipeline = NULL;
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
  depthStencil.depthTestEnable     = true;
  depthStencil.depthWriteEnable    = true;
  depthStencil.depthCompare        = GPU_COMPARE_LESS;
  depthStencil.stencilTestEnable   = true;
  depthStencil.front.compare       = GPU_COMPARE_ALWAYS;
  depthStencil.front.failOp        = GPU_STENCIL_OP_REPLACE;
  depthStencil.front.depthFailOp   = GPU_STENCIL_OP_INCREMENT_CLAMP;
  depthStencil.front.passOp        = GPU_STENCIL_OP_KEEP;
  depthStencil.back.compare        = GPU_COMPARE_ALWAYS;
  depthStencil.back.failOp         = GPU_STENCIL_OP_ZERO;
  depthStencil.back.depthFailOp    = GPU_STENCIL_OP_DECREMENT_WRAP;
  depthStencil.back.passOp         = GPU_STENCIL_OP_INVERT;
  depthStencil.stencilReadMask     = UINT8_MAX;
  depthStencil.stencilWriteMask    = UINT8_MAX;
  pipelineInfo.chain.sType         =
    GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize    = sizeof(pipelineInfo);
  pipelineInfo.vertexEntry         = "tri_vs";
  pipelineInfo.fragmentEntry       = "tri_fs";
  pipelineInfo.colorTargetCount    = 2u;
  pipelineInfo.pColorTargets       = colorTargets;
  pipelineInfo.depthStencilFormat  = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  pipelineInfo.pDepthStencilState  = &depthStencil;
  pipelineInfo.primitiveTopology   = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode            = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace           = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount           = 4u;
  pipelineInfo.multisample.sampleMask            = UINT32_MAX;
  pipelineInfo.multisample.alphaToCoverageEnable = true;

  ok = GPUCreateShaderLibraryFromUSL(device,
                                     artifact,
                                     artifactSize,
                                     &library) == GPU_OK && library &&
       GPUCreateShaderLayout(device, library, &layout) == GPU_OK && layout;
  if (ok) {
    pipelineInfo.layout  = layout->pipelineLayout;
    pipelineInfo.library = library;
    ok = GPUCreateRenderPipeline(device,
                                 &pipelineInfo,
                                 &pipeline) == GPU_OK && pipeline;
  }

  GPUDestroyRenderPipeline(pipeline);
  GPUDestroyShaderLayout(layout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (!ok) {
    fprintf(stderr, "DX12 depth-stencil pipeline validation failed\n");
    return 1;
  }

  puts("DX12 depth-stencil pipeline validation passed");
  return 0;
}
