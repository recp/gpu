#include "../common/webgpu.h"

#include <stdio.h>

enum {
  LINE_SIZE           = 16u,
  LINE_ROW_PITCH      = LINE_SIZE * 4u,
  LINE_WORKGROUP_SIZE = 8u,
  WARM_FRAME_COUNT    = 8u
};

typedef struct WebGPUTextureLine {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPUComputePipeline *computePipeline;
  GPURenderPipeline  *renderPipeline;
  GPUTexture         *inputTexture;
  GPUTexture         *outputTexture;
  GPUTextureView     *inputView;
  GPUTextureView     *outputView;
  GPUBindGroup       *computeGroup;
  GPUBindGroup       *renderGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
  bool                failed;
} WebGPUTextureLine;

static WebGPUTextureLine app;

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUTextureLine *state;

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
resize_canvas(WebGPUTextureLine *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
validate_reflection(WebGPUTextureLine *state) {
  const GPUBindGroupLayoutEntry *computeEntries;
  const GPUBindGroupLayoutEntry *renderEntries;
  uint32_t                       computeCount;
  uint32_t                       renderCount;

  computeEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &computeCount
  );
  renderEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[1],
    &renderCount
  );
  if (!computeEntries || computeCount != 2u ||
      computeEntries[0].binding != 0u ||
      computeEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      computeEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      computeEntries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_1D ||
      computeEntries[0].sampledTexture.sampleType !=
        GPU_TEXTURE_SAMPLE_TYPE_FLOAT ||
      computeEntries[1].binding != 1u ||
      computeEntries[1].bindingType != GPU_BINDING_STORAGE_TEXTURE ||
      computeEntries[1].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      computeEntries[1].storageTexture.viewType != GPU_TEXTURE_VIEW_1D ||
      computeEntries[1].storageTexture.format != GPU_FORMAT_RGBA8_UNORM ||
      computeEntries[1].storageTexture.access !=
        GPU_STORAGE_TEXTURE_ACCESS_WRITE_ONLY) {
    return 0;
  }
  if (!renderEntries || renderCount != 2u ||
      renderEntries[0].binding != 0u ||
      renderEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      renderEntries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      renderEntries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_1D ||
      renderEntries[0].sampledTexture.sampleType !=
        GPU_TEXTURE_SAMPLE_TYPE_FLOAT ||
      renderEntries[1].binding != 1u ||
      renderEntries[1].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      renderEntries[1].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      renderEntries[1].sampledTexture.viewType != GPU_TEXTURE_VIEW_1D ||
      renderEntries[1].sampledTexture.sampleType !=
        GPU_TEXTURE_SAMPLE_TYPE_FLOAT) {
    return 0;
  }
  return 1;
}

static int
create_shader(WebGPUTextureLine *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/texture_line.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /texture_line.us", 1);
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
      state->shaderLayout->bindGroupLayoutCount != 2u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0] ||
      !state->shaderLayout->bindGroupLayouts[1] ||
      !validate_reflection(state)) {
    set_status("GPU: unexpected texture-line reflection", 1);
    return 0;
  }
  return 1;
}

static int
create_pipelines(WebGPUTextureLine *state) {
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPURenderPipelineCreateInfo  renderInfo  = {0};
  GPUColorTargetState          color       = {0};

  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "texture-line-webgpu-compute";
  computeInfo.layout           = state->shaderLayout->pipelineLayout;
  computeInfo.library          = state->library;
  computeInfo.entryPoint       = "line_transform_cs";
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &state->computePipeline) != GPU_OK ||
      !state->computePipeline) {
    set_status("GPU: failed to create the texture-line compute pipeline", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  renderInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize        = sizeof(renderInfo);
  renderInfo.label                   = "texture-line-webgpu-render";
  renderInfo.layout                  = state->shaderLayout->pipelineLayout;
  renderInfo.library                 = state->library;
  renderInfo.vertexEntry             = "line_vs";
  renderInfo.fragmentEntry           = "line_fs";
  renderInfo.pColorTargets           = &color;
  renderInfo.colorTargetCount        = 1u;
  renderInfo.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderInfo.cullMode                = GPU_CULL_MODE_NONE;
  renderInfo.frontFace               = GPU_FRONT_FACE_CCW;
  renderInfo.multisample.sampleCount = 1u;
  renderInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &renderInfo,
                              &state->renderPipeline) != GPU_OK ||
      !state->renderPipeline) {
    set_status("GPU: failed to create the texture-line render pipeline", 1);
    return 0;
  }
  return 1;
}

static void
fill_line(uint8_t pixels[LINE_ROW_PITCH]) {
  static const uint8_t colors[4][4] = {
    { 255u,  70u,  22u, 255u },
    { 255u, 196u,  42u, 255u },
    {   0u, 195u, 255u, 255u },
    {  18u,  52u, 178u, 255u }
  };

  for (uint32_t x = 0u; x < LINE_SIZE; x++) {
    const uint8_t *color;
    uint32_t       offset;

    color  = colors[x >> 2u];
    offset = x * 4u;
    pixels[offset + 0u] = color[0];
    pixels[offset + 1u] = color[1];
    pixels[offset + 2u] = color[2];
    pixels[offset + 3u] = color[3];
  }
}

