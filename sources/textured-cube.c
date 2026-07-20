#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#include <gpu/gpu.h>

#include "../common/webgpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct CubeVertex {
  float position[3];
  float normal[3];
  float uv[2];
} CubeVertex;

typedef struct CubeUniforms {
  mat4 mvp;
  mat4 model;
} CubeUniforms;

typedef struct WebGPUTexturedCube {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
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
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
  mat4               viewProjection;
} WebGPUTexturedCube;

enum {
  CHECKER_SIZE     = 16u,
  INDEX_COUNT      = 36u,
  WARM_FRAME_COUNT = 8u
};

_Static_assert(sizeof(CubeUniforms) == 128u,
               "cube uniform layout must match two float4x4 matrices");
_Static_assert(sizeof(CubeVertex) == 32u,
               "cube vertex layout must match the pipeline stride");

#define CUBE_VERTEX(px, py, pz, nx, ny, nz, u, v) \
  {{px, py, pz}, {nx, ny, nz}, {u, v}}

static const CubeVertex kCubeVertices[] = {
  CUBE_VERTEX(-1, -1,  1,  0,  0,  1, 0, 1),
  CUBE_VERTEX( 1, -1,  1,  0,  0,  1, 1, 1),
  CUBE_VERTEX( 1,  1,  1,  0,  0,  1, 1, 0),
  CUBE_VERTEX(-1,  1,  1,  0,  0,  1, 0, 0),

  CUBE_VERTEX( 1, -1, -1,  0,  0, -1, 0, 1),
  CUBE_VERTEX(-1, -1, -1,  0,  0, -1, 1, 1),
  CUBE_VERTEX(-1,  1, -1,  0,  0, -1, 1, 0),
  CUBE_VERTEX( 1,  1, -1,  0,  0, -1, 0, 0),

  CUBE_VERTEX( 1, -1,  1,  1,  0,  0, 0, 1),
  CUBE_VERTEX( 1, -1, -1,  1,  0,  0, 1, 1),
  CUBE_VERTEX( 1,  1, -1,  1,  0,  0, 1, 0),
  CUBE_VERTEX( 1,  1,  1,  1,  0,  0, 0, 0),

  CUBE_VERTEX(-1, -1, -1, -1,  0,  0, 0, 1),
  CUBE_VERTEX(-1, -1,  1, -1,  0,  0, 1, 1),
  CUBE_VERTEX(-1,  1,  1, -1,  0,  0, 1, 0),
  CUBE_VERTEX(-1,  1, -1, -1,  0,  0, 0, 0),

  CUBE_VERTEX(-1,  1,  1,  0,  1,  0, 0, 1),
  CUBE_VERTEX( 1,  1,  1,  0,  1,  0, 1, 1),
  CUBE_VERTEX( 1,  1, -1,  0,  1,  0, 1, 0),
  CUBE_VERTEX(-1,  1, -1,  0,  1,  0, 0, 0),

  CUBE_VERTEX(-1, -1, -1,  0, -1,  0, 0, 1),
  CUBE_VERTEX( 1, -1, -1,  0, -1,  0, 1, 1),
  CUBE_VERTEX( 1, -1,  1,  0, -1,  0, 1, 0),
  CUBE_VERTEX(-1, -1,  1,  0, -1,  0, 0, 0)
};

#undef CUBE_VERTEX

static const uint16_t kCubeIndices[] = {
   0u,  1u,  2u,  0u,  2u,  3u,
   4u,  5u,  6u,  4u,  6u,  7u,
   8u,  9u, 10u,  8u, 10u, 11u,
  12u, 13u, 14u, 12u, 14u, 15u,
  16u, 17u, 18u, 16u, 18u, 19u,
  20u, 21u, 22u, 20u, 22u, 23u
};

_Static_assert(sizeof(kCubeIndices) / sizeof(kCubeIndices[0]) == INDEX_COUNT,
               "cube index count must match the draw call");

static const uint8_t kCheckerOrange[] = {255u, 122u, 18u, 255u};
static const uint8_t kCheckerBlue[]   = {18u, 154u, 230u, 255u};

static WebGPUTexturedCube app;

static void
update_camera(WebGPUTexturedCube *state) {
  vec3 eye    = {0.0f, 0.0f, 4.5f};
  vec3 center = {0.0f, 0.0f, 0.0f};
  vec3 up     = {0.0f, 1.0f, 0.0f};
  mat4 view;
  mat4 projection;
  float aspect;

  aspect = state->height > 0u
             ? (float)state->width / (float)state->height
             : 1.0f;
  glm_lookat(eye, center, up, view);
  glm_perspective(glm_rad(48.0f), aspect, 0.1f, 100.0f, projection);
  glm_mat4_mul(projection, view, state->viewProjection);
}

