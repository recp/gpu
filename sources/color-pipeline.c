#include "../common/webgpu.h"

#include <stdio.h>

enum {
  COLOR_SOURCE_SIZE    = 256u,
  COLOR_HDR_WIDTH      = 640u,
  COLOR_HDR_HEIGHT     = 400u,
  COLOR_PIXEL_CAPACITY = COLOR_SOURCE_SIZE * COLOR_SOURCE_SIZE * 4u,
  WARM_FRAME_COUNT     = 8u
};

typedef struct WebGPUColorPipeline {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *hdrPipeline;
  GPURenderPipeline *tonemapPipeline;
  GPUTexture        *srgbTexture;
  GPUTexture        *linearTexture;
  GPUTexture        *hdrTexture;
  GPUTextureView    *srgbView;
  GPUTextureView    *linearView;
  GPUTextureView    *hdrView;
  GPUSampler        *sampler;
  GPUBindGroup      *sourceGroup;
  GPUBindGroup      *tonemapGroup;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
  bool               failed;
} WebGPUColorPipeline;

static WebGPUColorPipeline app;
static uint8_t             sourcePixels[COLOR_PIXEL_CAPACITY];

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUColorPipeline *state;

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
resize_canvas(WebGPUColorPipeline *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static void
fill_source(void) {
  for (uint32_t y = 0u; y < COLOR_SOURCE_SIZE; y++) {
    for (uint32_t x = 0u; x < COLOR_SOURCE_SIZE; x++) {
      uint32_t cell;
      uint32_t offset;
      uint32_t red;
      uint32_t green;
      uint32_t blue;
      int32_t  dx;
      int32_t  dy;

      cell   = ((x / 32u) + (y / 32u)) & 1u;
      offset = (y * COLOR_SOURCE_SIZE + x) * 4u;
      red    = 24u + x * 210u / (COLOR_SOURCE_SIZE - 1u);
      green  = 18u + y * 205u / (COLOR_SOURCE_SIZE - 1u);
      blue   = cell ? 178u : 56u;
      dx     = (int32_t)x - 174;
      dy     = (int32_t)y - 78;
      if (dx * dx + dy * dy < 27 * 27) {
        red   = 255u;
        green = 232u;
        blue  = 118u;
      }

      sourcePixels[offset + 0u] = (uint8_t)red;
      sourcePixels[offset + 1u] = (uint8_t)green;
      sourcePixels[offset + 2u] = (uint8_t)blue;
      sourcePixels[offset + 3u] = 255u;
    }
  }
}

static int
validate_reflection(WebGPUColorPipeline *state) {
  const GPUBindGroupLayoutEntry *sourceEntries;
  const GPUBindGroupLayoutEntry *tonemapEntries;
  uint32_t                       sourceCount;
  uint32_t                       tonemapCount;

  sourceEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &sourceCount
  );
  tonemapEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[1],
    &tonemapCount
  );
  if (!sourceEntries || sourceCount != 3u ||
      sourceEntries[0].binding != 0u ||
      sourceEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      sourceEntries[1].binding != 1u ||
      sourceEntries[1].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      sourceEntries[2].binding != 2u ||
      sourceEntries[2].bindingType != GPU_BINDING_SAMPLER) {
    return 0;
  }
  if (!tonemapEntries || tonemapCount != 2u ||
      tonemapEntries[0].binding != 0u ||
      tonemapEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      tonemapEntries[1].binding != 1u ||
      tonemapEntries[1].bindingType != GPU_BINDING_SAMPLER) {
    return 0;
  }
  return 1;
}

static int
create_shader(WebGPUColorPipeline *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/color_pipeline.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /color_pipeline.us", 1);
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
    set_status("GPU: unexpected color-pipeline reflection", 1);
    return 0;
  }
  return 1;
}

