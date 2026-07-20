#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <gpu/gpu.h>

#include "../common/webgpu.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct WebGPUTriangle {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  uint32_t           width;
  uint32_t           height;
} WebGPUTriangle;

static WebGPUTriangle app;

static int
resize_canvas(WebGPUTriangle *state) {
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
create_pipeline(WebGPUTriangle *state) {
  GPUColorTargetState         color = {0};
  GPURenderPipelineCreateInfo info = {0};
  void                       *artifact;
  uint64_t                    artifactSize;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/triangle.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /triangle.us", 1);
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
      state->shaderLayout->bindGroupLayoutCount != 0u) {
    set_status("GPU: unexpected WebGPU shader reflection", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize     = sizeof(info);
  info.label                = "triangle-webgpu-usl-pipeline";
  info.layout               = state->shaderLayout->pipelineLayout;
  info.library              = state->library;
  info.vertexEntry          = "tri_vs";
  info.fragmentEntry        = "tri_fs";
  info.pColorTargets        = &color;
  info.colorTargetCount     = 1u;
  info.primitiveTopology    = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode             = GPU_CULL_MODE_NONE;
  info.frontFace            = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create WebGPU pipeline", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUTriangle                 *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
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
                              "triangle-webgpu-frame",
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

  passInfo.label                = "triangle-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU frame\n");
  }
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  WebGPUTriangle *state;

  state = userData;
  if (result != GPU_OK || !device) {
    set_status("GPU: failed to request WebGPU device", 1);
    return;
  }

  state->device = device;
  state->queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
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
  if (!create_pipeline(state)) {
    return;
  }

  set_status("GPU: WebGPU USL triangle ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  WebGPUTriangle *state;

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
  info.label            = "triangle-webgpu-usl";
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
    fprintf(stderr, "GPU: failed to start WebGPU adapter request (%d)\n", result);
    return 1;
  }
  return 0;
}
