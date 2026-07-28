#include "../../common/sample_platform.h"

typedef struct ImageVertex {
  float position[2];
  float uv[2];
} ImageVertex;

typedef struct ImageUniforms {
  float scale[2];
} ImageUniforms;

typedef struct WebGPUImageTexture {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *vertexBuffer;
  GPUBuffer         *uniformBuffer;
  GPUTexture        *texture;
  GPUTextureView    *textureView;
  GPUSampler        *sampler;
  GPUBindGroup      *bindGroup;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUImageTexture;

enum {
  WARM_FRAME_COUNT = 8u
};

static const ImageVertex kVertices[] = {
  { { -1.0f, -1.0f }, { 0.0f, 1.0f } },
  { {  1.0f, -1.0f }, { 1.0f, 1.0f } },
  { { -1.0f,  1.0f }, { 0.0f, 0.0f } },
  { { -1.0f,  1.0f }, { 0.0f, 0.0f } },
  { {  1.0f, -1.0f }, { 1.0f, 1.0f } },
  { {  1.0f,  1.0f }, { 1.0f, 0.0f } }
};

static WebGPUImageTexture app;

static void
build_uniforms(const WebGPUImageTexture *state, ImageUniforms *uniforms) {
  float aspect;

  aspect = (float)state->width / (float)state->height;
  if (aspect >= 1.0f) {
    uniforms->scale[0] = 0.82f / aspect;
    uniforms->scale[1] = 0.82f;
  } else {
    uniforms->scale[0] = 0.82f;
    uniforms->scale[1] = 0.82f * aspect;
  }
}

static int
resize_canvas(WebGPUImageTexture *state) {
  ImageUniforms uniforms;
  uint32_t      oldWidth;
  uint32_t      oldHeight;

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

  build_uniforms(state, &uniforms);
  return GPUQueueWriteBuffer(state->queue,
                             state->uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

static int
create_shader(WebGPUImageTexture *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/image_texture.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /image_texture.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the image texture artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0] ||
      !state->shaderLayout->pipelineLayout) {
    set_status("GPU: unexpected image texture reflection", 1);
    return 0;
  }
  return 1;
}

static int
create_pipeline(WebGPUImageTexture *state) {
  GPUVertexAttribute          attributes[2] = {0};
  GPUVertexBufferLayout       vertexLayout  = {0};
  GPUColorTargetState         color         = {0};
  GPURenderPipelineCreateInfo info          = {0};

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[0].offset          = offsetof(ImageVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[1].offset          = offsetof(ImageVertex, uv);
  attributes[1].shaderLocation = 1u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(ImageVertex);
  vertexLayout.attributeCount   = 2u;
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "image-texture-webgpu-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "image_vs";
  info.fragmentEntry            = "image_fs";
  info.pColorTargets            = &color;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount         = 1u;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_NONE;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &info,
                              &state->pipeline) != GPU_OK ||
      !state->pipeline) {
    set_status("GPU: failed to create the image texture pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUImageTexture *state) {
  ImageUniforms           uniforms;
  GPUBufferCreateInfo     bufferInfo  = {0};
  GPUTextureCreateInfo    textureInfo = {0};
  GPUTextureWriteRegion   write       = {0};
  GPUTextureViewCreateInfo viewInfo   = {0};
  GPUSamplerCreateInfo    samplerInfo = {0};
  GPUBindGroupEntry       entries[3]  = {0};
  GPUBindGroupCreateInfo  groupInfo   = {0};
  char                   *pixels;
  int                     imageWidth;
  int                     imageHeight;

  build_uniforms(state, &uniforms);

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "image-texture-vertices";
  bufferInfo.sizeBytes        = sizeof(kVertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          kVertices,
                          sizeof(kVertices)) != GPU_OK) {
    return 0;
  }

  bufferInfo.label     = "image-texture-uniforms";
  bufferInfo.sizeBytes = sizeof(uniforms);
  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    return 0;
  }

  imageWidth  = 0;
  imageHeight = 0;
  pixels = emscripten_get_preloaded_image_data("/texture-coordinate.png",
                                                &imageWidth,
                                                &imageHeight);
  if (!pixels || imageWidth <= 0 || imageHeight <= 0) {
    free(pixels);
    set_status("GPU: browser failed to decode texture-coordinate.png", 1);
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "image-texture-srgb";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM_SRGB;
  textureInfo.width            = (uint32_t)imageWidth;
  textureInfo.height           = (uint32_t)imageHeight;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->texture) != GPU_OK) {
    free(pixels);
    return 0;
  }

  write.aspect       = GPU_TEXTURE_ASPECT_ALL;
  write.width        = (uint32_t)imageWidth;
  write.height       = (uint32_t)imageHeight;
  write.depth        = 1u;
  write.layerCount   = 1u;
  write.bytesPerRow  = (uint32_t)imageWidth * 4u;
  write.rowsPerImage = (uint32_t)imageHeight;
  if (GPUQueueWriteTexture(state->queue,
                           state->texture,
                           &write,
                           pixels,
                           (uint64_t)imageWidth *
                             (uint64_t)imageHeight * 4u) != GPU_OK) {
    free(pixels);
    return 0;
  }
  free(pixels);

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "image-texture-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM_SRGB;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(state->texture,
                           &viewInfo,
                           &state->textureView) != GPU_OK) {
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "image-texture-linear-sampler";
  samplerInfo.desc.minFilter     = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter     = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter     = GPU_MIP_FILTER_LINEAR;
  samplerInfo.desc.addressU      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.maxAnisotropy = 8u;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
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
  groupInfo.label            = "image-texture-group0";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = 3u;
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK) {
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUImageTexture            *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPUBufferBinding               vertexBuffer = {0};
  GPURenderPassColorAttachment   color        = {0};
  GPURenderPassCreateInfo        passInfo     = {0};

  state = userData;
  if (!resize_canvas(state)) {
    set_status("GPU: failed to resize the image texture sample", 1);
    emscripten_cancel_main_loop();
    return;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return;
  }
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "image-texture-webgpu-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.018f;
  color.clearColor.float32[2] = 0.048f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType        = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize   = sizeof(passInfo);
  passInfo.label                = "image-texture-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  vertexBuffer.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->bindGroup, 0u, NULL);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUDraw(pass, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the image texture frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.hotPathAllocCount != 0u || stats.hotPathFreeCount != 0u)) {
      set_status("GPU: warm image texture frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUImageTexture *state;
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
  if (!state->swapchain ||
      !create_shader(state) ||
      !create_pipeline(state) ||
      !create_resources(state)) {
    set_status("GPU: failed to initialize image texture resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL image texture ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "image-texture-webgpu-usl";
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