static int
create_pipelines(WebGPUColorPipeline *state) {
  GPURenderPipelineCreateInfo info  = {0};
  GPUColorTargetState         color = {0};

  color.format          = GPU_FORMAT_RGBA16_FLOAT;
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "webgpu-color-hdr";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "color_vs";
  info.fragmentEntry           = "hdr_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->hdrPipeline) != GPU_OK ||
      !state->hdrPipeline) {
    set_status("GPU: failed to create HDR pipeline", 1);
    return 0;
  }

  color.format       = GPUGetSwapchainFormat(state->swapchain);
  info.label         = "webgpu-color-tonemap";
  info.fragmentEntry = "tonemap_fs";
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->tonemapPipeline) != GPU_OK ||
      !state->tonemapPipeline) {
    set_status("GPU: failed to create tone-map pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_texture(WebGPUColorPipeline *state,
               const char          *label,
               GPUFormat            format,
               uint32_t             width,
               uint32_t             height,
               GPUTextureUsageFlags usage,
               GPUTexture         **outTexture,
               GPUTextureView     **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = usage;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       outTexture) != GPU_OK ||
      !*outTexture) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(*outTexture, &viewInfo, outView) != GPU_OK ||
      !*outView) {
    GPUDestroyTexture(*outTexture);
    *outTexture = NULL;
    return 0;
  }
  return 1;
}

static int
create_bind_group(WebGPUColorPipeline    *state,
                  const char             *label,
                  uint32_t                layoutIndex,
                  const GPUBindGroupEntry *entries,
                  uint32_t                entryCount,
                  GPUBindGroup          **outGroup) {
  GPUBindGroupCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.layout           = state->shaderLayout->bindGroupLayouts[layoutIndex];
  info.pEntries         = entries;
  info.entryCount       = entryCount;
  return GPUCreateBindGroup(state->device, &info, outGroup) == GPU_OK &&
         *outGroup;
}

static int
create_resources(WebGPUColorPipeline *state) {
  GPUTextureWriteRegion upload           = {0};
  GPUSamplerCreateInfo  samplerInfo      = {0};
  GPUBindGroupEntry     sourceEntries[3] = {0};
  GPUBindGroupEntry     toneEntries[2]   = {0};

  if (!create_texture(state,
                      "webgpu-color-srgb",
                      GPU_FORMAT_RGBA8_UNORM_SRGB,
                      COLOR_SOURCE_SIZE,
                      COLOR_SOURCE_SIZE,
                      GPU_TEXTURE_USAGE_SAMPLED |
                        GPU_TEXTURE_USAGE_COPY_DST,
                      &state->srgbTexture,
                      &state->srgbView) ||
      !create_texture(state,
                      "webgpu-color-linear",
                      GPU_FORMAT_RGBA8_UNORM,
                      COLOR_SOURCE_SIZE,
                      COLOR_SOURCE_SIZE,
                      GPU_TEXTURE_USAGE_SAMPLED |
                        GPU_TEXTURE_USAGE_COPY_DST,
                      &state->linearTexture,
                      &state->linearView) ||
      !create_texture(state,
                      "webgpu-color-hdr",
                      GPU_FORMAT_RGBA16_FLOAT,
                      COLOR_HDR_WIDTH,
                      COLOR_HDR_HEIGHT,
                      GPU_TEXTURE_USAGE_COLOR_TARGET |
                        GPU_TEXTURE_USAGE_SAMPLED,
                      &state->hdrTexture,
                      &state->hdrView)) {
    set_status("GPU: failed to create color-pipeline textures", 1);
    return 0;
  }

  fill_source();
  upload.aspect       = GPU_TEXTURE_ASPECT_ALL;
  upload.width        = COLOR_SOURCE_SIZE;
  upload.height       = COLOR_SOURCE_SIZE;
  upload.depth        = 1u;
  upload.layerCount   = 1u;
  upload.bytesPerRow  = COLOR_SOURCE_SIZE * 4u;
  upload.rowsPerImage = COLOR_SOURCE_SIZE;
  if (GPUQueueWriteTexture(state->queue,
                           state->srgbTexture,
                           &upload,
                           sourcePixels,
                           sizeof(sourcePixels)) != GPU_OK ||
      GPUQueueWriteTexture(state->queue,
                           state->linearTexture,
                           &upload,
                           sourcePixels,
                           sizeof(sourcePixels)) != GPU_OK) {
    set_status("GPU: failed to upload color-pipeline source", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "webgpu-color-linear-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK ||
      !state->sampler) {
    set_status("GPU: failed to create color-pipeline sampler", 1);
    return 0;
  }

  sourceEntries[0].textureView = state->srgbView;
  sourceEntries[0].binding     = 0u;
  sourceEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  sourceEntries[1].textureView = state->linearView;
  sourceEntries[1].binding     = 1u;
  sourceEntries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  sourceEntries[2].sampler     = state->sampler;
  sourceEntries[2].binding     = 2u;
  sourceEntries[2].bindingType = GPU_BINDING_SAMPLER;

  toneEntries[0].textureView = state->hdrView;
  toneEntries[0].binding     = 0u;
  toneEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  toneEntries[1].sampler     = state->sampler;
  toneEntries[1].binding     = 1u;
  toneEntries[1].bindingType = GPU_BINDING_SAMPLER;

  if (!create_bind_group(state,
                         "webgpu-color-source-group",
                         0u,
                         sourceEntries,
                         GPU_ARRAY_LEN(sourceEntries),
                         &state->sourceGroup) ||
      !create_bind_group(state,
                         "webgpu-color-tonemap-group",
                         1u,
                         toneEntries,
                         GPU_ARRAY_LEN(toneEntries),
                         &state->tonemapGroup)) {
    set_status("GPU: failed to create color-pipeline bind groups", 1);
    return 0;
  }
  return 1;
}

static int
encode_hdr_pass(WebGPUColorPipeline *state,
                GPUCommandBuffer    *cmdb) {
  GPURenderPassColorAttachment color    = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPURenderPassEncoder        *pass;

  color.view                  = state->hdrView;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "webgpu-color-hdr-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    return 0;
  }

  GPUBindRenderPipeline(pass, state->hdrPipeline);
  GPUBindRenderGroup(pass, 0u, state->sourceGroup, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  return 1;
}

static void
encode_hdr_barrier(WebGPUColorPipeline *state,
                   GPUCommandBuffer    *cmdb) {
  GPUTextureBarrier barrier = {0};
  GPUBarrierBatch   batch   = {0};

  barrier.texture    = state->hdrTexture;
  barrier.srcAccess  = GPU_ACCESS_COLOR_WRITE;
  barrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  barrier.mipCount   = 1u;
  barrier.layerCount = 1u;
  batch.pTextureBarriers    = &barrier;
  batch.srcStages           = GPU_STAGE_FRAGMENT;
  batch.dstStages           = GPU_STAGE_FRAGMENT;
  batch.textureBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &batch);
}

