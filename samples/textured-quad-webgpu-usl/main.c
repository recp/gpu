#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <gpu/gpu.h>

#include "../common/webgpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct QuadVertex {
  float position[4];
  float uv[2];
} QuadVertex;

typedef struct FragmentUniforms {
  float tint[4];
} FragmentUniforms;

enum {
  WARM_FRAME_COUNT = 8u
};

typedef struct WebGPUTexturedQuad {
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
  GPUBuffer         *uniformBuffer;
  GPUTexture        *texture;
  GPUTextureView    *textureView;
  GPUSampler        *sampler;
  GPUBindGroup      *fragmentGroup;
  GPUBindGroup      *samplerGroup;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUTexturedQuad;

static const QuadVertex kQuadVertices[] = {
  { { -0.8f, -0.8f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
  { {  0.8f, -0.8f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { { -0.8f,  0.8f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { { -0.8f,  0.8f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { {  0.8f, -0.8f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { {  0.8f,  0.8f, 0.0f, 1.0f }, { 1.0f, 0.0f } }
};

static const uint8_t kCheckerPixels[] = {
  255,   0,   0, 255,    0, 255,   0, 255,
    0,   0, 255, 255,  255, 255, 255, 255
};

static WebGPUTexturedQuad app;

static int
resize_canvas(WebGPUTexturedQuad *state) {
  double cssWidth;
  double cssHeight;
  double scale;
  uint32_t width;
  uint32_t height;

  if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    return 0;
  }

  scale  = emscripten_get_device_pixel_ratio();
  width  = (uint32_t)(cssWidth * scale + 0.5);
  height = (uint32_t)(cssHeight * scale + 0.5);
  if (width == 0u || height == 0u) {
    return 0;
  }
  if (width == state->width && height == state->height) {
    return 1;
  }

  emscripten_set_canvas_element_size("#canvas", (int)width, (int)height);
  if (state->swapchain &&
      GPUResizeSwapchain(state->swapchain, width, height) != GPU_OK) {
    return 0;
  }
  state->width  = width;
  state->height = height;
  return 1;
}

static int
create_shader(WebGPUTexturedQuad *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/textured_quad.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /textured_quad.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the USL artifact", 1);
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
    set_status("GPU: unexpected WebGPU shader reflection", 1);
    return 0;
  }
  return 1;
}

static int
create_pipeline(WebGPUTexturedQuad *state) {
  GPUVertexAttribute vertexAttributes[] = {
    { GPU_VERTEX_FORMAT_FLOAT32X4, 0u, offsetof(QuadVertex, position) },
    { GPU_VERTEX_FORMAT_FLOAT32X2, 1u, offsetof(QuadVertex, uv) }
  };
  GPUVertexBufferLayout vertexBuffer = {
    .pAttributes   = vertexAttributes,
    .strideBytes   = sizeof(QuadVertex),
    .stepMode      = GPU_VERTEX_STEP_MODE_VERTEX,
    .attributeCount = 2u
  };
  GPUColorTargetState color = {0};
  GPURenderPipelineCreateInfo info = {0};

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize     = sizeof(info);
  info.label                = "textured-quad-webgpu-usl-pipeline";
  info.layout               = state->shaderLayout->pipelineLayout;
  info.library              = state->library;
  info.vertexEntry          = "quad_vs";
  info.fragmentEntry        = "quad_fs";
  info.pColorTargets        = &color;
  info.pDepthStencilState   = NULL;
  info.vertex.pBufferLayouts = &vertexBuffer;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount     = 1u;
  info.depthStencilFormat   = GPU_FORMAT_UNDEFINED;
  info.primitiveTopology    = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode             = GPU_CULL_MODE_NONE;
  info.frontFace            = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->pipeline) != GPU_OK ||
      !state->pipeline) {
    set_status("GPU: failed to create WebGPU pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUTexturedQuad *state) {
  const FragmentUniforms uniforms = { { 1.0f, 0.86f, 0.72f, 1.0f } };
  GPUBufferCreateInfo vertexInfo = {0};
  GPUBufferCreateInfo uniformInfo = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUTextureWriteRegion writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUSamplerCreateInfo samplerInfo = {0};
  GPUBindGroupEntry fragmentEntries[2] = {0};
  GPUBindGroupEntry samplerEntry = {0};
  GPUBindGroupCreateInfo fragmentGroupInfo = {0};
  GPUBindGroupCreateInfo samplerGroupInfo = {0};

  vertexInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  vertexInfo.chain.structSize = sizeof(vertexInfo);
  vertexInfo.label            = "textured-quad-webgpu-usl-vertices";
  vertexInfo.sizeBytes        = sizeof(kQuadVertices);
  vertexInfo.usage = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &vertexInfo,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          kQuadVertices,
                          sizeof(kQuadVertices)) != GPU_OK) {
    set_status("GPU: failed to create or upload the vertex buffer", 1);
    return 0;
  }

  uniformInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  uniformInfo.chain.structSize = sizeof(uniformInfo);
  uniformInfo.label            = "textured-quad-webgpu-usl-uniforms";
  uniformInfo.sizeBytes        = sizeof(uniforms);
  uniformInfo.usage = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &uniformInfo,
                      &state->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    set_status("GPU: failed to create or upload the uniform buffer", 1);
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "textured-quad-webgpu-usl-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 2u;
  textureInfo.height           = 2u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage = GPU_TEXTURE_USAGE_SAMPLED | GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->texture) != GPU_OK) {
    set_status("GPU: failed to create the checker texture", 1);
    return 0;
  }

  writeRegion.aspect         = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width          = 2u;
  writeRegion.height         = 2u;
  writeRegion.depth          = 1u;
  writeRegion.layerCount     = 1u;
  writeRegion.bytesPerRow    = 8u;
  writeRegion.rowsPerImage   = 2u;
  if (GPUQueueWriteTexture(state->queue,
                           state->texture,
                           &writeRegion,
                           kCheckerPixels,
                           sizeof(kCheckerPixels)) != GPU_OK) {
    set_status("GPU: failed to upload the checker texture", 1);
    return 0;
  }

  viewInfo.chain.sType       = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize  = sizeof(viewInfo);
  viewInfo.label             = "textured-quad-webgpu-usl-view";
  viewInfo.viewType          = GPU_TEXTURE_VIEW_2D;
  viewInfo.format            = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount     = 1u;
  viewInfo.arrayLayerCount   = 1u;
  if (GPUCreateTextureView(state->texture,
                           &viewInfo,
                           &state->textureView) != GPU_OK) {
    set_status("GPU: failed to create the checker texture view", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "textured-quad-webgpu-usl-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    set_status("GPU: failed to create the checker sampler", 1);
    return 0;
  }

  fragmentEntries[0].textureView = state->textureView;
  fragmentEntries[0].binding     = 0u;
  fragmentEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  fragmentEntries[1].buffer.buffer = state->uniformBuffer;
  fragmentEntries[1].buffer.size   = sizeof(uniforms);
  fragmentEntries[1].binding       = 1u;
  fragmentEntries[1].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  fragmentGroupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  fragmentGroupInfo.chain.structSize = sizeof(fragmentGroupInfo);
  fragmentGroupInfo.label        = "textured-quad-webgpu-usl-group0";
  fragmentGroupInfo.layout       = state->shaderLayout->bindGroupLayouts[0];
  fragmentGroupInfo.pEntries     = fragmentEntries;
  fragmentGroupInfo.entryCount   = 2u;
  if (GPUCreateBindGroup(state->device,
                         &fragmentGroupInfo,
                         &state->fragmentGroup) != GPU_OK) {
    set_status("GPU: failed to create reflected group 0", 1);
    return 0;
  }

  samplerEntry.sampler     = state->sampler;
  samplerEntry.binding     = 0u;
  samplerEntry.bindingType = GPU_BINDING_SAMPLER;
  samplerGroupInfo.chain.sType = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  samplerGroupInfo.chain.structSize = sizeof(samplerGroupInfo);
  samplerGroupInfo.label      = "textured-quad-webgpu-usl-group1";
  samplerGroupInfo.layout     = state->shaderLayout->bindGroupLayouts[1];
  samplerGroupInfo.pEntries   = &samplerEntry;
  samplerGroupInfo.entryCount = 1u;
  if (GPUCreateBindGroup(state->device,
                         &samplerGroupInfo,
                         &state->samplerGroup) != GPU_OK) {
    set_status("GPU: failed to create reflected group 1", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUTexturedQuad            *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPUBufferBinding               vertexBuffer = {0};
  GPURenderPassColorAttachment   color = {0};
  GPURenderPassCreateInfo        passInfo = {0};

  state = userData;
  if (!resize_canvas(state)) {
    return;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return;
  }
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "textured-quad-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.015f;
  color.clearColor.float32[1] = 0.035f;
  color.clearColor.float32[2] = 0.085f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "textured-quad-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  vertexBuffer.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindRenderGroup(pass, 0u, state->fragmentGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, state->samplerGroup, 0u, NULL);
  GPUDraw(pass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU textured-quad frame\n");
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.hotPathAllocCount != 0u || stats.hotPathFreeCount != 0u)) {
      set_status("GPU: warm WebGPU frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  WebGPUTexturedQuad *state;
  GPURuntimeConfig    runtime = {0};

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
  if (!state->swapchain) {
    set_status("GPU: failed to create WebGPU swapchain", 1);
    return;
  }
  if (!create_shader(state) ||
      !create_pipeline(state) ||
      !create_resources(state)) {
    return;
  }

  set_status("GPU: WebGPU USL textured quad ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  WebGPUTexturedQuad *state;

  state = userData;
  if (result != GPU_OK || !adapter) {
    set_status("GPU: failed to request WebGPU adapter", 1);
    return;
  }

  state->adapter = adapter;
  set_status("GPU: WebGPU adapter ready", 0);
  result = GPURequestDevice(adapter, NULL, device_ready, state);
  if (result != GPU_OK) {
    fprintf(stderr, "GPU: failed to start WebGPU device request (%d)\n", result);
  }
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "textured-quad-webgpu-usl";
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
    fprintf(stderr, "GPU: failed to start WebGPU adapter request (%d)\n",
            result);
    return 1;
  }
  return 0;
}
