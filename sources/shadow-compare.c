#include "../common/webgpu.h"

enum {
  WARM_FRAME_COUNT = 8u
};

typedef struct WebGPUShadowCompare {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *depthPipeline;
  GPURenderPipeline *previewPipeline;
  GPURenderPipeline *previewCoolPipeline;
  GPUTexture        *depthTexture;
  GPUTextureView    *depthView;
  GPUBindGroup      *shadowGroup;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUShadowCompare;

static WebGPUShadowCompare app;

static int
create_shadow_group(WebGPUShadowCompare *state,
                    GPUTextureView      *view,
                    GPUBindGroup       **outGroup) {
  GPUBindGroupEntry      entry = {0};
  GPUBindGroupCreateInfo info       = {0};

  if (!state || !view || !outGroup || !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    return 0;
  }

  entry.textureView      = view;
  entry.binding          = 0u;
  entry.bindingType      = GPU_BINDING_SAMPLED_TEXTURE;
  info.chain.sType       = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize  = sizeof(info);
  info.label             = "shadow-compare-webgpu-group";
  info.layout            = state->shaderLayout->bindGroupLayouts[0];
  info.pEntries          = &entry;
  info.entryCount        = 1u;
  return GPUCreateBindGroup(state->device, &info, outGroup) == GPU_OK &&
         *outGroup;
}

static int
create_depth_target(WebGPUShadowCompare *state,
                    uint32_t             width,
                    uint32_t             height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;
  GPUBindGroup            *group;

  texture = NULL;
  view    = NULL;
  group   = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "shadow-compare-webgpu-depth";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL |
                                 GPU_TEXTURE_USAGE_SAMPLED;
  if (GPUCreateTexture(state->device, &textureInfo, &texture) != GPU_OK) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "shadow-compare-webgpu-depth-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK ||
      !create_shadow_group(state, view, &group)) {
    GPUDestroyTextureView(view);
    GPUDestroyTexture(texture);
    return 0;
  }

  GPUDestroyBindGroup(state->shadowGroup);
  GPUDestroyTextureView(state->depthView);
  GPUDestroyTexture(state->depthTexture);
  state->depthTexture = texture;
  state->depthView    = view;
  state->shadowGroup  = group;
  return 1;
}

