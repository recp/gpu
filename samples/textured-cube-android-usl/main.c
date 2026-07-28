#include "../common/android.h"
#include "../common/android_samples.h"
#include "../textured-cube-usl/CubeData.h"

#include <stddef.h>

typedef struct CubeRenderer {
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *vertexBuffer;
  GPUBuffer         *indexBuffer;
  GPUBuffer         *uniformBuffer;
  GPUTexture        *texture;
  GPUTextureView    *textureView;
  GPUTexture        *depthTexture;
  GPUTextureView    *depthView;
  GPUSampler        *sampler;
  GPUBindGroup      *materialGroup;
  GPUBindGroup      *samplerGroup;
  double             animationStart;
  uint32_t           frameCount;
  mat4               viewProjection;
} CubeRenderer;

enum {
  WARM_FRAME_COUNT = 8u
};

static bool
cube_create_depth(GPUAndroidSample *sample,
                  CubeRenderer     *state,
                  uint32_t          width,
                  uint32_t          height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;

  texture = NULL;
  view    = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-cube-depth";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  if (GPUCreateTexture(sample->device, &textureInfo, &texture) != GPU_OK) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "textured-cube-depth-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK) {
    GPUDestroyTexture(texture);
    return false;
  }

  GPUDestroyTextureView(state->depthView);
  GPUDestroyTexture(state->depthTexture);
  state->depthTexture = texture;
  state->depthView    = view;
  CubeBuildViewProjection(width, height, state->viewProjection);
  return true;
}