static int
encode_tonemap_pass(WebGPUColorPipeline *state,
                    GPUCommandBuffer    *cmdb,
                    GPUFrame            *frame) {
  GPURenderPassColorAttachment color    = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPURenderPassEncoder        *pass;

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.006f;
  color.clearColor.float32[1] = 0.010f;
  color.clearColor.float32[2] = 0.024f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "webgpu-color-tonemap-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    return 0;
  }

  GPUBindRenderPipeline(pass, state->tonemapPipeline);
  GPUBindRenderGroup(pass, 1u, state->tonemapGroup, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUColorPipeline *state;
  GPUFrame            *frame;
  GPUCommandBuffer    *cmdb;

  state = userData;
  if (!resize_canvas(state)) {
    return;
  }
  frame = GPUBeginFrame(state->swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(state->queue,
                              "webgpu-color-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  if (!encode_hdr_pass(state, cmdb)) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    set_status("GPU: failed to encode HDR pass", 1);
    return;
  }
  encode_hdr_barrier(state, cmdb);
  if (!encode_tonemap_pass(state, cmdb, frame)) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    set_status("GPU: failed to encode tone-map pass", 1);
    return;
  }
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish color-pipeline frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 2u ||
         stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: color-pipeline warm path regression", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUColorPipeline *state;
  GPURuntimeConfig     runtime = {0};

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
    set_status("GPU: failed to configure color-pipeline runtime", 1);
    return;
  }
  if (GPUSetDeviceErrorCallback(device, device_error, state) != GPU_OK) {
    set_status("GPU: failed to install color-pipeline error callback", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create color-pipeline canvas", 1);
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

  GPUResetStats(device);
  set_status("GPU: WebGPU USL color pipeline ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "color-pipeline-webgpu-usl";
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