static int
create_depth_target(WebGPUTexturedCube *state,
                    uint32_t            width,
                    uint32_t            height) {
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
  if (GPUCreateTexture(state->device, &textureInfo, &texture) != GPU_OK) {
    return 0;
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
    return 0;
  }

  GPUDestroyTextureView(state->depthView);
  GPUDestroyTexture(state->depthTexture);
  state->depthTexture = texture;
  state->depthView    = view;
  return 1;
}

static int
resize_canvas(WebGPUTexturedCube *state) {
  double   cssWidth;
  double   cssHeight;
  double   scale;
  uint32_t width;
  uint32_t height;

  if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    return 0;
  }

  scale  = emscripten_get_device_pixel_ratio();
  width  = (uint32_t)(cssWidth * scale);
  height = (uint32_t)(cssHeight * scale);
  if (width == 0u || height == 0u) return 0;
  if (width == state->width && height == state->height) return 1;

  emscripten_set_canvas_element_size("#canvas", (int)width, (int)height);
  if (state->swapchain &&
      (GPUResizeSwapchain(state->swapchain, width, height) != GPU_OK ||
       !create_depth_target(state, width, height))) {
    return 0;
  }
  state->width  = width;
  state->height = height;
  update_camera(state);
  return 1;
}

static int
create_pipeline(WebGPUTexturedCube *state) {
  GPUVertexAttribute          attributes[3] = {0};
  GPUVertexBufferLayout       vertexLayout  = {0};
  GPUColorTargetState         color         = {0};
  GPUDepthStencilState        depth         = {0};
  GPURenderPipelineCreateInfo info          = {0};
  void                       *artifact;
  uint64_t                    artifactSize;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/textured_cube.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /textured_cube.us", 1);
    return 0;
  }
  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the textured cube artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 2u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0] ||
      !state->shaderLayout->bindGroupLayouts[1]) {
    set_status("GPU: unexpected textured cube reflection", 1);
    return 0;
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

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "textured-cube-webgpu-usl-pipeline";
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
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create the textured cube pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_geometry(WebGPUTexturedCube *state) {
  CubeUniforms        uniforms;
  GPUBufferCreateInfo info = {0};

  glm_mat4_identity(uniforms.mvp);
  glm_mat4_identity(uniforms.model);

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "textured-cube-vertices";
  info.sizeBytes        = sizeof(kCubeVertices);
  info.usage            = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device, &info, &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          kCubeVertices,
                          sizeof(kCubeVertices)) != GPU_OK) {
    return 0;
  }

  info.label     = "textured-cube-indices";
  info.sizeBytes = sizeof(kCubeIndices);
  info.usage     = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device, &info, &state->indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->indexBuffer,
                          0u,
                          kCubeIndices,
                          sizeof(kCubeIndices)) != GPU_OK) {
    return 0;
  }

  info.label     = "textured-cube-uniforms";
  info.sizeBytes = sizeof(uniforms);
  info.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device, &info, &state->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    return 0;
  }
  return 1;
}

static void
fill_checker(uint8_t *pixels) {
  for (uint32_t y = 0u; y < CHECKER_SIZE; y++) {
    for (uint32_t x = 0u; x < CHECKER_SIZE; x++) {
      const uint8_t *color;
      uint32_t       offset;

      color  = (((x / 4u) ^ (y / 4u)) & 1u)
                 ? kCheckerOrange
                 : kCheckerBlue;
      offset = (y * CHECKER_SIZE + x) * 4u;
      pixels[offset + 0u] = color[0];
      pixels[offset + 1u] = color[1];
      pixels[offset + 2u] = color[2];
      pixels[offset + 3u] = color[3];
    }
  }
}