static bool
cube_create_pipeline(GPUAndroidSample *sample, CubeRenderer *state) {
  GPUVertexAttribute          attributes[3] = {0};
  GPUVertexBufferLayout       vertexLayout  = {0};
  GPUColorTargetState         color         = {0};
  GPUDepthStencilState        depth         = {0};
  GPURenderPipelineCreateInfo info          = {0};

  if (!GPUSampleAndroidLoadUSL(sample,
                               "textured_cube.us",
                               &state->library) ||
      GPUCreateShaderLayout(sample->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 2u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0] ||
      !state->shaderLayout->bindGroupLayouts[1]) {
    return GPUSampleAndroidFail(sample, "cube shader layout");
  }

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[0].offset          = offsetof(CubeVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[1].offset          = offsetof(CubeVertex, normal);
  attributes[1].shaderLocation = 1u;
  attributes[2].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[2].offset          = offsetof(CubeVertex, uv);
  attributes[2].shaderLocation = 2u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(CubeVertex);
  vertexLayout.attributeCount   = 3u;
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format          = GPUGetSwapchainFormat(sample->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "textured-cube-android-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "cube_vs";
  info.fragmentEntry            = "cube_fs";
  info.pColorTargets            = &color;
  info.pDepthStencilState       = &depth;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount         = 1u;
  info.depthStencilFormat       = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_BACK;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  return GPUCreateRenderPipeline(sample->device,
                                 &info,
                                 &state->pipeline) == GPU_OK &&
         state->pipeline;
}

static bool
cube_create_geometry(GPUAndroidSample *sample, CubeRenderer *state) {
  CubeUniforms        uniforms;
  GPUBufferCreateInfo info = {0};

  CubeBuildUniforms(0.0f, state->viewProjection, &uniforms);

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "textured-cube-vertices";
  info.sizeBytes        = sizeof(kCubeVertices);
  info.usage            = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(sample->device,
                      &info,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(sample->queue,
                          state->vertexBuffer,
                          0u,
                          kCubeVertices,
                          sizeof(kCubeVertices)) != GPU_OK) {
    return false;
  }

  info.label     = "textured-cube-indices";
  info.sizeBytes = sizeof(kCubeIndices);
  info.usage     = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(sample->device,
                      &info,
                      &state->indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(sample->queue,
                          state->indexBuffer,
                          0u,
                          kCubeIndices,
                          sizeof(kCubeIndices)) != GPU_OK) {
    return false;
  }

  info.label     = "textured-cube-uniforms";
  info.sizeBytes = sizeof(uniforms);
  info.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(sample->device,
                      &info,
                      &state->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(sample->queue,
                          state->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    return false;
  }
  return true;
}

static bool
cube_create_material(GPUAndroidSample *sample, CubeRenderer *state) {
  uint8_t                  pixels[CUBE_CHECKER_SIZE * CUBE_CHECKER_SIZE * 4u];
  GPUTextureCreateInfo     textureInfo       = {0};
  GPUTextureWriteRegion    writeRegion       = {0};
  GPUTextureViewCreateInfo viewInfo          = {0};
  GPUSamplerCreateInfo     samplerInfo       = {0};
  GPUBindGroupEntry        materialEntries[2] = {0};
  GPUBindGroupEntry        samplerEntry      = {0};
  GPUBindGroupCreateInfo   materialInfo      = {0};
  GPUBindGroupCreateInfo   samplerGroupInfo  = {0};

  CubeFillChecker(pixels);
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-cube-checker";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = CUBE_CHECKER_SIZE;
  textureInfo.height           = CUBE_CHECKER_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(sample->device,
                       &textureInfo,
                       &state->texture) != GPU_OK) {
    return false;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = CUBE_CHECKER_SIZE;
  writeRegion.height       = CUBE_CHECKER_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = CUBE_CHECKER_SIZE * 4u;
  writeRegion.rowsPerImage = CUBE_CHECKER_SIZE;
  if (GPUQueueWriteTexture(sample->queue,
                           state->texture,
                           &writeRegion,
                           pixels,
                           sizeof(pixels)) != GPU_OK) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "textured-cube-checker-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(state->texture,
                           &viewInfo,
                           &state->textureView) != GPU_OK) {
    return false;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "textured-cube-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_REPEAT;
  if (GPUCreateSampler(sample->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    return false;
  }

  materialEntries[0].buffer.buffer = state->uniformBuffer;
  materialEntries[0].buffer.size   = sizeof(CubeUniforms);
  materialEntries[0].binding       = 0u;
  materialEntries[0].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  materialEntries[1].textureView   = state->textureView;
  materialEntries[1].binding       = 1u;
  materialEntries[1].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  materialInfo.chain.sType         = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  materialInfo.chain.structSize    = sizeof(materialInfo);
  materialInfo.label               = "textured-cube-group0";
  materialInfo.layout              = state->shaderLayout->bindGroupLayouts[0];
  materialInfo.pEntries            = materialEntries;
  materialInfo.entryCount          = 2u;
  if (GPUCreateBindGroup(sample->device,
                         &materialInfo,
                         &state->materialGroup) != GPU_OK) {
    return false;
  }

  samplerEntry.sampler              = state->sampler;
  samplerEntry.binding              = 0u;
  samplerEntry.bindingType          = GPU_BINDING_SAMPLER;
  samplerGroupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  samplerGroupInfo.chain.structSize = sizeof(samplerGroupInfo);
  samplerGroupInfo.label            = "textured-cube-group1";
  samplerGroupInfo.layout           = state->shaderLayout->bindGroupLayouts[1];
  samplerGroupInfo.pEntries         = &samplerEntry;
  samplerGroupInfo.entryCount       = 1u;
  return GPUCreateBindGroup(sample->device,
                            &samplerGroupInfo,
                            &state->samplerGroup) == GPU_OK &&
         state->samplerGroup;
}

static bool
cube_create(GPUAndroidSample *sample, void *userData) {
  CubeRenderer *state;

  state = userData;
  CubeBuildViewProjection(sample->width,
                          sample->height,
                          state->viewProjection);
  state->animationStart = GPUSampleAndroidTime();
  if (!cube_create_depth(sample,
                         state,
                         sample->width,
                         sample->height)) {
    return GPUSampleAndroidFail(sample, "cube depth target");
  }
  if (!cube_create_pipeline(sample, state)) {
    return GPUSampleAndroidFail(sample, "cube pipeline");
  }
  if (!cube_create_geometry(sample, state)) {
    return GPUSampleAndroidFail(sample, "cube geometry");
  }
  if (!cube_create_material(sample, state)) {
    return GPUSampleAndroidFail(sample, "cube material");
  }
  return true;
}

static bool
cube_resize(GPUAndroidSample *sample,
            uint32_t          width,
            uint32_t          height,
            void             *userData) {
  return cube_create_depth(sample, userData, width, height);
}

static bool
cube_update_uniforms(GPUAndroidSample *sample, CubeRenderer *state) {
  CubeUniforms uniforms;
  float        seconds;

  seconds = (float)(GPUSampleAndroidTime() - state->animationStart);
  CubeBuildUniforms(seconds, state->viewProjection, &uniforms);
  return GPUQueueWriteBuffer(sample->queue,
                             state->uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

static bool
cube_render(GPUAndroidSample *sample, void *userData) {
  CubeRenderer                        *state;
  GPUFrame                           *frame;
  GPUCommandBuffer                   *cmdb;
  GPURenderPassEncoder               *pass;
  GPUBufferBinding                    vertexBuffer = {0};
  GPURenderPassColorAttachment        color        = {0};
  GPURenderPassDepthStencilAttachment depth        = {0};
  GPURenderPassCreateInfo             passInfo     = {0};
  GPUFrameStats                       stats;

  state = userData;
  if (!cube_update_uniforms(sample, state)) {
    return false;
  }

  frame = GPUBeginFrame(sample->swapchain);
  if (!frame) {
    return true;
  }

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(sample->queue,
                              "textured-cube-android-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return false;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.018f;
  color.clearColor.float32[2] = 0.048f;
  color.clearColor.float32[3] = 1.0f;
  depth.view                  = state->depthView;
  depth.depthLoadOp           = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp          = GPU_STORE_OP_DONT_CARE;
  depth.stencilLoadOp         = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp        = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth            = 1.0f;
  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "textured-cube-android-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depth;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return false;
  }

  vertexBuffer.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->materialGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, state->samplerGroup, 0u, NULL);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindIndexBuffer(pass, state->indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexed(pass, CUBE_INDEX_COUNT, 1u, 0u, 0, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(sample->queue, cmdb, frame) != GPU_OK) {
    return false;
  }

  state->frameCount++;
  return state->frameCount <= WARM_FRAME_COUNT ||
         GPUGetLastFrameStats(sample->device, &stats) != GPU_OK ||
         (stats.hotPathAllocCount == 0u && stats.hotPathFreeCount == 0u);
}

static void
cube_destroy(GPUAndroidSample *sample, void *userData) {
  CubeRenderer *state;

  (void)sample;
  state = userData;
  GPUDestroyBindGroup(state->samplerGroup);
  GPUDestroyBindGroup(state->materialGroup);
  GPUDestroyRenderPipeline(state->pipeline);
  GPUDestroySampler(state->sampler);
  GPUDestroyTextureView(state->depthView);
  GPUDestroyTexture(state->depthTexture);
  GPUDestroyTextureView(state->textureView);
  GPUDestroyTexture(state->texture);
  GPUDestroyBuffer(state->uniformBuffer);
  GPUDestroyBuffer(state->indexBuffer);
  GPUDestroyBuffer(state->vertexBuffer);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  *state = (CubeRenderer){0};
}

const GPUAndroidSampleDefinition*
GPUSampleAndroidTexturedCube(void) {
  static const GPUAndroidSampleCallbacks callbacks = {
    .create  = cube_create,
    .resize  = cube_resize,
    .render  = cube_render,
    .destroy = cube_destroy
  };
  static const GPUAndroidSampleDefinition definition = {
    .callbacks    = &callbacks,
    .id           = "textured-cube",
    .name         = "GPU + USL Rotating Cube",
    .userDataSize = sizeof(CubeRenderer)
  };

  return &definition;
}

#if GPU_ANDROID_SAMPLE_STANDALONE
void
android_main(struct android_app *app) {
  GPUSampleAndroidRunDefinition(app, GPUSampleAndroidTexturedCube());
}
#endif
