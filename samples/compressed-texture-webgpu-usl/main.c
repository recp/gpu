#include "../common/webgpu.h"

#include <stdio.h>

enum {
  COMPRESSED_TEXTURE_SIZE = 256u,
  WARM_FRAME_COUNT        = 8u
};

typedef struct CompressedSource {
  const char *path;
  const char *name;
  GPUFormat   format;
  uint64_t    sizeBytes;
  uint32_t    bytesPerRow;
} CompressedSource;

typedef struct CompressedUniforms {
  float scale[2];
} CompressedUniforms;

typedef struct WebGPUCompressedTexture {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *uniformBuffer;
  GPUTexture        *texture;
  GPUTextureView    *textureView;
  GPUSampler        *sampler;
  GPUBindGroup      *bindGroup;
  WebGPURequest      request;
  const char        *selectedFormat;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
  bool               failed;
} WebGPUCompressedTexture;

static const CompressedSource kSources[] = {
  {
    "/texture.astc",
    "ASTC 4x4 sRGB",
    GPU_FORMAT_ASTC_4X4_UNORM_SRGB,
    64u * 64u * 16u,
    64u * 16u
  },
  {
    "/texture.bc1",
    "BC1 sRGB",
    GPU_FORMAT_BC1_RGBA_UNORM_SRGB,
    64u * 64u * 8u,
    64u * 8u
  },
  {
    "/texture.etc2",
    "ETC2 RGBA8 sRGB",
    GPU_FORMAT_ETC2_RGBA8_UNORM_SRGB,
    64u * 64u * 16u,
    64u * 16u
  },
  {
    "/texture.rgba",
    "RGBA8 sRGB fallback",
    GPU_FORMAT_RGBA8_UNORM_SRGB,
    COMPRESSED_TEXTURE_SIZE * COMPRESSED_TEXTURE_SIZE * 4u,
    COMPRESSED_TEXTURE_SIZE * 4u
  }
};

static WebGPUCompressedTexture app;

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUCompressedTexture *state;

  (void)device;
  state = userData;
  if (!state || !error || state->failed) return;
  state->failed = true;
  set_status(error->message ? error->message : "GPU: unknown device error", 1);
  emscripten_cancel_main_loop();
}

static int
resize_canvas(WebGPUCompressedTexture *state) {
  CompressedUniforms uniforms;
  uint32_t           oldWidth;
  uint32_t           oldHeight;

  oldWidth  = state->width;
  oldHeight = state->height;
  if (!resize_webgpu_canvas(state->swapchain,
                            &state->width,
                            &state->height)) {
    return 0;
  }
  if (!state->uniformBuffer ||
      (oldWidth == state->width && oldHeight == state->height)) {
    return 1;
  }
  uniforms.scale[0] = 0.78f * (float)state->height / (float)state->width;
  uniforms.scale[1] = 0.78f;
  return GPUQueueWriteBuffer(state->queue,
                             state->uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

static const CompressedSource *
select_source(const WebGPUCompressedTexture *state) {
  GPUFormatCapabilities caps;

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(kSources); i++) {
    if (GPUGetFormatCapabilities(state->adapter,
                                 kSources[i].format,
                                 &caps) == GPU_OK &&
        caps.sampled &&
        caps.filterable) {
      return &kSources[i];
    }
  }
  return NULL;
}

static int
create_shader(WebGPUCompressedTexture *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/compressed_texture.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read compressed_texture.us", 1);
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
    set_status("GPU: unexpected compressed-texture reflection", 1);
    return 0;
  }
  return 1;
}

