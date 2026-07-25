#include "../common/webgpu.h"

#include <stdio.h>

enum {
  BLOOM_TEXTURE_SIZE   = 256u,
  BLOOM_WORKGROUP_SIZE = 8u,
  BLOOM_PIXEL_CAPACITY = BLOOM_TEXTURE_SIZE * BLOOM_TEXTURE_SIZE * 4u,
  WARM_FRAME_COUNT     = 8u
};

typedef struct WebGPUBloom {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPUComputePipeline *horizontalPipeline;
  GPUComputePipeline *verticalPipeline;
  GPURenderPipeline  *renderPipeline;
  GPUTexture         *sourceTexture;
  GPUTexture         *temporaryTexture;
  GPUTexture         *bloomTexture;
  GPUTextureView     *sourceView;
  GPUTextureView     *temporaryView;
  GPUTextureView     *bloomView;
  GPUSampler         *sampler;
  GPUBindGroup       *horizontalGroup;
  GPUBindGroup       *verticalGroup;
  GPUBindGroup       *renderGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
  bool                failed;
} WebGPUBloom;

static WebGPUBloom app;
static uint8_t     sourcePixels[BLOOM_PIXEL_CAPACITY];

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUBloom *state;

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
resize_canvas(WebGPUBloom *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static void
fill_source(void) {
  for (uint32_t y = 0u; y < BLOOM_TEXTURE_SIZE; y++) {
    for (uint32_t x = 0u; x < BLOOM_TEXTURE_SIZE; x++) {
      int32_t  cyanX;
      int32_t  cyanY;
      int32_t  orangeX;
      int32_t  orangeY;
      int32_t  triangleX;
      int32_t  triangleY;
      int32_t  triangleHalfWidth;
      uint32_t offset;
      uint8_t  red;
      uint8_t  green;
      uint8_t  blue;

      cyanX     = (int32_t)x - 82;
      cyanY     = (int32_t)y - 142;
      orangeX   = (int32_t)x - 174;
      orangeY   = (int32_t)y - 105;
      triangleX = (int32_t)x - 128;
      triangleY = (int32_t)y - 38;
      offset    = (y * BLOOM_TEXTURE_SIZE + x) * 4u;
      red       = 2u;
      green     = 5u;
      blue      = 14u;

      if (cyanX * cyanX + cyanY * cyanY < 25 * 25) {
        red   = 24u;
        green = 220u;
        blue  = 255u;
      }
      if (orangeX * orangeX + orangeY * orangeY < 18 * 18) {
        red   = 255u;
        green = 92u;
        blue  = 18u;
      }
      if (triangleY >= 0 && triangleY < 36) {
        triangleHalfWidth = triangleY * 3 / 5;
        if (triangleX >= -triangleHalfWidth &&
            triangleX <= triangleHalfWidth) {
          red   = 255u;
          green = 230u;
          blue  = 96u;
        }
      }
      sourcePixels[offset + 0u] = red;
      sourcePixels[offset + 1u] = green;
      sourcePixels[offset + 2u] = blue;
      sourcePixels[offset + 3u] = 255u;
    }
  }
}

static int
validate_reflection(WebGPUBloom *state) {
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
      computeEntries[1].binding != 1u ||
      computeEntries[1].bindingType != GPU_BINDING_STORAGE_TEXTURE ||
      computeEntries[1].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      computeEntries[1].storageTexture.format != GPU_FORMAT_RGBA8_UNORM ||
      computeEntries[1].storageTexture.access !=
        GPU_STORAGE_TEXTURE_ACCESS_WRITE_ONLY) {
    return 0;
  }
  if (!renderEntries || renderCount != 3u ||
      renderEntries[0].binding != 0u ||
      renderEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      renderEntries[1].binding != 1u ||
      renderEntries[1].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      renderEntries[2].binding != 2u ||
      renderEntries[2].bindingType != GPU_BINDING_SAMPLER ||
      renderEntries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      renderEntries[1].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      renderEntries[2].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    return 0;
  }
  return 1;
}

static int
create_shader(WebGPUBloom *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/bloom.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /bloom.us", 1);
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
    set_status("GPU: unexpected bloom reflection", 1);
    return 0;
  }
  return 1;
}

