#include "../common/webgpu.h"

#include <stdio.h>

typedef struct WebGPUMRTBlend {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPURenderPipeline  *mrtPipeline;
  GPURenderPipeline  *compositePipeline;
  GPUTexture         *targets[2];
  GPUTextureView     *targetViews[2];
  GPUSampler         *sampler;
  GPUBindGroup       *compositeGroups[2];
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
  bool                failed;
} WebGPUMRTBlend;

enum {
  WARM_FRAME_COUNT = 8u
};

static WebGPUMRTBlend app;

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUMRTBlend *state;

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
create_shader(WebGPUMRTBlend *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/mrt_blend.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /mrt_blend.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the MRT artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected MRT shader reflection", 1);
    return 0;
  }
  return 1;
}

static void
set_alpha_blend(GPUBlendState *blend) {
  blend->color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
  blend->color.dstFactor = GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend->color.op        = GPU_BLEND_OP_ADD;
  blend->alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
  blend->alpha.dstFactor = GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend->alpha.op        = GPU_BLEND_OP_ADD;
  blend->writeMask       = GPU_COLOR_WRITE_ALL;
  blend->enabled         = true;
}

static void
set_additive_blend(GPUBlendState *blend) {
  blend->color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
  blend->color.dstFactor = GPU_BLEND_FACTOR_ONE;
  blend->color.op        = GPU_BLEND_OP_ADD;
  blend->alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
  blend->alpha.dstFactor = GPU_BLEND_FACTOR_ONE;
  blend->alpha.op        = GPU_BLEND_OP_ADD;
  blend->writeMask       = GPU_COLOR_WRITE_ALL;
  blend->enabled         = true;
}

