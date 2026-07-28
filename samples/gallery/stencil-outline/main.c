#include "../../common/sample_platform.h"

#include <stdio.h>

enum {
  WARM_FRAME_COUNT = 8u
};

typedef struct WebGPUStencilOutline {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *fillPipeline;
  GPURenderPipeline *outlinePipeline;
  GPUTexture        *depthStencilTexture;
  GPUTextureView    *depthStencilView;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUStencilOutline;

static WebGPUStencilOutline app;

static int
create_depth_stencil_target(WebGPUStencilOutline *state,
                            uint32_t               width,
                            uint32_t               height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;

  texture = NULL;
  view    = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "webgpu-stencil-outline-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH24_UNORM_STENCIL8;
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
  viewInfo.label            = "webgpu-stencil-outline-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_DEPTH24_UNORM_STENCIL8;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK) {
    GPUDestroyTexture(texture);
    return 0;
  }

  GPUDestroyTextureView(state->depthStencilView);
  GPUDestroyTexture(state->depthStencilTexture);
  state->depthStencilTexture = texture;
  state->depthStencilView    = view;
  return 1;
}

static int
resize_canvas(WebGPUStencilOutline *state) {
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
      (state->width != oldWidth || state->height != oldHeight) &&
      !create_depth_stencil_target(state, state->width, state->height)) {
    return 0;
  }
  return 1;
}

static void
set_stencil_face(GPUStencilFaceState *face,
                 GPUCompareOp         compare,
                 GPUStencilOp         passOp) {
  face->compare     = compare;
  face->failOp      = GPU_STENCIL_OP_KEEP;
  face->depthFailOp = GPU_STENCIL_OP_KEEP;
  face->passOp      = passOp;
}

static int
create_pipelines(WebGPUStencilOutline *state) {
  GPUColorTargetState         color   = {0};
  GPUDepthStencilState        stencil = {0};
  GPURenderPipelineCreateInfo info    = {0};
  void                       *artifact;
  uint64_t                    artifactSize;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/stencil_outline.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read stencil_outline.us", 1);
    return 0;
  }
  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library ||
      GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 0u) {
    set_status("GPU: failed to create stencil shader layout", 1);
    return 0;
  }

  color.format                 = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask        = GPU_COLOR_WRITE_ALL;
  stencil.stencilReadMask      = UINT8_MAX;
  stencil.stencilWriteMask     = UINT8_MAX;
  stencil.stencilTestEnable    = true;
  set_stencil_face(&stencil.front,
                   GPU_COMPARE_ALWAYS,
                   GPU_STENCIL_OP_REPLACE);
  stencil.back = stencil.front;

  info.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize   = sizeof(info);
  info.label              = "webgpu-stencil-fill";
  info.layout             = state->shaderLayout->pipelineLayout;
  info.library            = state->library;
  info.vertexEntry        = "fill_vs";
  info.fragmentEntry      = "solid_fs";
  info.pColorTargets      = &color;
  info.pDepthStencilState = &stencil;
  info.colorTargetCount   = 1u;
  info.depthStencilFormat = GPU_FORMAT_DEPTH24_UNORM_STENCIL8;

  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->fillPipeline) != GPU_OK) {
    set_status("GPU: failed to create stencil fill pipeline", 1);
    return 0;
  }

  stencil.stencilWriteMask = 0u;
  set_stencil_face(&stencil.front,
                   GPU_COMPARE_NOT_EQUAL,
                   GPU_STENCIL_OP_KEEP);
  stencil.back     = stencil.front;
  info.label       = "webgpu-stencil-outline";
  info.vertexEntry = "outline_vs";
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->outlinePipeline) != GPU_OK) {
    set_status("GPU: failed to create stencil outline pipeline", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUStencilOutline                *state;
  GPUFrame                            *frame;
  GPUCommandBuffer                    *cmdb;
  GPURenderPassEncoder                *pass;
  GPURenderPassColorAttachment         color = {0};
  GPURenderPassDepthStencilAttachment  depthStencil = {0};
  GPURenderPassCreateInfo              passInfo = {0};

  state = userData;
  if (!resize_canvas(state)) {
    return;
  }
  frame = GPUBeginFrame(state->swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(state->queue,
                              "webgpu-stencil-outline-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.014f;
  color.clearColor.float32[2] = 0.034f;
  color.clearColor.float32[3] = 1.0f;
  depthStencil.view           = state->depthStencilView;
  depthStencil.depthLoadOp    = GPU_LOAD_OP_DONT_CARE;
  depthStencil.depthStoreOp   = GPU_STORE_OP_DONT_CARE;
  depthStencil.stencilLoadOp  = GPU_LOAD_OP_CLEAR;
  depthStencil.stencilStoreOp = GPU_STORE_OP_STORE;
  depthStencil.clearDepth     = 1.0f;
  depthStencil.clearStencil   = 0u;
  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "webgpu-stencil-outline-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depthStencil;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  GPUSetStencilReference(pass, 1u);
  GPUBindRenderPipeline(pass, state->fillPipeline);
  GPUDraw(pass, 4u, 1u, 0u, 0u);
  GPUBindRenderPipeline(pass, state->outlinePipeline);
  GPUDraw(pass, 4u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish stencil frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 2u ||
         stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: stencil warm path regression", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUStencilOutline *state;
  GPURuntimeConfig      runtime = {0};

  state = userData;
  if (result != GPU_OK || !adapter || !device) {
    set_status("GPU: failed to request WebGPU device", 1);
    return;
  }
  state->adapter = adapter;
  state->device  = device;
  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (!state->queue || GPUConfigureRuntime(device, &runtime) != GPU_OK) {
    set_status("GPU: failed to configure stencil runtime", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create stencil canvas surface", 1);
    return;
  }
  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain ||
      !create_depth_stencil_target(state, state->width, state->height) ||
      !create_pipelines(state)) {
    return;
  }

  GPUResetStats(device);
  set_status("GPU: WebGPU USL stencil outline ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "stencil-outline-webgpu-usl";
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  result = GPUCreateInstance(&info, &app.instance);
  if (result != GPU_OK || !app.instance) {
    set_status("GPU: failed to create WebGPU instance", 1);
    return 1;
  }

  set_status("GPU: requesting WebGPU device", 0);
  result = request_webgpu_device(app.instance,
                                 &app.request,
                                 webgpu_ready,
                                 &app);
  return result == GPU_OK ? 0 : 1;
}