static int
create_material(WebGPUTexturedCube *state) {
  uint8_t                       pixels[CHECKER_SIZE * CHECKER_SIZE * 4u];
  GPUTextureCreateInfo          textureInfo       = {0};
  GPUTextureWriteRegion         writeRegion       = {0};
  GPUTextureViewCreateInfo      viewInfo          = {0};
  GPUSamplerCreateInfo          samplerInfo       = {0};
  GPUBindGroupEntry            materialEntries[2] = {0};
  GPUBindGroupEntry             samplerEntry      = {0};
  GPUBindGroupCreateInfo        materialInfo      = {0};
  GPUBindGroupCreateInfo        samplerGroupInfo  = {0};

  fill_checker(pixels);
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-cube-checker";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = CHECKER_SIZE;
  textureInfo.height           = CHECKER_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->texture) != GPU_OK) {
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = CHECKER_SIZE;
  writeRegion.height       = CHECKER_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = CHECKER_SIZE * 4u;
  writeRegion.rowsPerImage = CHECKER_SIZE;
  if (GPUQueueWriteTexture(state->queue,
                           state->texture,
                           &writeRegion,
                           pixels,
                           sizeof(pixels)) != GPU_OK) {
    return 0;
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
    return 0;
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
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    return 0;
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
  if (GPUCreateBindGroup(state->device,
                         &materialInfo,
                         &state->materialGroup) != GPU_OK) {
    return 0;
  }

  samplerEntry.sampler           = state->sampler;
  samplerEntry.binding           = 0u;
  samplerEntry.bindingType       = GPU_BINDING_SAMPLER;
  samplerGroupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  samplerGroupInfo.chain.structSize = sizeof(samplerGroupInfo);
  samplerGroupInfo.label            = "textured-cube-group1";
  samplerGroupInfo.layout           = state->shaderLayout->bindGroupLayouts[1];
  samplerGroupInfo.pEntries         = &samplerEntry;
  samplerGroupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(state->device,
                         &samplerGroupInfo,
                         &state->samplerGroup) != GPU_OK) {
    return 0;
  }
  return 1;
}

static int
update_uniforms(WebGPUTexturedCube *state) {
  CubeUniforms uniforms;
  vec3         axisX = {1.0f, 0.0f, 0.0f};
  vec3         axisY = {0.0f, 1.0f, 0.0f};
  float        angle;

  angle = (float)(emscripten_get_now() * 0.001);
  glm_mat4_identity(uniforms.model);
  glm_rotate(uniforms.model, angle * 0.72f, axisY);
  glm_rotate(uniforms.model, angle * 0.43f, axisX);
  glm_mat4_mul(state->viewProjection, uniforms.model, uniforms.mvp);
  return GPUQueueWriteBuffer(state->queue,
                             state->uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

static void
render_frame(void *userData) {
  WebGPUTexturedCube                *state;
  GPUFrame                          *frame;
  GPUCommandBuffer                  *cmdb;
  GPURenderPassEncoder              *pass;
  GPUBufferBinding                   vertexBuffer = {0};
  GPURenderPassColorAttachment       color        = {0};
  GPURenderPassDepthStencilAttachment depth       = {0};
  GPURenderPassCreateInfo            passInfo     = {0};

  state = userData;
  if (!resize_canvas(state) || !update_uniforms(state)) {
    set_status("GPU: failed to update textured cube frame", 1);
    emscripten_cancel_main_loop();
    return;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) return;
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "textured-cube-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
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
  passInfo.label                   = "textured-cube-webgpu-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depth;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  vertexBuffer.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->materialGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, state->samplerGroup, 0u, NULL);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindIndexBuffer(pass, state->indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexed(pass, INDEX_COUNT, 1u, 0u, 0, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the textured cube frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.hotPathAllocCount != 0u || stats.hotPathFreeCount != 0u)) {
      set_status("GPU: warm textured cube frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  WebGPUTexturedCube *state;
  GPURuntimeConfig   runtime = {0};

  state = userData;
  if (result != GPU_OK || !device) {
    set_status("GPU: failed to request WebGPU device", 1);
    return;
  }

  state->device = device;
  state->queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (GPUConfigureRuntime(device, &runtime) != GPU_OK) {
    set_status("GPU: failed to configure WebGPU runtime stats", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               state->adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->queue || !state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create WebGPU queue or canvas surface", 1);
    return;
  }

  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain ||
      !create_depth_target(state, state->width, state->height) ||
      !create_pipeline(state) ||
      !create_geometry(state) ||
      !create_material(state)) {
    set_status("GPU: failed to initialize textured cube resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL textured cube ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  WebGPUTexturedCube *state;

  state = userData;
  if (result != GPU_OK || !adapter) {
    set_status("GPU: failed to request WebGPU adapter", 1);
    return;
  }

  state->adapter = adapter;
  set_status("GPU: WebGPU adapter ready", 0);
  result = GPURequestDevice(adapter, NULL, device_ready, state);
  if (result != GPU_OK) {
    set_status("GPU: failed to start WebGPU device request", 1);
  }
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "textured-cube-webgpu-usl";
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  result = GPUCreateInstance(&info, &app.instance);
  if (result != GPU_OK || !app.instance) {
    set_status("GPU: failed to create WebGPU instance", 1);
    return 1;
  }

  set_status("GPU: requesting WebGPU adapter", 0);
  result = GPURequestAdapter(app.instance, adapter_ready, &app);
  if (result != GPU_OK) {
    set_status("GPU: failed to start WebGPU adapter request", 1);
    return 1;
  }
  return 0;
}
