#include "../common/webgpu.h"

#include <stdio.h>

enum {
  ARRAY_SIZE        = 8u,
  ARRAY_ROW_PITCH   = ARRAY_SIZE * 4u,
  ARRAY_LAYER_SIZE  = ARRAY_ROW_PITCH * ARRAY_SIZE,
  ARRAY_LAYER_COUNT = 2u,
  WARM_FRAME_COUNT  = 8u
};

typedef struct WebGPUTextureArray {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPURenderPipeline  *pipeline;
  GPUTexture         *texture;
  GPUTextureView     *textureView;
  GPUSampler         *sampler;
  GPUBindGroup       *bindGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
} WebGPUTextureArray;

static WebGPUTextureArray app;

static int
resize_canvas(WebGPUTextureArray *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static void
fill_layer(uint8_t *pixels, uint32_t layer) {
  static const uint8_t colors[2][2][4] = {
    {
      { 255u,  88u,  18u, 255u },
      { 255u, 196u,  42u, 255u }
    },
    {
      {   0u, 195u, 255u, 255u },
      {   9u,  54u, 168u, 255u }
    }
  };

  for (uint32_t y = 0u; y < ARRAY_SIZE; y++) {
    for (uint32_t x = 0u; x < ARRAY_SIZE; x++) {
      const uint8_t *color;
      uint32_t       offset;

      color  = colors[layer][((x >> 1u) + (y >> 1u)) & 1u];
      offset = (y * ARRAY_SIZE + x) * 4u;
      pixels[offset + 0u] = color[0];
      pixels[offset + 1u] = color[1];
      pixels[offset + 2u] = color[2];
      pixels[offset + 3u] = color[3];
    }
  }
}

static int
create_shader(WebGPUTextureArray *state) {
  const GPUBindGroupLayoutEntry *entries;
  GPUColorTargetState            color = {0};
  GPURenderPipelineCreateInfo    info  = {0};
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       entryCount;
  GPUResult                      result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/texture_array.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /texture_array.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the texture-array artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected texture-array reflection", 1);
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 2u ||
      entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D_ARRAY ||
      entries[0].sampledTexture.sampleType != GPU_TEXTURE_SAMPLE_TYPE_FLOAT ||
      entries[1].binding != 1u ||
      entries[1].bindingType != GPU_BINDING_SAMPLER) {
    set_status("GPU: texture-array reflection lost its typed layout", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "texture-array-webgpu-usl-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "array_vs";
  info.fragmentEntry           = "array_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create the texture-array pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUTextureArray *state) {
  uint8_t                  pixels[ARRAY_LAYER_COUNT][ARRAY_LAYER_SIZE];
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUBindGroupEntry        entries[2]  = {0};
  GPUBindGroupCreateInfo   groupInfo   = {0};

  fill_layer(pixels[0], 0u);
  fill_layer(pixels[1], 1u);

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "texture-array-webgpu-usl-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = ARRAY_SIZE;
  textureInfo.height           = ARRAY_SIZE;
  textureInfo.depthOrLayers    = ARRAY_LAYER_COUNT;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->texture) != GPU_OK) {
    set_status("GPU: failed to create the texture array", 1);
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = ARRAY_SIZE;
  writeRegion.height       = ARRAY_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = ARRAY_ROW_PITCH;
  writeRegion.rowsPerImage = ARRAY_SIZE;
  for (uint32_t layer = 0u; layer < ARRAY_LAYER_COUNT; layer++) {
    writeRegion.baseArrayLayer = layer;
    if (GPUQueueWriteTexture(state->queue,
                             state->texture,
                             &writeRegion,
                             pixels[layer],
                             ARRAY_LAYER_SIZE) != GPU_OK) {
      set_status("GPU: failed to upload a texture-array layer", 1);
      return 0;
    }
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "texture-array-webgpu-usl-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = ARRAY_LAYER_COUNT;
  if (GPUCreateTextureView(state->texture,
                           &viewInfo,
                           &state->textureView) != GPU_OK) {
    set_status("GPU: failed to create the texture-array view", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "texture-array-webgpu-usl-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    set_status("GPU: failed to create the texture-array sampler", 1);
    return 0;
  }

  entries[0].textureView = state->textureView;
  entries[0].binding     = 0u;
  entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entries[1].sampler     = state->sampler;
  entries[1].binding     = 1u;
  entries[1].bindingType = GPU_BINDING_SAMPLER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "texture-array-webgpu-usl-group0";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK) {
    set_status("GPU: failed to create the texture-array bind group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUTextureArray            *state;
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
                              "texture-array-webgpu-frame",
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
  passInfo.label                = "texture-array-webgpu-pass";
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
    fprintf(stderr, "GPU: failed to finish WebGPU texture-array frame\n");
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
  WebGPUTextureArray *state;
  GPURuntimeConfig    runtime = {0};

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
    set_status("GPU: failed to initialize texture-array resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL texture array ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "texture-array-webgpu-usl";
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