static int
create_pipelines(WebGPUMRTBlend *state) {
  GPUColorTargetState         mrtTargets[2]   = {0};
  GPUColorTargetState         compositeTarget = {0};
  GPURenderPipelineCreateInfo mrtInfo          = {0};
  GPURenderPipelineCreateInfo compositeInfo    = {0};

  mrtTargets[0].format = GPU_FORMAT_RGBA8_UNORM;
  mrtTargets[1].format = GPU_FORMAT_RGBA8_UNORM;
  set_alpha_blend(&mrtTargets[0].blend);
  set_additive_blend(&mrtTargets[1].blend);

  mrtInfo.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  mrtInfo.chain.structSize         = sizeof(mrtInfo);
  mrtInfo.label                    = "mrt-blend-webgpu-mrt-pipeline";
  mrtInfo.layout                   = state->shaderLayout->pipelineLayout;
  mrtInfo.library                  = state->library;
  mrtInfo.vertexEntry              = "fullscreen_vs";
  mrtInfo.fragmentEntry            = "mrt_fs";
  mrtInfo.pColorTargets            = mrtTargets;
  mrtInfo.colorTargetCount         = 2u;
  mrtInfo.depthStencilFormat       = GPU_FORMAT_UNDEFINED;
  mrtInfo.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  mrtInfo.cullMode                 = GPU_CULL_MODE_NONE;
  mrtInfo.frontFace                = GPU_FRONT_FACE_CCW;
  mrtInfo.multisample.sampleCount  = 1u;
  mrtInfo.multisample.sampleMask   = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &mrtInfo,
                              &state->mrtPipeline) != GPU_OK ||
      !state->mrtPipeline) {
    set_status("GPU: failed to create the MRT pipeline", 1);
    return 0;
  }

  compositeTarget.format          = GPUGetSwapchainFormat(state->swapchain);
  compositeTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  compositeInfo.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  compositeInfo.chain.structSize         = sizeof(compositeInfo);
  compositeInfo.label                    = "mrt-blend-webgpu-composite-pipeline";
  compositeInfo.layout                   = state->shaderLayout->pipelineLayout;
  compositeInfo.library                  = state->library;
  compositeInfo.vertexEntry              = "fullscreen_vs";
  compositeInfo.fragmentEntry            = "composite_fs";
  compositeInfo.pColorTargets            = &compositeTarget;
  compositeInfo.colorTargetCount         = 1u;
  compositeInfo.depthStencilFormat       = GPU_FORMAT_UNDEFINED;
  compositeInfo.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  compositeInfo.cullMode                 = GPU_CULL_MODE_NONE;
  compositeInfo.frontFace                = GPU_FRONT_FACE_CCW;
  compositeInfo.multisample.sampleCount = 1u;
  compositeInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &compositeInfo,
                              &state->compositePipeline) != GPU_OK ||
      !state->compositePipeline) {
    set_status("GPU: failed to create the MRT composite pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_sampler(WebGPUMRTBlend *state) {
  GPUSamplerCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "mrt-blend-webgpu-linear-sampler";
  info.desc.minFilter   = GPU_FILTER_LINEAR;
  info.desc.magFilter   = GPU_FILTER_LINEAR;
  info.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  info.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  info.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  return GPUCreateSampler(state->device,
                          &info,
                          false,
                          &state->sampler) == GPU_OK;
}

static int
create_targets(WebGPUMRTBlend *state) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUBindGroupEntry        entries[2]  = {0};
  GPUBindGroupCreateInfo   groupInfo   = {0};
  GPUTexture              *targets[2]  = {0};
  GPUTextureView          *views[2]    = {0};
  GPUBindGroup            *groups[2]   = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = state->width;
  textureInfo.height           = state->height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_SAMPLED;
  viewInfo.chain.sType         = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize    = sizeof(viewInfo);
  viewInfo.viewType            = GPU_TEXTURE_VIEW_2D;
  viewInfo.format              = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount       = 1u;
  viewInfo.arrayLayerCount     = 1u;

  for (uint32_t i = 0u; i < 2u; i++) {
    textureInfo.label = i == 0u ? "mrt-blend-webgpu-alpha-target"
                                : "mrt-blend-webgpu-additive-target";
    viewInfo.label    = i == 0u ? "mrt-blend-webgpu-alpha-view"
                                : "mrt-blend-webgpu-additive-view";
    if (GPUCreateTexture(state->device, &textureInfo, &targets[i]) != GPU_OK ||
        GPUCreateTextureView(targets[i], &viewInfo, &views[i]) != GPU_OK) {
      for (uint32_t j = 0u; j < 2u; j++) {
        GPUDestroyTextureView(views[j]);
        GPUDestroyTexture(targets[j]);
      }
      return 0;
    }
  }

  entries[0].binding     = 0u;
  entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entries[1].sampler     = state->sampler;
  entries[1].binding     = 1u;
  entries[1].bindingType = GPU_BINDING_SAMPLER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  for (uint32_t i = 0u; i < 2u; i++) {
    entries[0].textureView = views[i];
    groupInfo.label          = i == 0u ? "mrt-blend-webgpu-alpha-group"
                                       : "mrt-blend-webgpu-additive-group";
    if (GPUCreateBindGroup(state->device, &groupInfo, &groups[i]) != GPU_OK) {
      for (uint32_t j = 0u; j < 2u; j++) {
        GPUDestroyBindGroup(groups[j]);
        GPUDestroyTextureView(views[j]);
        GPUDestroyTexture(targets[j]);
      }
      return 0;
    }
  }

  for (uint32_t i = 0u; i < 2u; i++) {
    GPUDestroyBindGroup(state->compositeGroups[i]);
    GPUDestroyTextureView(state->targetViews[i]);
    GPUDestroyTexture(state->targets[i]);
    state->targets[i]         = targets[i];
    state->targetViews[i]     = views[i];
    state->compositeGroups[i] = groups[i];
  }
  state->frameCount = 0u;
  return 1;
}

static int
resize_canvas(WebGPUMRTBlend *state) {
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
      !create_targets(state)) {
    state->width  = 0u;
    state->height = 0u;
    return 0;
  }
  return 1;
}

