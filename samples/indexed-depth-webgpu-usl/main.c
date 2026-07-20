#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <gpu/gpu.h>

#include "../common/webgpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct CubeVertex {
  float position[4];
  float color[4];
} CubeVertex;

typedef struct WebGPUIndexedDepth {
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
  GPUTexture        *depthTexture;
  GPUTextureView    *depthView;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUIndexedDepth;

enum {
  WARM_FRAME_COUNT = 8u
};

static const CubeVertex kCubeVertices[] = {
  {{-0.52f, -0.46f, 0.18f, 1.0f}, {1.00f, 0.24f, 0.08f, 1.0f}},
  {{ 0.28f, -0.46f, 0.18f, 1.0f}, {1.00f, 0.72f, 0.10f, 1.0f}},
  {{ 0.28f,  0.34f, 0.18f, 1.0f}, {0.20f, 0.92f, 0.54f, 1.0f}},
  {{-0.52f,  0.34f, 0.18f, 1.0f}, {0.10f, 0.56f, 1.00f, 1.0f}},
  {{-0.26f, -0.20f, 0.76f, 1.0f}, {0.72f, 0.16f, 0.96f, 1.0f}},
  {{ 0.54f, -0.20f, 0.76f, 1.0f}, {1.00f, 0.24f, 0.60f, 1.0f}},
  {{ 0.54f,  0.60f, 0.76f, 1.0f}, {0.18f, 0.86f, 0.96f, 1.0f}},
  {{-0.26f,  0.60f, 0.76f, 1.0f}, {0.36f, 0.34f, 1.00f, 1.0f}}
};

static const uint16_t kCubeIndices[] = {
  0u, 1u, 2u, 0u, 2u, 3u,
  1u, 5u, 6u, 1u, 6u, 2u,
  3u, 2u, 6u, 3u, 6u, 7u,
  0u, 3u, 7u, 0u, 7u, 4u,
  0u, 4u, 5u, 0u, 5u, 1u,
  4u, 7u, 6u, 4u, 6u, 5u
};

static WebGPUIndexedDepth app;

static int
create_depth_target(WebGPUIndexedDepth *state,
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
  textureInfo.label            = "indexed-depth-webgpu-depth";
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
  viewInfo.label            = "indexed-depth-webgpu-depth-view";
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
resize_canvas(WebGPUIndexedDepth *state) {
  uint32_t oldWidth;
  uint32_t oldHeight;

  oldWidth  = state->width;
  oldHeight = state->height;
  if (!resize_webgpu_canvas(state->swapchain,
                            &state->width,
                            &state->height)) {
    return 0;
  }
  if (state->swapchain &&
      (oldWidth != state->width || oldHeight != state->height) &&
      !create_depth_target(state, state->width, state->height)) {
    state->width  = 0u;
    state->height = 0u;
    return 0;
  }
  return 1;
}

static int
create_pipeline(WebGPUIndexedDepth *state) {
  GPUVertexAttribute          attributes[2] = {0};
  GPUVertexBufferLayout       vertexLayout  = {0};
  GPUColorTargetState         color         = {0};
  GPUDepthStencilState        depth         = {0};
  GPURenderPipelineCreateInfo info          = {0};
  void                       *artifact;
  uint64_t                    artifactSize;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/indexed_depth.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /indexed_depth.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the indexed-depth artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 0u) {
    set_status("GPU: unexpected indexed-depth shader reflection", 1);
    return 0;
  }

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[0].offset          = offsetof(CubeVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[1].offset          = offsetof(CubeVertex, color);
  attributes[1].shaderLocation = 1u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(CubeVertex);
  vertexLayout.attributeCount   = 2u;
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "indexed-depth-webgpu-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "cube_vs";
  info.fragmentEntry            = "cube_fs";
  info.pColorTargets            = &color;
  info.pDepthStencilState       = &depth;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount        = 1u;
  info.depthStencilFormat      = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create indexed-depth pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_geometry(WebGPUIndexedDepth *state) {
  GPUBufferCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "indexed-depth-webgpu-vertices";
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

  info.label     = "indexed-depth-webgpu-indices";
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
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUIndexedDepth                 *state;
  GPUFrame                           *frame;
  GPUCommandBuffer                   *cmdb;
  GPURenderPassEncoder               *pass;
  GPUBufferBinding                    vertexBuffer = {0};
  GPURenderPassColorAttachment        color        = {0};
  GPURenderPassDepthStencilAttachment depth = {0};
  GPURenderPassCreateInfo             passInfo     = {0};

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
                              "indexed-depth-webgpu-frame",
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
  depth.view                  = state->depthView;
  depth.depthLoadOp           = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp          = GPU_STORE_OP_DONT_CARE;
  depth.stencilLoadOp         = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp        = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth            = 1.0f;
  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "indexed-depth-webgpu-pass";
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
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindIndexBuffer(pass, state->indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexed(pass,
                 (uint32_t)(sizeof(kCubeIndices) / sizeof(kCubeIndices[0])),
                 1u,
                 0u,
                 0,
                 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish indexed-depth frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.hotPathAllocCount != 0u || stats.hotPathFreeCount != 0u)) {
      set_status("GPU: warm indexed-depth frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  WebGPUIndexedDepth *state;
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
  if (!state->swapchain ||
      !create_depth_target(state, state->width, state->height) ||
      !create_pipeline(state) ||
      !create_geometry(state)) {
    set_status("GPU: failed to initialize indexed-depth resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL indexed depth ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  WebGPUIndexedDepth *state;

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
  info.label            = "indexed-depth-webgpu-usl";
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