static int
create_resources(WebGPUTextureLine *state) {
  uint8_t                  pixels[LINE_ROW_PITCH];
  GPUTextureCreateInfo     textureInfo       = {0};
  GPUTextureWriteRegion    writeRegion       = {0};
  GPUTextureViewCreateInfo viewInfo          = {0};
  GPUBindGroupEntry        computeEntries[2] = {0};
  GPUBindGroupEntry        renderEntries[2]  = {0};
  GPUBindGroupCreateInfo   computeGroupInfo  = {0};
  GPUBindGroupCreateInfo   renderGroupInfo   = {0};

  fill_line(pixels);

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "texture-line-webgpu-input";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_1D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = LINE_SIZE;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->inputTexture) != GPU_OK) {
    set_status("GPU: failed to create the input 1D texture", 1);
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = LINE_SIZE;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = LINE_ROW_PITCH;
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(state->queue,
                           state->inputTexture,
                           &writeRegion,
                           pixels,
                           sizeof(pixels)) != GPU_OK) {
    set_status("GPU: failed to upload the input 1D texture", 1);
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "texture-line-webgpu-input-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_1D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(state->inputTexture,
                           &viewInfo,
                           &state->inputView) != GPU_OK) {
    set_status("GPU: failed to create the input 1D texture view", 1);
    return 0;
  }

  textureInfo.label = "texture-line-webgpu-output";
  textureInfo.usage = GPU_TEXTURE_USAGE_STORAGE |
                      GPU_TEXTURE_USAGE_SAMPLED;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->outputTexture) != GPU_OK) {
    set_status("GPU: failed to create the output 1D texture", 1);
    return 0;
  }

  viewInfo.label = "texture-line-webgpu-output-view";
  if (GPUCreateTextureView(state->outputTexture,
                           &viewInfo,
                           &state->outputView) != GPU_OK) {
    set_status("GPU: failed to create the output 1D texture view", 1);
    return 0;
  }

  computeEntries[0].textureView = state->inputView;
  computeEntries[0].binding     = 0u;
  computeEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  computeEntries[1].textureView = state->outputView;
  computeEntries[1].binding     = 1u;
  computeEntries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  computeGroupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  computeGroupInfo.chain.structSize = sizeof(computeGroupInfo);
  computeGroupInfo.label            = "texture-line-webgpu-compute-group";
  computeGroupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  computeGroupInfo.pEntries         = computeEntries;
  computeGroupInfo.entryCount       = GPU_ARRAY_LEN(computeEntries);
  if (GPUCreateBindGroup(state->device,
                         &computeGroupInfo,
                         &state->computeGroup) != GPU_OK) {
    set_status("GPU: failed to create the texture-line compute group", 1);
    return 0;
  }

  renderEntries[0].textureView = state->inputView;
  renderEntries[0].binding     = 0u;
  renderEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  renderEntries[1].textureView = state->outputView;
  renderEntries[1].binding     = 1u;
  renderEntries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  renderGroupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  renderGroupInfo.chain.structSize = sizeof(renderGroupInfo);
  renderGroupInfo.label            = "texture-line-webgpu-render-group";
  renderGroupInfo.layout           = state->shaderLayout->bindGroupLayouts[1];
  renderGroupInfo.pEntries         = renderEntries;
  renderGroupInfo.entryCount       = GPU_ARRAY_LEN(renderEntries);
  if (GPUCreateBindGroup(state->device,
                         &renderGroupInfo,
                         &state->renderGroup) != GPU_OK) {
    set_status("GPU: failed to create the texture-line render group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUTextureLine            *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPUComputePassEncoder        *compute;
  GPURenderPassEncoder         *render;
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
                              "texture-line-webgpu-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  compute = GPUBeginComputePass(cmdb, "texture-line-webgpu-transform");
  if (!compute) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindComputePipeline(compute, state->computePipeline);
  GPUBindComputeGroup(compute, 0u, state->computeGroup, 0u, NULL);
  GPUDispatch(compute, LINE_SIZE / LINE_WORKGROUP_SIZE, 1u, 1u);
  GPUEndComputePass(compute);

  textureBarrier.texture    = state->outputTexture;
  textureBarrier.srcAccess  = GPU_ACCESS_SHADER_WRITE;
  textureBarrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  textureBarrier.mipCount   = 1u;
  textureBarrier.layerCount = 1u;
  barrier.pTextureBarriers    = &textureBarrier;
  barrier.srcStages           = GPU_STAGE_COMPUTE;
  barrier.dstStages           = GPU_STAGE_FRAGMENT;
  barrier.textureBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &barrier);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.012f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.065f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "texture-line-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  render = GPUBeginRenderPass(cmdb, &passInfo);
  if (!render) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(render, state->renderPipeline);
  GPUBindRenderGroup(render, 1u, state->renderGroup, 0u, NULL);
  GPUDraw(render, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(render);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the texture-line frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK) {
      if (stats.drawCalls != 1u) {
        set_status("GPU: texture-line draw was not encoded", 1);
        emscripten_cancel_main_loop();
      } else if (stats.hotPathAllocCount != 0u ||
                 stats.hotPathFreeCount != 0u) {
        set_status("GPU: warm texture-line frame allocated wrapper memory", 1);
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
  WebGPUTextureLine *state;
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
  if (!state->swapchain ||
      !create_shader(state) ||
      !create_pipelines(state) ||
      !create_resources(state)) {
    return;
  }

  set_status("GPU: WebGPU USL 1D compute + render ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "texture-line-webgpu-usl";
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
