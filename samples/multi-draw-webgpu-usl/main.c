#include "../common/webgpu.h"

#include <stdio.h>

typedef struct WebGPUMultiDraw {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *indirectBuffer;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  bool               nativeMultiDraw;
} WebGPUMultiDraw;

static const uint32_t kDraws[] = {
  3u, 1u, 0u, 0u,
  3u, 1u, 3u, 0u
};

static WebGPUMultiDraw app;

static int
resize_canvas(WebGPUMultiDraw *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
create_resources(WebGPUMultiDraw *state) {
  GPUColorTargetState          color        = {0};
  GPURenderPipelineCreateInfo  pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo   = {0};
  void                        *artifact;
  uint64_t                     artifactSize;
  GPUResult                    result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/multi_draw.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /multi_draw.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile multi-draw USL", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 0u) {
    set_status("GPU: unexpected multi-draw shader reflection", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  pipelineInfo.chain.sType             =
    GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize        = sizeof(pipelineInfo);
  pipelineInfo.label                   = "multi-draw-webgpu-usl-pipeline";
  pipelineInfo.layout                  = state->shaderLayout->pipelineLayout;
  pipelineInfo.library                 = state->library;
  pipelineInfo.vertexEntry             = "multi_vs";
  pipelineInfo.fragmentEntry           = "multi_fs";
  pipelineInfo.pColorTargets           = &color;
  pipelineInfo.colorTargetCount        = 1u;
  pipelineInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode                = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace               = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device,
                                   &pipelineInfo,
                                   &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create multi-draw pipeline", 1);
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "multi-draw-webgpu-indirect";
  bufferInfo.sizeBytes        = sizeof(kDraws);
  bufferInfo.usage             =
    GPU_BUFFER_USAGE_INDIRECT | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->indirectBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->indirectBuffer,
                          0u,
                          kDraws,
                          sizeof(kDraws)) != GPU_OK) {
    set_status("GPU: failed to upload multi-draw commands", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUMultiDraw              *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};

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
                              "multi-draw-webgpu-frame",
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

  passInfo.label                = "multi-draw-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUMultiDrawIndirect(pass, state->indirectBuffer, 0u, 2u, 16u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU multi-draw frame\n");
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUMultiDraw *state;

  state = userData;
  if (result != GPU_OK || !adapter || !device) {
    set_status(!adapter ? "GPU: failed to request WebGPU adapter"
                        : "GPU: failed to request WebGPU device",
               1);
    return;
  }

  state->adapter = adapter;
  state->device  = device;
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW)) {
    set_status("GPU: WebGPU indirect draw unsupported by this adapter", 1);
    return;
  }
  state->nativeMultiDraw = GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW);
  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
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
  if (!state->swapchain || !create_resources(state)) {
    return;
  }

  set_status(state->nativeMultiDraw
               ? "GPU: WebGPU USL native multi-draw ready"
               : "GPU: WebGPU USL multi-draw fallback ready",
             0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "multi-draw-webgpu-usl";
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
