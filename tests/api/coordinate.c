#include "test.h"

#include <stddef.h>

typedef struct CoordinateTextureVertex {
  float position[4];
  float uv[2];
} CoordinateTextureVertex;

static int
coordinate_pixel(const uint8_t *pixel,
                 uint8_t        red,
                 uint8_t        green,
                 uint8_t        blue) {
  const uint32_t tolerance = 2u;

  return abs((int)pixel[0] - red) <= (int)tolerance &&
         abs((int)pixel[1] - green) <= (int)tolerance &&
         abs((int)pixel[2] - blue) <= (int)tolerance &&
         pixel[3] >= 253u;
}

static int
coordinate_create_texture(GPUDevice          *device,
                          const char         *label,
                          GPUFormat           format,
                          GPUTextureUsageFlags usage,
                          uint32_t            width,
                          uint32_t            height,
                          GPUTexture        **outTexture,
                          GPUTextureView    **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = usage;
  if (GPUCreateTexture(device, &textureInfo, outTexture) != GPU_OK ||
      !*outTexture) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(*outTexture, &viewInfo, outView) == GPU_OK &&
         *outView;
}

static int
coordinate_create_pipeline(GPUDevice                   *device,
                           GPUShaderLibrary            *library,
                           GPUPipelineLayout           *layout,
                           const char                  *fragmentEntry,
                           const GPUVertexBufferLayout *vertexLayout,
                           const GPUDepthStencilState  *depthState,
                           GPURenderPipeline          **outPipeline) {
  GPUColorTargetState         colorTarget = {0};
  GPURenderPipelineCreateInfo info        = {0};

  colorTarget.format          = GPU_FORMAT_RGBA8_UNORM;
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  info.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = fragmentEntry;
  info.layout           = layout;
  info.library          = library;
  info.vertexEntry      = depthState ? "coordinate_position_vs"
                                     : "coordinate_texture_vs";
  info.fragmentEntry    = fragmentEntry;
  info.vertex.bufferLayoutCount = 1u;
  info.vertex.pBufferLayouts     = vertexLayout;
  info.colorTargetCount          = 1u;
  info.pColorTargets             = &colorTarget;
  info.depthStencilFormat        = depthState ? GPU_FORMAT_DEPTH32_FLOAT
                                               : GPU_FORMAT_UNDEFINED;
  info.pDepthStencilState        = depthState;
  info.primitiveTopology         = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                  = GPU_CULL_MODE_BACK;
  info.frontFace                 = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount   = 1u;
  info.multisample.sampleMask    = UINT32_MAX;
  return GPUCreateRenderPipeline(device, &info, outPipeline) == GPU_OK &&
         *outPipeline;
}

