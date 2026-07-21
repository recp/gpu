#include "../common/webgpu.h"

#include <stdio.h>

enum {
  DESCRIPTOR_COUNT  = 4u,
  TEXTURE_SIZE      = 8u,
  TEXTURE_ROW_PITCH = TEXTURE_SIZE * 4u,
  TEXTURE_DATA_SIZE = TEXTURE_ROW_PITCH * TEXTURE_SIZE,
  WARM_FRAME_COUNT  = 8u
};

typedef struct WebGPUDescriptorArray {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPURenderPipeline  *pipeline;
  GPUTexture         *textures[DESCRIPTOR_COUNT];
  GPUTextureView     *textureViews[DESCRIPTOR_COUNT];
  GPUSampler         *samplers[DESCRIPTOR_COUNT];
  GPUBindGroup       *bindGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
} WebGPUDescriptorArray;

static WebGPUDescriptorArray app;

static int
resize_canvas(WebGPUDescriptorArray *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static void
fill_texture(uint8_t *pixels, uint32_t textureIndex) {
  static const uint8_t colors[DESCRIPTOR_COUNT][2][4] = {
    {
      { 255u,  86u,  18u, 255u },
      { 255u, 196u,  42u, 255u }
    },
    {
      {   0u, 195u, 255u, 255u },
      {   9u,  54u, 168u, 255u }
    },
    {
      { 255u,  48u, 142u, 255u },
      { 116u,  18u,  86u, 255u }
    },
    {
      {  33u, 220u, 105u, 255u },
      { 184u, 255u,  68u, 255u }
    }
  };

  for (uint32_t y = 0u; y < TEXTURE_SIZE; y++) {
    for (uint32_t x = 0u; x < TEXTURE_SIZE; x++) {
      const uint8_t *color;
      uint32_t       offset;

      color  = colors[textureIndex][((x >> 1u) + (y >> 1u)) & 1u];
      offset = (y * TEXTURE_SIZE + x) * 4u;
      pixels[offset + 0u] = color[0];
      pixels[offset + 1u] = color[1];
      pixels[offset + 2u] = color[2];
      pixels[offset + 3u] = color[3];
    }
  }
}

static int
create_shader(WebGPUDescriptorArray *state) {
  const GPUBindGroupLayoutEntry *entries;
  GPUColorTargetState            color = {0};
  GPURenderPipelineCreateInfo    info  = {0};
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       entryCount;
  GPUResult                      result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/descriptor_array.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /descriptor_array.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the descriptor-array artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected descriptor-array reflection", 1);
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 2u ||
      entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[0].arrayCount != DESCRIPTOR_COUNT ||
      entries[1].binding != 1u ||
      entries[1].bindingType != GPU_BINDING_SAMPLER ||
      entries[1].arrayCount != DESCRIPTOR_COUNT) {
    set_status("GPU: descriptor-array reflection lost its array shape", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "descriptor-array-webgpu-usl-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "descriptor_array_vs";
  info.fragmentEntry           = "descriptor_array_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create the descriptor-array pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUDescriptorArray *state) {
  static const char * const textureLabels[DESCRIPTOR_COUNT] = {
    "descriptor-array-orange",
    "descriptor-array-blue",
    "descriptor-array-pink",
    "descriptor-array-green"
  };
  uint8_t                  pixels[TEXTURE_DATA_SIZE];
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUBindGroupEntry        entries[DESCRIPTOR_COUNT * 2u] = {0};
  GPUBindGroupCreateInfo   groupInfo = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = TEXTURE_SIZE;
  textureInfo.height           = TEXTURE_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = TEXTURE_SIZE;
  writeRegion.height       = TEXTURE_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = TEXTURE_ROW_PITCH;
  writeRegion.rowsPerImage = TEXTURE_SIZE;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_REPEAT;

  for (uint32_t i = 0u; i < DESCRIPTOR_COUNT; i++) {
    fill_texture(pixels, i);
    textureInfo.label = textureLabels[i];
    if (GPUCreateTexture(state->device,
                         &textureInfo,
                         &state->textures[i]) != GPU_OK ||
        GPUQueueWriteTexture(state->queue,
                             state->textures[i],
                             &writeRegion,
                             pixels,
                             sizeof(pixels)) != GPU_OK ||
        GPUCreateTextureView(state->textures[i],
                             &viewInfo,
                             &state->textureViews[i]) != GPU_OK) {
      set_status("GPU: failed to create a descriptor-array texture", 1);
      return 0;
    }

    samplerInfo.label = textureLabels[i];
    if (GPUCreateSampler(state->device,
                         &samplerInfo,
                         false,
                         &state->samplers[i]) != GPU_OK) {
      set_status("GPU: failed to create a descriptor-array sampler", 1);
      return 0;
    }

    entries[i].textureView = state->textureViews[i];
    entries[i].binding     = 0u;
    entries[i].arrayIndex  = i;
    entries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    entries[DESCRIPTOR_COUNT + i].sampler     = state->samplers[i];
    entries[DESCRIPTOR_COUNT + i].binding     = 1u;
    entries[DESCRIPTOR_COUNT + i].arrayIndex  = i;
    entries[DESCRIPTOR_COUNT + i].bindingType = GPU_BINDING_SAMPLER;
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "descriptor-array-webgpu-usl-group0";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK) {
    set_status("GPU: failed to create the descriptor-array bind group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUDescriptorArray          *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPURenderPassColorAttachment   color    = {0};
  GPURenderPassCreateInfo        passInfo = {0};

  state = userData;
  if (!resize_canvas(state)) return;

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) return;

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "descriptor-array-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.012f;
  color.clearColor.float32[1] = 0.025f;
  color.clearColor.float32[2] = 0.065f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "descriptor-array-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->bindGroup, 0u, NULL);
  GPUDraw(pass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU descriptor-array frame\n");
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
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUDescriptorArray *state;
  GPURuntimeConfig       runtime = {0};

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
  if (!state->swapchain || !create_shader(state) || !create_resources(state)) {
    set_status("GPU: failed to initialize descriptor-array resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL descriptor array ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "descriptor-array-webgpu-usl";
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
