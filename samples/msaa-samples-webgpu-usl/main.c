#include "../common/webgpu.h"

#include <stdio.h>

enum {
  SAMPLE_COUNT     = 4u,
  WARM_FRAME_COUNT = 8u
};

typedef struct WebGPUMSAASamples {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *sourcePipeline;
  GPURenderPipeline *previewPipeline;
  GPUTexture        *colorTexture;
  GPUTextureView    *colorView;
  GPUBindGroup      *previewGroup;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
  bool               failed;
} WebGPUMSAASamples;

static WebGPUMSAASamples app;

static int
create_group(WebGPUMSAASamples *state);

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUMSAASamples *state;

  (void)device;
  state = userData;
  if (!state || !error || state->failed) {
    return;
  }
  state->failed = true;
  set_status(error->message ? error->message : "GPU: unknown device error", 1);
  emscripten_cancel_main_loop();
}

static int
create_color_target(WebGPUMSAASamples *state,
                    uint32_t           width,
                    uint32_t           height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;

  texture = NULL;
  view    = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "msaa-samples-webgpu-color";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = SAMPLE_COUNT;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_SAMPLED;
  if (GPUCreateTexture(state->device, &textureInfo, &texture) != GPU_OK) {
    set_status("GPU: failed to create the sampled 4x color texture", 1);
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "msaa-samples-webgpu-color-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = textureInfo.format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK) {
    GPUDestroyTexture(texture);
    set_status("GPU: failed to create the sampled 4x color view", 1);
    return 0;
  }

  GPUDestroyBindGroup(state->previewGroup);
  GPUDestroyTextureView(state->colorView);
  GPUDestroyTexture(state->colorTexture);
  state->previewGroup = NULL;
  state->colorTexture = texture;
  state->colorView    = view;
  return 1;
}

static int
resize_canvas(WebGPUMSAASamples *state) {
  uint32_t oldWidth;
  uint32_t oldHeight;

  oldWidth  = state->width;
  oldHeight = state->height;
  if (!resize_webgpu_canvas(state->swapchain,
                            &state->width,
                            &state->height)) {
    return 0;
  }
  if (oldWidth == state->width && oldHeight == state->height) {
    return 1;
  }
  if (state->swapchain) {
    if (!create_color_target(state, state->width, state->height) ||
        (state->shaderLayout && !create_group(state))) {
      state->width  = 0u;
      state->height = 0u;
      return 0;
    }
  }
  return 1;
}

static int
create_shader(WebGPUMSAASamples *state) {
  const GPUBindGroupLayoutEntry *entries;
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       entryCount;
  GPUResult                      result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/msaa_samples.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /msaa_samples.us", 1);
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
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected MSAA sample reflection", 1);
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 1u || entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      entries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      entries[0].sampledTexture.sampleType !=
        GPU_TEXTURE_SAMPLE_TYPE_UNFILTERABLE_FLOAT ||
      !entries[0].sampledTexture.multisampled) {
    set_status("GPU: missing reflected multisampled texture binding", 1);
    return 0;
  }
  return 1;
}