int
gpu_test_coordinate_contract(GPUDevice *device, const char *bytecodePath) {
  static const CoordinateTextureVertex kTextureVertices[] = {
    {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{-1.0f, -1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{-1.0f,  1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{ 1.0f, -1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
    {{ 1.0f,  1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}}
  };
  static const float kNearTriangle[] = {
    -1.0f, -1.0f, 0.25f, 1.0f,
     3.0f, -1.0f, 0.25f, 1.0f,
    -1.0f,  3.0f, 0.25f, 1.0f
  };
  static const float kFarTriangle[] = {
    -1.0f, -1.0f, 0.75f, 1.0f,
     3.0f, -1.0f, 0.75f, 1.0f,
    -1.0f,  3.0f, 0.75f, 1.0f
  };
  static const float kClippedTriangle[] = {
    -1.0f, -1.0f, -0.25f, 1.0f,
     3.0f, -1.0f, -0.25f, 1.0f,
    -1.0f,  3.0f, -0.25f, 1.0f
  };
  static const uint8_t kTexturePixels[] = {
    255u,   0u,   0u, 255u,   0u, 255u,   0u, 255u,
      0u,   0u, 255u, 255u, 255u, 255u, 255u, 255u
  };
  const uint32_t width      = 4u;
  const uint32_t height     = 4u;
  const uint32_t rowPitch   = 256u;
  const uint64_t imageBytes = (uint64_t)rowPitch * height;
  GPUQueue             *queue           = NULL;
  GPUShaderLibrary     *library         = NULL;
  GPUShaderLayout      *shaderLayout    = NULL;
  GPURenderPipeline    *texturePipeline = NULL;
  GPURenderPipeline    *repeatPipeline  = NULL;
  GPURenderPipeline    *redPipeline     = NULL;
  GPURenderPipeline    *greenPipeline   = NULL;
  GPURenderPipeline    *bluePipeline    = NULL;
  GPUBuffer            *textureVertices = NULL;
  GPUBuffer            *nearVertices    = NULL;
  GPUBuffer            *farVertices     = NULL;
  GPUBuffer            *clippedVertices = NULL;
  GPUBuffer            *readbackBuffer  = NULL;
  GPUTexture           *sampleTexture   = NULL;
  GPUTexture           *textureTarget   = NULL;
  GPUTexture           *repeatTarget    = NULL;
  GPUTexture           *depthTarget     = NULL;
  GPUTexture           *depthTexture    = NULL;
  GPUTextureView       *sampleView      = NULL;
  GPUTextureView       *textureView     = NULL;
  GPUTextureView       *repeatView      = NULL;
  GPUTextureView       *depthTargetView = NULL;
  GPUTextureView       *depthView       = NULL;
  GPUSampler           *sampler         = NULL;
  GPUSampler           *repeatSampler   = NULL;
  GPUBindGroup         *textureGroup     = NULL;
  GPUBindGroup         *samplerGroup     = NULL;
  GPUBindGroup         *repeatGroup      = NULL;
  GPUBindGroup         *repeatClampGroup = NULL;
  GPUCommandBuffer     *cmdb            = NULL;
  GPUCommandBuffer     *buffers[1];
  GPURenderPassEncoder *renderPass      = NULL;
  GPUTransferPassEncoder   *copyPass        = NULL;
  GPUFence             *fence           = NULL;
  GPUVertexAttribute    textureAttributes[2] = {{0}};
  GPUVertexAttribute    positionAttribute    = {0};
  GPUVertexBufferLayout textureLayout        = {0};
  GPUVertexBufferLayout positionLayout       = {0};
  GPUDepthStencilState  depthState           = {0};
  GPUBufferCreateInfo   bufferInfo           = {0};
  GPUTextureWriteRegion writeRegion          = {0};
  GPUSamplerCreateInfo  samplerInfo          = {0};
  GPUBindGroupEntry     groupEntry           = {0};
  GPUBindGroupCreateInfo groupInfo           = {0};
  GPURenderPassColorAttachment color         = {0};
  GPURenderPassDepthStencilAttachment depth  = {0};
  GPURenderPassCreateInfo passInfo            = {0};
  GPUBufferBinding       vertexBinding        = {0};
  GPUViewport            viewport             = {0};
  GPUScissorRect         scissor              = {0};
  GPUTextureBarrier      textureBarriers[4]   = {{0}};
  GPUBarrierBatch        barrierBatch         = {0};
  GPUBufferTextureCopyRegion copyRegion       = {0};
  GPUQueueSubmitInfo     submitInfo           = {0};
  GPUCacheStats          cacheStats           = {0};
  uint8_t                pixels[4u * 256u * 4u] = {0};
  uint64_t               bytecodeSize = 0u;
  void                  *bytecode;
  size_t                 topRight;
  size_t                 bottomLeft;
  size_t                 bottomRight;
  size_t                 repeatOffset;
  size_t                 depthCenter;
  size_t                 sampleOffset;
  int                    ok = 0;

  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  queue    = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!bytecode || !queue ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 3u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->bindGroupLayouts[2]) {
    fprintf(stderr, "coordinate shader setup failed\n");
    goto cleanup;
  }

  textureAttributes[0].shaderLocation = 0u;
  textureAttributes[0].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
  textureAttributes[0].offset         = offsetof(CoordinateTextureVertex,
                                                  position);
  textureAttributes[1].shaderLocation = 1u;
  textureAttributes[1].format         = GPU_VERTEX_FORMAT_FLOAT32X2;
  textureAttributes[1].offset         = offsetof(CoordinateTextureVertex, uv);
  textureLayout.strideBytes           = sizeof(CoordinateTextureVertex);
  textureLayout.stepMode              = GPU_VERTEX_STEP_MODE_VERTEX;
  textureLayout.attributeCount        = 2u;
  textureLayout.pAttributes           = textureAttributes;
  positionAttribute.shaderLocation    = 0u;
  positionAttribute.format            = GPU_VERTEX_FORMAT_FLOAT32X4;
  positionLayout.strideBytes          = 4u * sizeof(float);
  positionLayout.stepMode             = GPU_VERTEX_STEP_MODE_VERTEX;
  positionLayout.attributeCount       = 1u;
  positionLayout.pAttributes          = &positionAttribute;
  depthState.depthTestEnable          = true;
  depthState.depthWriteEnable         = true;
  depthState.depthCompare             = GPU_COMPARE_LESS;
  if (!coordinate_create_pipeline(device,
                                  library,
                                  shaderLayout->pipelineLayout,
                                  "coordinate_texture_fs",
                                  &textureLayout,
                                  NULL,
                                  &texturePipeline) ||
      !coordinate_create_pipeline(device,
                                  library,
                                  shaderLayout->pipelineLayout,
                                  "coordinate_texture_repeat_fs",
                                  &textureLayout,
                                  NULL,
                                  &repeatPipeline) ||
      !coordinate_create_pipeline(device,
                                  library,
                                  shaderLayout->pipelineLayout,
                                  "coordinate_red_fs",
                                  &positionLayout,
                                  &depthState,
                                  &redPipeline) ||
      !coordinate_create_pipeline(device,
                                  library,
                                  shaderLayout->pipelineLayout,
                                  "coordinate_green_fs",
                                  &positionLayout,
                                  &depthState,
                                  &greenPipeline) ||
      !coordinate_create_pipeline(device,
                                  library,
                                  shaderLayout->pipelineLayout,
                                  "coordinate_blue_fs",
                                  &positionLayout,
                                  &depthState,
                                  &bluePipeline)) {
    fprintf(stderr, "coordinate pipeline setup failed\n");
    goto cleanup;
  }

#define CREATE_BUFFER(target, name, data, usageBits) do {                  \
    bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;   \
    bufferInfo.chain.structSize = sizeof(bufferInfo);                      \
    bufferInfo.label            = name;                                    \
    bufferInfo.sizeBytes        = sizeof(data);                            \
    bufferInfo.usage            = (usageBits) | GPU_BUFFER_USAGE_COPY_DST; \
    if (GPUCreateBuffer(device, &bufferInfo, &(target)) != GPU_OK ||        \
        !(target) ||                                                       \
        GPUQueueWriteBuffer(queue,                                         \
                            (target),                                      \
                            0u,                                            \
                            (data),                                        \
                            sizeof(data)) != GPU_OK) {                      \
      fprintf(stderr, "coordinate buffer setup failed: %s\n", name);     \
      goto cleanup;                                                        \
    }                                                                      \
  } while (0)
  CREATE_BUFFER(textureVertices,
                "coordinate-texture-vertices",
                kTextureVertices,
                GPU_BUFFER_USAGE_VERTEX);
  CREATE_BUFFER(nearVertices,
                "coordinate-near-vertices",
                kNearTriangle,
                GPU_BUFFER_USAGE_VERTEX);
  CREATE_BUFFER(farVertices,
                "coordinate-far-vertices",
                kFarTriangle,
                GPU_BUFFER_USAGE_VERTEX);
  CREATE_BUFFER(clippedVertices,
                "coordinate-clipped-vertices",
                kClippedTriangle,
                GPU_BUFFER_USAGE_VERTEX);
#undef CREATE_BUFFER

  bufferInfo.label     = "coordinate-readback";
  bufferInfo.sizeBytes = 4u * imageBytes;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readbackBuffer) != GPU_OK ||
      !readbackBuffer) {
    fprintf(stderr, "coordinate readback setup failed\n");
    goto cleanup;
  }

  if (!coordinate_create_texture(device,
                                 "coordinate-sample",
                                 GPU_FORMAT_RGBA8_UNORM,
                                 GPU_TEXTURE_USAGE_SAMPLED |
                                   GPU_TEXTURE_USAGE_COPY_SRC |
                                   GPU_TEXTURE_USAGE_COPY_DST,
                                 2u,
                                 2u,
                                 &sampleTexture,
                                 &sampleView) ||
      !coordinate_create_texture(device,
                                 "coordinate-texture-target",
                                 GPU_FORMAT_RGBA8_UNORM,
                                 GPU_TEXTURE_USAGE_COLOR_TARGET |
                                   GPU_TEXTURE_USAGE_COPY_SRC,
                                 width,
                                 height,
                                 &textureTarget,
                                 &textureView) ||
      !coordinate_create_texture(device,
                                 "coordinate-repeat-target",
                                 GPU_FORMAT_RGBA8_UNORM,
                                 GPU_TEXTURE_USAGE_COLOR_TARGET |
                                   GPU_TEXTURE_USAGE_COPY_SRC,
                                 width,
                                 height,
                                 &repeatTarget,
                                 &repeatView) ||
      !coordinate_create_texture(device,
                                 "coordinate-depth-target",
                                 GPU_FORMAT_RGBA8_UNORM,
                                 GPU_TEXTURE_USAGE_COLOR_TARGET |
                                   GPU_TEXTURE_USAGE_COPY_SRC,
                                 width,
                                 height,
                                 &depthTarget,
                                 &depthTargetView) ||
      !coordinate_create_texture(device,
                                 "coordinate-depth",
                                 GPU_FORMAT_DEPTH32_FLOAT,
                                 GPU_TEXTURE_USAGE_DEPTH_STENCIL,
                                 width,
                                 height,
                                 &depthTexture,
                                 &depthView)) {
    fprintf(stderr, "coordinate texture setup failed\n");
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
                           kTexturePixels,
                           sizeof(kTexturePixels)) != GPU_OK) {
    fprintf(stderr, "coordinate texture upload failed\n");
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "coordinate-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(device, &samplerInfo, false, &sampler) != GPU_OK ||
      !sampler) {
    fprintf(stderr, "coordinate sampler setup failed\n");
    goto cleanup;
  }
  samplerInfo.label         = "coordinate-repeat-sampler";
  samplerInfo.desc.addressU = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressV = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressW = GPU_ADDRESS_MODE_REPEAT;
  if (GPUCreateSampler(device,
                       &samplerInfo,
                       false,
                       &repeatSampler) != GPU_OK ||
      !repeatSampler) {
    fprintf(stderr, "coordinate repeat sampler setup failed\n");
    goto cleanup;
  }

  groupEntry.binding     = 0u;
  groupEntry.bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupEntry.textureView = sampleView;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "coordinate-texture-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &groupEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &textureGroup) != GPU_OK ||
      !textureGroup) {
    fprintf(stderr, "coordinate texture group setup failed\n");
    goto cleanup;
  }
  groupEntry.bindingType = GPU_BINDING_SAMPLER;
  groupEntry.textureView = NULL;
  groupEntry.sampler     = sampler;
  groupInfo.label        = "coordinate-sampler-group";
  groupInfo.layout       = shaderLayout->bindGroupLayouts[1];
  GPUResetStats(device);
  if (GPUCreateBindGroup(device, &groupInfo, &samplerGroup) != GPU_OK ||
      !samplerGroup) {
    fprintf(stderr, "coordinate sampler group setup failed\n");
    goto cleanup;
  }
  if (GPUGetCacheStats(device, &cacheStats) != GPU_OK ||
      cacheStats.bindGroupHits != 0u ||
      cacheStats.bindGroupMisses != 1u) {
    fprintf(stderr,
            "coordinate sampler group cache mismatch: hits=%llu misses=%llu\n",
            (unsigned long long)cacheStats.bindGroupHits,
            (unsigned long long)cacheStats.bindGroupMisses);
    goto cleanup;
  }
  groupEntry.sampler = repeatSampler;
  groupInfo.label    = "coordinate-repeat-sampler-group";
  groupInfo.layout   = shaderLayout->bindGroupLayouts[2];
  if (GPUCreateBindGroup(device, &groupInfo, &repeatGroup) != GPU_OK ||
      !repeatGroup) {
    fprintf(stderr, "coordinate repeat sampler group setup failed\n");
    goto cleanup;
  }
  groupEntry.sampler = sampler;
  groupInfo.label    = "coordinate-repeat-clamp-group";
  if (GPUCreateBindGroup(device, &groupInfo, &repeatClampGroup) != GPU_OK ||
      !repeatClampGroup) {
    fprintf(stderr, "coordinate repeat clamp group setup failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "coordinate-contract", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "coordinate command buffer setup failed\n");
    goto cleanup;
  }
  viewport.width    = (float)width;
  viewport.height   = (float)height;
  viewport.maxDepth = 1.0f;
  scissor.x      = -2;
  scissor.y      = -3;
  scissor.width  = width + 2u;
  scissor.height = height + 3u;

  color.view                  = textureView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "coordinate-texture-pass";
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "coordinate texture pass failed\n");
    goto cleanup;
  }
  vertexBinding.buffer = textureVertices;
  GPUBindRenderPipeline(renderPass, texturePipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUBindRenderGroup(renderPass, 0u, textureGroup, 0u, NULL);
  GPUBindRenderGroup(renderPass, 1u, samplerGroup, 0u, NULL);
  GPUSetViewport(renderPass, &viewport);
  GPUSetScissor(renderPass, &scissor);
  GPUDraw(renderPass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  color.view       = repeatView;
  passInfo.label   = "coordinate-repeat-pass";
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "coordinate repeat pass failed\n");
    goto cleanup;
  }
  GPUBindRenderPipeline(renderPass, repeatPipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUBindRenderGroup(renderPass, 0u, textureGroup, 0u, NULL);
  GPUBindRenderGroup(renderPass, 1u, samplerGroup, 0u, NULL);
  GPUBindRenderGroup(renderPass, 2u, repeatGroup, 0u, NULL);
  GPUSetViewport(renderPass, &viewport);
  GPUSetScissor(renderPass, &scissor);
  GPUDraw(renderPass, 6u, 1u, 0u, 0u);
  scissor.x      = (int32_t)(width / 2u);
  scissor.y      = 0;
  scissor.width  = width - width / 2u;
  scissor.height = height;
  GPUBindRenderGroup(renderPass, 2u, repeatClampGroup, 0u, NULL);
  GPUSetScissor(renderPass, &scissor);
  GPUDraw(renderPass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  color.view                       = depthTargetView;
  depth.view                       = depthView;
  depth.depthLoadOp                = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp               = GPU_STORE_OP_STORE;
  depth.clearDepth                 = 1.0f;
  depth.stencilLoadOp              = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp             = GPU_STORE_OP_DONT_CARE;
  passInfo.label                   = "coordinate-depth-pass";
  passInfo.pDepthStencilAttachment = &depth;
  renderPass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!renderPass) {
    fprintf(stderr, "coordinate depth pass failed\n");
    goto cleanup;
  }
  scissor.x      = -2;
  scissor.y      = -3;
  scissor.width  = width + 2u;
  scissor.height = height + 3u;
  GPUSetViewport(renderPass, &viewport);
  GPUSetScissor(renderPass, &scissor);
  vertexBinding.buffer = nearVertices;
  GPUBindRenderPipeline(renderPass, redPipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  vertexBinding.buffer = clippedVertices;
  GPUBindRenderPipeline(renderPass, bluePipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  vertexBinding.buffer = farVertices;
  GPUBindRenderPipeline(renderPass, greenPipeline);
  GPUBindVertexBuffers(renderPass, 0u, 1u, &vertexBinding);
  GPUDraw(renderPass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarriers[0].texture   = textureTarget;
  textureBarriers[1].texture   = repeatTarget;
  textureBarriers[2].texture   = depthTarget;
  for (uint32_t i = 0u; i < 3u; i++) {
    textureBarriers[i].srcAccess  = GPU_ACCESS_COLOR_WRITE;
    textureBarriers[i].dstAccess  = GPU_ACCESS_TRANSFER_READ;
    textureBarriers[i].mipCount   = 1u;
    textureBarriers[i].layerCount = 1u;
  }
  textureBarriers[3].texture    = sampleTexture;
  textureBarriers[3].srcAccess  = GPU_ACCESS_SHADER_READ;
  textureBarriers[3].dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarriers[3].mipCount   = 1u;
  textureBarriers[3].layerCount = 1u;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 4u;
  barrierBatch.pTextureBarriers    = textureBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginTransferPass(cmdb, "coordinate-readback");
  if (!copyPass) {
    fprintf(stderr, "coordinate copy pass failed\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow        = rowPitch;
  copyRegion.rowsPerImage       = height;
  copyRegion.texture.width      = width;
  copyRegion.texture.height     = height;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = 1u;
  GPUCopyTextureToBuffer(copyPass,
                         textureTarget,
                         readbackBuffer,
                         &copyRegion);
  copyRegion.bufferOffset = imageBytes;
  GPUCopyTextureToBuffer(copyPass,
                         repeatTarget,
                         readbackBuffer,
                         &copyRegion);
  copyRegion.bufferOffset       = 2u * imageBytes;
  GPUCopyTextureToBuffer(copyPass,
                         depthTarget,
                         readbackBuffer,
                         &copyRegion);
  copyRegion.bufferOffset       = 3u * imageBytes;
  copyRegion.bytesPerRow        = rowPitch;
  copyRegion.rowsPerImage       = 2u;
  copyRegion.texture.width      = 2u;
  copyRegion.texture.height     = 2u;
  GPUCopyTextureToBuffer(copyPass,
                         sampleTexture,
                         readbackBuffer,
                         &copyRegion);
  GPUEndTransferPass(copyPass);
  copyPass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "coordinate fence setup failed\n");
    goto cleanup;
  }
  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "coordinate submit failed\n");
    cmdb = NULL;
    goto cleanup;
  }
  cmdb = NULL;
  if (GPUQueueReadBuffer(queue,
                         readbackBuffer,
                         0u,
                         pixels,
                         sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "coordinate readback failed\n");
    goto cleanup;
  }

  topRight    = (width - 1u) * 4u;
  bottomLeft  = (size_t)(height - 1u) * rowPitch;
  bottomRight = bottomLeft + (width - 1u) * 4u;
  repeatOffset = (size_t)imageBytes;
  depthCenter  = (size_t)(2u * imageBytes) + 2u * rowPitch + 2u * 4u;
  sampleOffset = (size_t)(3u * imageBytes);
  if (memcmp(&pixels[sampleOffset],
             &kTexturePixels[0],
             2u * 4u) != 0 ||
      memcmp(&pixels[sampleOffset + rowPitch],
             &kTexturePixels[2u * 4u],
             2u * 4u) != 0) {
    fprintf(stderr,
            "coordinate source texture mismatch: "
            "TL=%u,%u,%u,%u TR=%u,%u,%u,%u "
            "BL=%u,%u,%u,%u BR=%u,%u,%u,%u\n",
            pixels[sampleOffset + 0u], pixels[sampleOffset + 1u],
            pixels[sampleOffset + 2u], pixels[sampleOffset + 3u],
            pixels[sampleOffset + 4u], pixels[sampleOffset + 5u],
            pixels[sampleOffset + 6u], pixels[sampleOffset + 7u],
            pixels[sampleOffset + rowPitch + 0u],
            pixels[sampleOffset + rowPitch + 1u],
            pixels[sampleOffset + rowPitch + 2u],
            pixels[sampleOffset + rowPitch + 3u],
            pixels[sampleOffset + rowPitch + 4u],
            pixels[sampleOffset + rowPitch + 5u],
            pixels[sampleOffset + rowPitch + 6u],
            pixels[sampleOffset + rowPitch + 7u]);
    goto cleanup;
  }
  if (!coordinate_pixel(&pixels[0], 255u, 0u, 0u) ||
      !coordinate_pixel(&pixels[topRight], 0u, 255u, 0u) ||
      !coordinate_pixel(&pixels[bottomLeft], 0u, 0u, 255u) ||
      !coordinate_pixel(&pixels[bottomRight], 255u, 255u, 255u)) {
    fprintf(stderr,
            "coordinate texture orientation mismatch: "
            "TL=%u,%u,%u,%u TR=%u,%u,%u,%u "
            "BL=%u,%u,%u,%u BR=%u,%u,%u,%u\n",
            pixels[0], pixels[1], pixels[2], pixels[3],
            pixels[topRight], pixels[topRight + 1u],
            pixels[topRight + 2u], pixels[topRight + 3u],
            pixels[bottomLeft], pixels[bottomLeft + 1u],
            pixels[bottomLeft + 2u], pixels[bottomLeft + 3u],
            pixels[bottomRight], pixels[bottomRight + 1u],
            pixels[bottomRight + 2u], pixels[bottomRight + 3u]);
    goto cleanup;
  }
  if (!coordinate_pixel(&pixels[repeatOffset], 255u, 64u, 64u) ||
      !coordinate_pixel(&pixels[repeatOffset + topRight],
                        255u,
                        255u,
                        255u) ||
      !coordinate_pixel(&pixels[repeatOffset + bottomLeft], 64u, 64u, 255u) ||
      !coordinate_pixel(&pixels[repeatOffset + bottomRight],
                        255u,
                        255u,
                        255u)) {
    fprintf(stderr,
            "coordinate repeat sampling mismatch: "
            "TL=%u,%u,%u,%u TR=%u,%u,%u,%u "
            "BL=%u,%u,%u,%u BR=%u,%u,%u,%u\n",
            pixels[repeatOffset + 0u], pixels[repeatOffset + 1u],
            pixels[repeatOffset + 2u], pixels[repeatOffset + 3u],
            pixels[repeatOffset + topRight + 0u],
            pixels[repeatOffset + topRight + 1u],
            pixels[repeatOffset + topRight + 2u],
            pixels[repeatOffset + topRight + 3u],
            pixels[repeatOffset + bottomLeft + 0u],
            pixels[repeatOffset + bottomLeft + 1u],
            pixels[repeatOffset + bottomLeft + 2u],
            pixels[repeatOffset + bottomLeft + 3u],
            pixels[repeatOffset + bottomRight + 0u],
            pixels[repeatOffset + bottomRight + 1u],
            pixels[repeatOffset + bottomRight + 2u],
            pixels[repeatOffset + bottomRight + 3u]);
    goto cleanup;
  }
  if (!coordinate_pixel(&pixels[depthCenter], 255u, 0u, 0u)) {
    fprintf(stderr,
            "coordinate depth/culling mismatch: %u %u %u %u\n",
            pixels[depthCenter + 0u],
            pixels[depthCenter + 1u],
            pixels[depthCenter + 2u],
            pixels[depthCenter + 3u]);
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (copyPass) {
    GPUEndTransferPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(repeatClampGroup);
  GPUDestroyBindGroup(repeatGroup);
  GPUDestroyBindGroup(samplerGroup);
  GPUDestroyBindGroup(textureGroup);
  GPUDestroySampler(repeatSampler);
  GPUDestroySampler(sampler);
  GPUDestroyTextureView(depthView);
  GPUDestroyTexture(depthTexture);
  GPUDestroyTextureView(depthTargetView);
  GPUDestroyTexture(depthTarget);
  GPUDestroyTextureView(repeatView);
  GPUDestroyTexture(repeatTarget);
  GPUDestroyTextureView(textureView);
  GPUDestroyTexture(textureTarget);
  GPUDestroyTextureView(sampleView);
  GPUDestroyTexture(sampleTexture);
  GPUDestroyBuffer(readbackBuffer);
  GPUDestroyBuffer(clippedVertices);
  GPUDestroyBuffer(farVertices);
  GPUDestroyBuffer(nearVertices);
  GPUDestroyBuffer(textureVertices);
  GPUDestroyRenderPipeline(bluePipeline);
  GPUDestroyRenderPipeline(greenPipeline);
  GPUDestroyRenderPipeline(redPipeline);
  GPUDestroyRenderPipeline(repeatPipeline);
  GPUDestroyRenderPipeline(texturePipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