static int
create_pipeline(WebGPUCompressedTexture *state) {
  GPURenderPipelineCreateInfo info  = {0};
  GPUColorTargetState         color = {0};

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "webgpu-compressed-texture-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "compressed_vs";
  info.fragmentEntry           = "compressed_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->pipeline) != GPU_OK ||
      !state->pipeline) {
    set_status("GPU: failed to create compressed-texture pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUCompressedTexture *state) {
  const CompressedSource  *source;
  CompressedUniforms       uniforms;
  GPUBufferCreateInfo      bufferInfo  = {0};
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureWriteRegion    write       = {0};
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUBindGroupEntry        entries[3]  = {0};
  GPUBindGroupCreateInfo   groupInfo   = {0};
  void                    *data;
  uint64_t                 sizeBytes;

  source = select_source(state);
  if (!source) {
    set_status("GPU: no portable sampled texture format available", 1);
    return 0;
  }
  data      = NULL;
  sizeBytes = 0u;
  if (!read_file(source->path, &data, &sizeBytes) ||
      sizeBytes != source->sizeBytes) {
    free(data);
    set_status("GPU: invalid compressed texture payload", 1);
    return 0;
  }

  uniforms.scale[0] = 0.78f * (float)state->height / (float)state->width;
  uniforms.scale[1] = 0.78f;
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "webgpu-compressed-texture-uniforms";
  bufferInfo.sizeBytes        = sizeof(uniforms);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    free(data);
    set_status("GPU: failed to create compressed-texture uniforms", 1);
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "webgpu-selected-compressed-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = source->format;
  textureInfo.width            = COMPRESSED_TEXTURE_SIZE;
  textureInfo.height           = COMPRESSED_TEXTURE_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->texture) != GPU_OK ||
      !state->texture) {
    free(data);
    set_status("GPU: failed to create selected texture format", 1);
    return 0;
  }

  write.aspect       = GPU_TEXTURE_ASPECT_ALL;
  write.width        = COMPRESSED_TEXTURE_SIZE;
  write.height       = COMPRESSED_TEXTURE_SIZE;
  write.depth        = 1u;
  write.layerCount   = 1u;
  write.bytesPerRow  = source->bytesPerRow;
  write.rowsPerImage = COMPRESSED_TEXTURE_SIZE;
  if (GPUQueueWriteTexture(state->queue,
                           state->texture,
                           &write,
                           data,
                           sizeBytes) != GPU_OK) {
    free(data);
    set_status("GPU: failed to upload selected texture format", 1);
    return 0;
  }
  free(data);

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "webgpu-selected-compressed-texture-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = source->format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(state->texture,
                           &viewInfo,
                           &state->textureView) != GPU_OK ||
      !state->textureView) {
    set_status("GPU: failed to create selected texture view", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "webgpu-compressed-texture-sampler";
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
    set_status("GPU: failed to create compressed-texture sampler", 1);
    return 0;
  }

  entries[0].buffer.buffer = state->uniformBuffer;
  entries[0].buffer.size   = sizeof(uniforms);
  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entries[1].textureView   = state->textureView;
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  entries[2].sampler       = state->sampler;
  entries[2].binding       = 2u;
  entries[2].bindingType   = GPU_BINDING_SAMPLER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "webgpu-compressed-texture-group";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK ||
      !state->bindGroup) {
    set_status("GPU: failed to create compressed-texture bind group", 1);
    return 0;
  }
  state->selectedFormat = source->name;
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUCompressedTexture      *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color    = {0};
  GPURenderPassCreateInfo       passInfo = {0};

  state = userData;
  if (!resize_canvas(state)) {
    set_status("GPU: failed to resize compressed-texture sample", 1);
    emscripten_cancel_main_loop();
    return;
  }
  frame = GPUBeginFrame(state->swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(state->queue,
                              "webgpu-compressed-texture-frame",
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
  passInfo.chain.sType         = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize    = sizeof(passInfo);
  passInfo.label                = "webgpu-compressed-texture-pass";
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
  GPUDraw(pass, 4u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish compressed-texture frame", 1);
    emscripten_cancel_main_loop();
    return;
  }
  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 1u ||
         stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: compressed-texture warm path regression", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUCompressedTexture *state;
  GPURuntimeConfig         runtime = {0};
  char                     status[128];

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
  if (!state->queue ||
      GPUConfigureRuntime(device, &runtime) != GPU_OK ||
      GPUSetDeviceErrorCallback(device,
                                device_error,
                                state) != GPU_OK) {
    set_status("GPU: failed to configure compressed-texture runtime", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create compressed-texture surface", 1);
    return;
  }
  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain ||
      !create_shader(state) ||
      !create_pipeline(state) ||
      !create_resources(state)) {
    return;
  }

  GPUResetStats(device);
  snprintf(status,
           sizeof(status),
           "GPU: %s selected; portable fallback ready",
           state->selectedFormat);
  set_status(status, 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "compressed-texture-webgpu-usl";
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