static int
create_pipelines(WebGPUBloom *state) {
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPURenderPipelineCreateInfo  renderInfo  = {0};
  GPUColorTargetState          color       = {0};

  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "webgpu-bloom-horizontal";
  computeInfo.layout           = state->shaderLayout->pipelineLayout;
  computeInfo.library          = state->library;
  computeInfo.entryPoint       = "blur_horizontal";
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &state->horizontalPipeline) != GPU_OK ||
      !state->horizontalPipeline) {
    set_status("GPU: failed to create horizontal bloom pipeline", 1);
    return 0;
  }

  computeInfo.label      = "webgpu-bloom-vertical";
  computeInfo.entryPoint = "blur_vertical";
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &state->verticalPipeline) != GPU_OK ||
      !state->verticalPipeline) {
    set_status("GPU: failed to create vertical bloom pipeline", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  renderInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize        = sizeof(renderInfo);
  renderInfo.label                   = "webgpu-bloom-composite";
  renderInfo.layout                  = state->shaderLayout->pipelineLayout;
  renderInfo.library                 = state->library;
  renderInfo.vertexEntry             = "bloom_vs";
  renderInfo.fragmentEntry           = "bloom_fs";
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
    set_status("GPU: failed to create bloom composite pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_texture(WebGPUBloom        *state,
               const char         *textureLabel,
               const char         *viewLabel,
               GPUTextureUsageFlags usage,
               GPUTexture        **outTexture,
               GPUTextureView    **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = textureLabel;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = BLOOM_TEXTURE_SIZE;
  textureInfo.height           = BLOOM_TEXTURE_SIZE;
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
  viewInfo.label            = viewLabel;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
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
create_bind_group(WebGPUBloom *state,
                  const char  *label,
                  uint32_t     layoutIndex,
                  uint32_t     entryCount,
                  const GPUBindGroupEntry *entries,
                  GPUBindGroup **outGroup) {
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
create_resources(WebGPUBloom *state) {
  GPUTextureWriteRegion upload = {0};
  GPUSamplerCreateInfo  samplerInfo = {0};
  GPUBindGroupEntry     horizontalEntries[2] = {0};
  GPUBindGroupEntry     verticalEntries[2] = {0};
  GPUBindGroupEntry     renderEntries[3] = {0};

  if (!create_texture(state,
                      "webgpu-bloom-source",
                      "webgpu-bloom-source-view",
                      GPU_TEXTURE_USAGE_SAMPLED |
                        GPU_TEXTURE_USAGE_COPY_DST,
                      &state->sourceTexture,
                      &state->sourceView) ||
      !create_texture(state,
                      "webgpu-bloom-temporary",
                      "webgpu-bloom-temporary-view",
                      GPU_TEXTURE_USAGE_SAMPLED |
                        GPU_TEXTURE_USAGE_STORAGE,
                      &state->temporaryTexture,
                      &state->temporaryView) ||
      !create_texture(state,
                      "webgpu-bloom-output",
                      "webgpu-bloom-output-view",
                      GPU_TEXTURE_USAGE_SAMPLED |
                        GPU_TEXTURE_USAGE_STORAGE,
                      &state->bloomTexture,
                      &state->bloomView)) {
    set_status("GPU: failed to create bloom textures", 1);
    return 0;
  }

  fill_source();
  upload.aspect         = GPU_TEXTURE_ASPECT_ALL;
  upload.width          = BLOOM_TEXTURE_SIZE;
  upload.height         = BLOOM_TEXTURE_SIZE;
  upload.depth          = 1u;
  upload.layerCount     = 1u;
  upload.bytesPerRow    = BLOOM_TEXTURE_SIZE * 4u;
  upload.rowsPerImage   = BLOOM_TEXTURE_SIZE;
  if (GPUQueueWriteTexture(state->queue,
                           state->sourceTexture,
                           &upload,
                           sourcePixels,
                           sizeof(sourcePixels)) != GPU_OK) {
    set_status("GPU: failed to upload bloom source", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "webgpu-bloom-sampler";
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
    set_status("GPU: failed to create bloom sampler", 1);
    return 0;
  }

  horizontalEntries[0].textureView = state->sourceView;
  horizontalEntries[0].binding     = 0u;
  horizontalEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  horizontalEntries[1].textureView = state->temporaryView;
  horizontalEntries[1].binding     = 1u;
  horizontalEntries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;

  verticalEntries[0].textureView = state->temporaryView;
  verticalEntries[0].binding     = 0u;
  verticalEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  verticalEntries[1].textureView = state->bloomView;
  verticalEntries[1].binding     = 1u;
  verticalEntries[1].bindingType = GPU_BINDING_STORAGE_TEXTURE;

  renderEntries[0].textureView = state->sourceView;
  renderEntries[0].binding     = 0u;
  renderEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  renderEntries[1].textureView = state->bloomView;
  renderEntries[1].binding     = 1u;
  renderEntries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  renderEntries[2].sampler     = state->sampler;
  renderEntries[2].binding     = 2u;
  renderEntries[2].bindingType = GPU_BINDING_SAMPLER;

  if (!create_bind_group(state,
                         "webgpu-bloom-horizontal-group",
                         0u,
                         2u,
                         horizontalEntries,
                         &state->horizontalGroup) ||
      !create_bind_group(state,
                         "webgpu-bloom-vertical-group",
                         0u,
                         2u,
                         verticalEntries,
                         &state->verticalGroup) ||
      !create_bind_group(state,
                         "webgpu-bloom-render-group",
                         1u,
                         3u,
                         renderEntries,
                         &state->renderGroup)) {
    set_status("GPU: failed to create bloom bind groups", 1);
    return 0;
  }
  return 1;
}

static int
encode_blur_pass(GPUCommandBuffer     *cmdb,
                 GPUComputePipeline   *pipeline,
                 GPUBindGroup         *group,
                 const char           *label) {
  GPUComputePassEncoder *pass;

  pass = GPUBeginComputePass(cmdb, label);
  if (!pass) {
    return 0;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass,
              BLOOM_TEXTURE_SIZE / BLOOM_WORKGROUP_SIZE,
              BLOOM_TEXTURE_SIZE / BLOOM_WORKGROUP_SIZE,
              1u);
  GPUEndComputePass(pass);
  return 1;
}

static void
encode_texture_barrier(GPUCommandBuffer    *cmdb,
                       GPUTexture          *texture,
                       GPUPipelineStageMask srcStages,
                       GPUPipelineStageMask dstStages) {
  GPUTextureBarrier barrier = {0};
  GPUBarrierBatch   batch   = {0};

  barrier.texture    = texture;
  barrier.srcAccess  = GPU_ACCESS_SHADER_WRITE;
  barrier.dstAccess  = GPU_ACCESS_SHADER_READ;
  barrier.mipCount   = 1u;
  barrier.layerCount = 1u;
  batch.pTextureBarriers    = &barrier;
  batch.srcStages           = srcStages;
  batch.dstStages           = dstStages;
  batch.textureBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &batch);
}

static void
render_frame(void *userData) {
  WebGPUBloom                  *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color    = {0};
  GPURenderPassCreateInfo       passInfo = {0};

  state = userData;
  if (!resize_canvas(state)) {
    return;
  }
  frame = GPUBeginFrame(state->swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(state->queue,
                              "webgpu-bloom-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  if (!encode_blur_pass(cmdb,
                        state->horizontalPipeline,
                        state->horizontalGroup,
                        "webgpu-bloom-horizontal")) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    set_status("GPU: failed to encode horizontal bloom pass", 1);
    return;
  }
  encode_texture_barrier(cmdb,
                         state->temporaryTexture,
                         GPU_STAGE_COMPUTE,
                         GPU_STAGE_COMPUTE);
  if (!encode_blur_pass(cmdb,
                        state->verticalPipeline,
                        state->verticalGroup,
                        "webgpu-bloom-vertical")) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    set_status("GPU: failed to encode vertical bloom pass", 1);
    return;
  }
  encode_texture_barrier(cmdb,
                         state->bloomTexture,
                         GPU_STAGE_COMPUTE,
                         GPU_STAGE_FRAGMENT);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.014f;
  color.clearColor.float32[2] = 0.034f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label                = "webgpu-bloom-composite";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  GPUBindRenderPipeline(pass, state->renderPipeline);
  GPUBindRenderGroup(pass, 1u, state->renderGroup, 0u, NULL);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish bloom frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 1u ||
         stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: bloom warm path regression", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUBloom     *state;
  GPURuntimeConfig runtime = {0};

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
    set_status("GPU: failed to configure bloom runtime", 1);
    return;
  }
  if (GPUSetDeviceErrorCallback(device, device_error, state) != GPU_OK) {
    set_status("GPU: failed to install bloom error callback", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create bloom canvas surface", 1);
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
  set_status("GPU: WebGPU USL separable bloom ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "bloom-webgpu-usl";
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