static int
create_pipelines(WebGPUMSAASamples *state) {
  GPURenderPipelineCreateInfo sourceInfo  = {0};
  GPURenderPipelineCreateInfo previewInfo = {0};
  GPUColorTargetState         sourceColor  = {0};
  GPUColorTargetState         previewColor = {0};

  sourceColor.format          = GPU_FORMAT_RGBA8_UNORM;
  sourceColor.blend.writeMask = GPU_COLOR_WRITE_ALL;
  previewColor.format          = GPUGetSwapchainFormat(state->swapchain);
  previewColor.blend.writeMask = GPU_COLOR_WRITE_ALL;

  sourceInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  sourceInfo.chain.structSize        = sizeof(sourceInfo);
  sourceInfo.label                   = "msaa-samples-webgpu-source";
  sourceInfo.layout                  = state->shaderLayout->pipelineLayout;
  sourceInfo.library                 = state->library;
  sourceInfo.vertexEntry             = "sample_source_vs";
  sourceInfo.fragmentEntry           = "sample_source_fs";
  sourceInfo.pColorTargets           = &sourceColor;
  sourceInfo.colorTargetCount        = 1u;
  sourceInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  sourceInfo.cullMode                = GPU_CULL_MODE_NONE;
  sourceInfo.frontFace               = GPU_FRONT_FACE_CCW;
  sourceInfo.multisample.sampleCount = SAMPLE_COUNT;
  sourceInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &sourceInfo,
                              &state->sourcePipeline) != GPU_OK ||
      !state->sourcePipeline) {
    set_status("GPU: failed to create the multisampled source pipeline", 1);
    return 0;
  }

  previewInfo                           = sourceInfo;
  previewInfo.label                     = "msaa-samples-webgpu-preview";
  previewInfo.vertexEntry               = "sample_preview_vs";
  previewInfo.fragmentEntry             = "sample_preview_fs";
  previewInfo.pColorTargets             = &previewColor;
  previewInfo.multisample.sampleCount   = 1u;
  if (GPUCreateRenderPipeline(state->device,
                              &previewInfo,
                              &state->previewPipeline) != GPU_OK ||
      !state->previewPipeline) {
    set_status("GPU: failed to create the MSAA sample preview pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_group(WebGPUMSAASamples *state) {
  GPUBindGroupEntry      entry = {0};
  GPUBindGroupCreateInfo info  = {0};
  GPUBindGroup          *group;

  group             = NULL;
  entry.textureView = state->colorView;
  entry.binding     = 0u;
  entry.bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  info.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "msaa-samples-webgpu-group";
  info.layout           = state->shaderLayout->bindGroupLayouts[0];
  info.pEntries         = &entry;
  info.entryCount       = 1u;
  if (GPUCreateBindGroup(state->device,
                         &info,
                         &group) != GPU_OK ||
      !group) {
    set_status("GPU: failed to create the MSAA sample bind group", 1);
    return 0;
  }
  state->previewGroup = group;
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUMSAASamples            *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPUTextureBarrier             textureBarrier = {0};
  GPUBarrierBatch               barrier        = {0};
  GPURenderPassColorAttachment  color          = {0};
  GPURenderPassCreateInfo       passInfo       = {0};

  state = userData;
  if (!resize_canvas(state)) return;

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) return;

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "msaa-samples-webgpu-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = state->colorView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.012f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.065f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "msaa-samples-webgpu-source-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindRenderPipeline(pass, state->sourcePipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  textureBarrier.texture    = state->colorTexture;
  textureBarrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrier.pTextureBarriers    = &textureBarrier;
  barrier.srcStages           = GPU_STAGE_FRAGMENT;
  barrier.dstStages           = GPU_STAGE_FRAGMENT;
  barrier.textureBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &barrier);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.006f;
  color.clearColor.float32[1] = 0.012f;
  color.clearColor.float32[2] = 0.034f;
  passInfo.label              = "msaa-samples-webgpu-preview-pass";
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindRenderPipeline(pass, state->previewPipeline);
  GPUBindRenderGroup(pass, 0u, state->previewGroup, 0u, NULL);
  GPUDraw(pass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the per-sample MSAA frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 2u || stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: invalid warm MSAA sample frame stats", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUMSAASamples *state;
  GPURuntimeConfig   runtime = {0};

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
  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (!state->queue || GPUConfigureRuntime(device, &runtime) != GPU_OK) {
    set_status("GPU: failed to configure the WebGPU runtime", 1);
    return;
  }
  if (GPUSetDeviceErrorCallback(device, device_error, state) != GPU_OK) {
    set_status("GPU: failed to install the WebGPU error callback", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               state->adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create the WebGPU canvas surface", 1);
    return;
  }
  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain) {
    set_status("GPU: failed to create the WebGPU swapchain", 1);
    return;
  }
  if (!create_color_target(state, state->width, state->height) ||
      !create_shader(state) ||
      !create_pipelines(state) ||
      !create_group(state)) {
    return;
  }

  set_status("GPU: WebGPU USL per-sample MSAA read ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "msaa-samples-webgpu-usl";
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  result = GPUCreateInstance(&info, &app.instance);
  if (result != GPU_OK || !app.instance) {
    set_status("GPU: failed to create the WebGPU instance", 1);
    return 1;
  }

  set_status("GPU: requesting WebGPU device", 0);
  result = request_webgpu_device(app.instance,
                                 &app.request,
                                 webgpu_ready,
                                 &app);
  return result == GPU_OK ? 0 : 1;
}