static int
resize_canvas(WebGPUShadowCompare *state) {
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
create_shader(WebGPUShadowCompare *state) {
  const GPUBindGroupLayoutEntry *entries;
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       entryCount;
  bool                           foundDepth;
  bool                           foundComparison;
  GPUResult                      result;

  artifact        = NULL;
  artifactSize    = 0u;
  foundDepth      = false;
  foundComparison = false;
  if (!read_file("/shadow_compare.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /shadow_compare.us", 1);
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
    set_status("GPU: failed to create reflected shadow layout", 1);
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 2u) {
    set_status("GPU: unexpected shadow resource count", 1);
    return 0;
  }
  for (uint32_t i = 0u; i < entryCount; i++) {
    if (entries[i].binding == 0u &&
        entries[i].bindingType == GPU_BINDING_SAMPLED_TEXTURE &&
        entries[i].sampledTexture.sampleType == GPU_TEXTURE_SAMPLE_TYPE_DEPTH) {
      foundDepth = true;
    }
    if (entries[i].binding == 1u &&
        entries[i].bindingType == GPU_BINDING_SAMPLER &&
        entries[i].sampler.type == GPU_SAMPLER_BINDING_COMPARISON &&
        entries[i].immutableSampler &&
        entries[i].immutableSamplerDesc.compareEnable) {
      foundComparison = true;
    }
  }
  if (!foundDepth || !foundComparison) {
    set_status("GPU: USL reflection lost depth comparison types", 1);
    return 0;
  }
  return 1;
}

static int
create_pipelines(WebGPUShadowCompare *state) {
  GPUColorTargetState         color = {0};
  GPUDepthStencilState        depth = {0};
  GPURenderPipelineCreateInfo info  = {0};
  GPUResult                   result;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "shadow-compare-webgpu-depth-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "shadow_depth_vs";
  info.fragmentEntry            = "shadow_depth_fs";
  info.pColorTargets            = &color;
  info.pDepthStencilState       = &depth;
  info.colorTargetCount         = 1u;
  info.depthStencilFormat       = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_NONE;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device,
                                   &info,
                                   &state->depthPipeline);
  if (result != GPU_OK || !state->depthPipeline) {
    set_status("GPU: failed to create shadow depth pipeline", 1);
    return 0;
  }

  info.label              = "shadow-compare-webgpu-preview-pipeline";
  info.vertexEntry        = "shadow_preview_vs";
  info.fragmentEntry      = "shadow_preview_fs";
  info.pDepthStencilState = NULL;
  info.depthStencilFormat = GPU_FORMAT_UNDEFINED;
  result = GPUCreateRenderPipeline(state->device,
                                   &info,
                                   &state->previewPipeline);
  if (result != GPU_OK || !state->previewPipeline) {
    set_status("GPU: failed to create shadow comparison pipeline", 1);
    return 0;
  }

  info.label         = "shadow-compare-webgpu-preview-cool-pipeline";
  info.fragmentEntry = "shadow_preview_cool_fs";
  result = GPUCreateRenderPipeline(state->device,
                                   &info,
                                   &state->previewCoolPipeline);
  if (result != GPU_OK || !state->previewCoolPipeline) {
    set_status("GPU: failed to create second shadow comparison pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUShadowCompare *state) {
  if (!create_depth_target(state, state->width, state->height)) {
    set_status("GPU: failed to create shadow comparison resources", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUShadowCompare                *state;
  GPUFrame                           *frame;
  GPUCommandBuffer                   *cmdb;
  GPURenderPassEncoder               *pass;
  GPURenderPassColorAttachment        color = {0};
  GPURenderPassDepthStencilAttachment depth = {0};
  GPURenderPassCreateInfo             passInfo     = {0};
  GPUTextureBarrier                   depthBarrier = {0};
  GPUBarrierBatch                     barriers     = {0};
  GPUViewport                         viewport     = {0};
  GPUScissorRect                      scissor      = {0};
  uint32_t                            leftWidth;
  uint32_t                            rightWidth;

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
                              "shadow-compare-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.04f;
  color.clearColor.float32[1] = 0.07f;
  color.clearColor.float32[2] = 0.12f;
  color.clearColor.float32[3] = 1.0f;
  depth.view                  = state->depthView;
  depth.depthLoadOp           = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp          = GPU_STORE_OP_STORE;
  depth.stencilLoadOp         = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp        = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth            = 1.0f;
  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "shadow-compare-webgpu-depth-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depth;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->depthPipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  depthBarrier.texture    = state->depthTexture;
  depthBarrier.srcAccess  = GPU_ACCESS_DEPTH_WRITE;
  depthBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  depthBarrier.mipCount   = 1u;
  depthBarrier.layerCount = 1u;
  barriers.pTextureBarriers    = &depthBarrier;
  barriers.srcStages           = GPU_STAGE_FRAGMENT;
  barriers.dstStages           = GPU_STAGE_FRAGMENT;
  barriers.textureBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &barriers);

  color.loadOp                     = GPU_LOAD_OP_LOAD;
  passInfo.label                   = "shadow-compare-webgpu-preview-pass";
  passInfo.pDepthStencilAttachment = NULL;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  leftWidth         = state->width / 2u;
  rightWidth        = state->width - leftWidth;
  viewport.width    = (float)leftWidth;
  viewport.height   = (float)state->height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  scissor.width     = leftWidth;
  scissor.height    = state->height;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderPipeline(pass, state->previewPipeline);
  GPUBindRenderGroup(pass, 0u, state->shadowGroup, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);

  viewport.x     = (float)leftWidth;
  viewport.width = (float)rightWidth;
  scissor.x      = (int32_t)leftWidth;
  scissor.width  = rightWidth;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderPipeline(pass, state->previewCoolPipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish shadow comparison frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 3u || stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status(stats.drawCalls != 3u
                   ? "GPU: shadow comparison did not encode all draws"
                   : "GPU: warm shadow frame allocated wrapper memory",
                 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUShadowCompare *state;
  GPURuntimeConfig     runtime = {0};

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
      !create_shader(state) ||
      !create_pipelines(state) ||
      !create_resources(state)) {
    set_status("GPU: failed to initialize shadow comparison", 1);
    return;
  }

  set_status("GPU: WebGPU USL multi-entry comparison shadow ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "shadow-compare-webgpu-usl";
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