static void
render_composite(GPURenderPassEncoder *pass, WebGPUMRTBlend *state) {
  GPUViewport    viewport = {0};
  GPUScissorRect scissor  = {0};
  uint32_t       leftWidth;

  leftWidth         = state->width / 2u;
  viewport.y        = 0.0f;
  viewport.width    = (float)leftWidth;
  viewport.height   = (float)state->height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  scissor.x         = 0;
  scissor.y         = 0;
  scissor.width     = leftWidth;
  scissor.height    = state->height;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderGroup(pass, 0u, state->compositeGroups[0], 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);

  viewport.x      = (float)leftWidth;
  viewport.width  = (float)(state->width - leftWidth);
  scissor.x       = (int32_t)leftWidth;
  scissor.width   = state->width - leftWidth;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderGroup(pass, 0u, state->compositeGroups[1], 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
}

static void
render_frame(void *userData) {
  WebGPUMRTBlend               *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  mrtColors[2]     = {0};
  GPURenderPassColorAttachment  compositeColor   = {0};
  GPURenderPassCreateInfo       mrtPassInfo       = {0};
  GPURenderPassCreateInfo       compositePassInfo = {0};

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
                              "mrt-blend-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  mrtColors[0].view                  = state->targetViews[0];
  mrtColors[0].loadOp                = GPU_LOAD_OP_CLEAR;
  mrtColors[0].storeOp               = GPU_STORE_OP_STORE;
  mrtColors[0].clearColor.float32[0] = 0.055f;
  mrtColors[0].clearColor.float32[1] = 0.012f;
  mrtColors[0].clearColor.float32[2] = 0.008f;
  mrtColors[0].clearColor.float32[3] = 1.0f;
  mrtColors[1].view                  = state->targetViews[1];
  mrtColors[1].loadOp                = GPU_LOAD_OP_CLEAR;
  mrtColors[1].storeOp               = GPU_STORE_OP_STORE;
  mrtColors[1].clearColor.float32[0] = 0.004f;
  mrtColors[1].clearColor.float32[1] = 0.018f;
  mrtColors[1].clearColor.float32[2] = 0.060f;
  mrtColors[1].clearColor.float32[3] = 1.0f;
  mrtPassInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  mrtPassInfo.chain.structSize     = sizeof(mrtPassInfo);
  mrtPassInfo.label                = "mrt-blend-webgpu-offscreen-pass";
  mrtPassInfo.pColorAttachments    = mrtColors;
  mrtPassInfo.colorAttachmentCount = 2u;
  pass = GPUBeginRenderPass(cmdb, &mrtPassInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindRenderPipeline(pass, state->mrtPipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  compositeColor.view                  = GPUFrameGetTargetView(frame);
  compositeColor.loadOp                = GPU_LOAD_OP_CLEAR;
  compositeColor.storeOp               = GPU_STORE_OP_STORE;
  compositeColor.clearColor.float32[0] = 0.003f;
  compositeColor.clearColor.float32[1] = 0.008f;
  compositeColor.clearColor.float32[2] = 0.020f;
  compositeColor.clearColor.float32[3] = 1.0f;
  compositePassInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  compositePassInfo.chain.structSize     = sizeof(compositePassInfo);
  compositePassInfo.label                = "mrt-blend-webgpu-composite-pass";
  compositePassInfo.pColorAttachments    = &compositeColor;
  compositePassInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &compositePassInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindRenderPipeline(pass, state->compositePipeline);
  render_composite(pass, state);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the MRT frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK) {
      if (stats.drawCalls != 3u) {
        set_status("GPU: MRT frame did not encode all draws", 1);
        emscripten_cancel_main_loop();
      } else if (stats.hotPathAllocCount != 0u ||
                 stats.hotPathFreeCount != 0u) {
        set_status("GPU: warm MRT frame allocated wrapper memory", 1);
        emscripten_cancel_main_loop();
      }
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUMRTBlend   *state;
  GPURuntimeConfig runtime = {0};

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
  if (GPUConfigureRuntime(device, &runtime) != GPU_OK) {
    set_status("GPU: failed to configure WebGPU runtime stats", 1);
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
  if (!state->queue || !state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create the WebGPU surface", 1);
    return;
  }
  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain ||
      !create_shader(state) ||
      !create_pipelines(state) ||
      !create_sampler(state) ||
      !create_targets(state)) {
    set_status("GPU: failed to initialize MRT resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL MRT blend ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "mrt-blend-webgpu-usl";
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
