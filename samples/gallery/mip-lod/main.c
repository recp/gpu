#include "../../common/sample_platform.h"

#include <stdio.h>

enum {
  MIP_LEVEL_COUNT = 5u,
  MIP_TEXTURE_SIZE = 128u,
  MIP_PIXEL_CAPACITY = MIP_TEXTURE_SIZE * MIP_TEXTURE_SIZE * 4u,
  WARM_FRAME_COUNT = 8u
};

typedef struct WebGPUMipLod {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUTexture        *texture;
  GPUTextureView    *textureView;
  GPUSampler        *sampler;
  GPUBindGroup      *bindGroup;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUMipLod;

static WebGPUMipLod app;
static uint8_t mipPixels[MIP_PIXEL_CAPACITY];

static int
resize_canvas(WebGPUMipLod *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static void
fill_mip(uint8_t *pixels, uint32_t size, uint32_t level) {
  static const uint8_t colors[MIP_LEVEL_COUNT][3] = {
    { 245u,  88u,  38u },
    {  31u, 192u, 224u },
    { 251u, 190u,  47u },
    { 114u, 222u,  93u },
    { 192u,  96u, 232u }
  };
  uint32_t x;
  uint32_t y;

  for (y = 0u; y < size; y++) {
    for (x = 0u; x < size; x++) {
      uint32_t offset;
      uint32_t checker;

      offset  = (y * size + x) * 4u;
      checker = ((x >> (3u - (level > 2u ? 2u : level))) ^
                 (y >> (3u - (level > 2u ? 2u : level)))) & 1u;
      pixels[offset + 0u] = checker
                              ? colors[level][0]
                              : (uint8_t)(colors[level][0] / 4u);
      pixels[offset + 1u] = checker
                              ? colors[level][1]
                              : (uint8_t)(colors[level][1] / 4u);
      pixels[offset + 2u] = checker
                              ? colors[level][2]
                              : (uint8_t)(colors[level][2] / 4u);
      pixels[offset + 3u] = 255u;
    }
  }
}

static int
create_resources(WebGPUMipLod *state) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUBindGroupEntry        entries[2] = {0};
  GPUBindGroupCreateInfo   groupInfo = {0};
  uint32_t                 level;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "webgpu-mip-chain";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = MIP_TEXTURE_SIZE;
  textureInfo.height           = MIP_TEXTURE_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = MIP_LEVEL_COUNT;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->texture) != GPU_OK) {
    set_status("GPU: failed to create mip texture", 1);
    return 0;
  }

  for (level = 0u; level < MIP_LEVEL_COUNT; level++) {
    GPUTextureWriteRegion region = {0};
    uint32_t              size;

    size = MIP_TEXTURE_SIZE >> level;
    fill_mip(mipPixels, size, level);
    region.aspect       = GPU_TEXTURE_ASPECT_ALL;
    region.width        = size;
    region.height       = size;
    region.depth        = 1u;
    region.mipLevel     = level;
    region.layerCount   = 1u;
    region.bytesPerRow  = size * 4u;
    region.rowsPerImage = size;
    if (GPUQueueWriteTexture(state->queue,
                             state->texture,
                             &region,
                             mipPixels,
                             (uint64_t)size * size * 4u) != GPU_OK) {
      set_status("GPU: failed to upload mip chain", 1);
      return 0;
    }
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "webgpu-mip-chain-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = MIP_LEVEL_COUNT;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(state->texture,
                           &viewInfo,
                           &state->textureView) != GPU_OK) {
    set_status("GPU: failed to create mip texture view", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "webgpu-mip-chain-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_REPEAT;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    set_status("GPU: failed to create mip sampler", 1);
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
  groupInfo.label            = "webgpu-mip-chain-group";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK) {
    set_status("GPU: failed to create mip bind group", 1);
    return 0;
  }
  return 1;
}

static int
create_shader_and_pipeline(WebGPUMipLod *state) {
  GPURenderPipelineCreateInfo info = {0};
  GPUColorTargetState         color = {0};
  void                       *artifact;
  uint64_t                    artifactSize;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/mip_lod.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read mip_lod.us", 1);
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
      state->shaderLayout->bindGroupLayoutCount != 1u) {
    set_status("GPU: failed to create mip shader layout", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  info.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "webgpu-mip-lod-pipeline";
  info.layout           = state->shaderLayout->pipelineLayout;
  info.library          = state->library;
  info.vertexEntry      = "mip_vs";
  info.fragmentEntry    = "mip_fs";
  info.pColorTargets    = &color;
  info.colorTargetCount = 1u;
  info.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode          = GPU_CULL_MODE_NONE;
  info.frontFace         = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->pipeline) != GPU_OK) {
    set_status("GPU: failed to create mip render pipeline", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUMipLod                 *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};

  state = userData;
  if (!resize_canvas(state)) {
    return;
  }
  frame = GPUBeginFrame(state->swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(state->queue,
                              "webgpu-mip-lod-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.014f;
  color.clearColor.float32[2] = 0.034f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "webgpu-mip-lod-pass";
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
  GPUDraw(pass, 6u, MIP_LEVEL_COUNT, 0u, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish mip frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 1u ||
         stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: mip warm path regression", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUMipLod   *state;
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
    set_status("GPU: failed to configure mip runtime", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create mip canvas surface", 1);
    return;
  }
  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain ||
      !create_shader_and_pipeline(state) ||
      !create_resources(state)) {
    return;
  }

  GPUResetStats(device);
  set_status("GPU: WebGPU USL explicit mip LOD ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "mip-lod-webgpu-usl";
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
