#include "../common/webgpu.h"

#include <math.h>
#include <stdio.h>

typedef struct DrawConstants {
  float tint[4];
} DrawConstants;

typedef struct WebGPUPushConstants {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
} WebGPUPushConstants;

static WebGPUPushConstants app;

static int
resize_canvas(WebGPUPushConstants *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
create_pipeline(WebGPUPushConstants *state) {
  GPUColorTargetState         color = {0};
  GPURenderPipelineCreateInfo info = {0};
  GPUShaderReflection         reflection = {0};
  void                       *artifact;
  uint64_t                    artifactSize;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/push_constants.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /push_constants.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library ||
      GPUGetShaderReflection(state->library, &reflection) != GPU_OK ||
      reflection.pushConstantSizeBytes != sizeof(DrawConstants) ||
      reflection.pushConstantStages != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    GPUFreeShaderReflection(&reflection);
    set_status("GPU: unexpected push-constant reflection", 1);
    return 0;
  }
  GPUFreeShaderReflection(&reflection);

  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 0u) {
    set_status("GPU: failed to create push-constant shader layout", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize     = sizeof(info);
  info.label                = "push-constants-webgpu-usl-pipeline";
  info.layout               = state->shaderLayout->pipelineLayout;
  info.library              = state->library;
  info.vertexEntry          = "push_vs";
  info.fragmentEntry        = "push_fs";
  info.pColorTargets        = &color;
  info.colorTargetCount     = 1u;
  info.primitiveTopology    = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode             = GPU_CULL_MODE_NONE;
  info.frontFace            = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create push-constant pipeline", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUPushConstants           *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPURenderPassColorAttachment   color = {0};
  GPURenderPassCreateInfo        passInfo = {0};
  DrawConstants                 draw;
  float                         phase;

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
                              "push-constants-webgpu-frame",
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
  passInfo.label                = "push-constants-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  phase        = (float)(emscripten_get_now() * 0.001);
  draw.tint[0] = 0.55f + 0.45f * sinf(phase);
  draw.tint[1] = 0.55f + 0.45f * sinf(phase + 2.0943951f);
  draw.tint[2] = 0.55f + 0.45f * sinf(phase + 4.1887902f);
  draw.tint[3] = 1.0f;

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUSetRenderPushConstants(pass, 0u, (uint32_t)sizeof(draw), &draw);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU frame\n");
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUPushConstants *state;

  state = userData;
  if (result != GPU_OK || !adapter || !device) {
    set_status(!adapter ? "GPU: failed to request WebGPU adapter"
                        : "GPU: failed to request WebGPU device",
               1);
    return;
  }

  state->adapter = adapter;
  state->device  = device;
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
  if (!state->swapchain || !create_pipeline(state)) {
    return;
  }

  set_status("GPU: WebGPU USL push constants ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "push-constants-webgpu-usl";
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
