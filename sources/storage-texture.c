#include "../common/webgpu.h"

#include <stdio.h>

typedef struct StorageVertex {
  float position[4];
  float uv[2];
} StorageVertex;

enum {
  STORAGE_TEXTURE_COUNT  = 2u,
  STORAGE_TEXTURE_SIZE   = 256u,
  STORAGE_WORKGROUP_SIZE = 8u,
  WARM_FRAME_COUNT       = 8u
};

typedef struct WebGPUStorageTexture {
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
  GPUBuffer          *vertexBuffer;
  GPUTexture         *textures[STORAGE_TEXTURE_COUNT];
  GPUTextureView     *textureViews[STORAGE_TEXTURE_COUNT];
  GPUSampler         *sampler;
  GPUBindGroup       *storageGroup;
  GPUBindGroup       *sampleGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
  bool                failed;
} WebGPUStorageTexture;

static const StorageVertex kQuadVertices[] = {
  { { -0.82f, -0.82f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
  { {  0.82f, -0.82f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { { -0.82f,  0.82f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { { -0.82f,  0.82f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { {  0.82f, -0.82f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { {  0.82f,  0.82f, 0.0f, 1.0f }, { 1.0f, 0.0f } }
};

static WebGPUStorageTexture app;

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUStorageTexture *state;

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
resize_canvas(WebGPUStorageTexture *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
validate_reflection(WebGPUStorageTexture *state) {
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
  if (!computeEntries || computeCount != 1u ||
      computeEntries[0].binding != 0u ||
      computeEntries[0].bindingType != GPU_BINDING_STORAGE_TEXTURE ||
      computeEntries[0].arrayCount != STORAGE_TEXTURE_COUNT ||
      computeEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      computeEntries[0].storageTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      computeEntries[0].storageTexture.format != GPU_FORMAT_RGBA8_UNORM ||
      computeEntries[0].storageTexture.access !=
        GPU_STORAGE_TEXTURE_ACCESS_WRITE_ONLY) {
    return 0;
  }
  if (!renderEntries || renderCount != 2u ||
      renderEntries[0].binding != 0u ||
      renderEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      renderEntries[0].arrayCount != STORAGE_TEXTURE_COUNT ||
      renderEntries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      renderEntries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      renderEntries[0].sampledTexture.sampleType !=
        GPU_TEXTURE_SAMPLE_TYPE_FLOAT ||
      renderEntries[1].binding != 1u ||
      renderEntries[1].bindingType != GPU_BINDING_SAMPLER ||
      renderEntries[1].arrayCount != 1u ||
      renderEntries[1].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    return 0;
  }
  return 1;
}

static int
create_shader(WebGPUStorageTexture *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/storage_texture.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /storage_texture.us", 1);
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
    set_status("GPU: unexpected storage-texture reflection", 1);
    return 0;
  }
  return 1;
}

static int
create_pipelines(WebGPUStorageTexture *state) {
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPURenderPipelineCreateInfo  renderInfo = {0};
  GPUVertexAttribute           attributes[2] = {0};
  GPUVertexBufferLayout        vertexLayout = {0};
  GPUColorTargetState          color = {0};

  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "storage-texture-webgpu-compute";
  computeInfo.layout           = state->shaderLayout->pipelineLayout;
  computeInfo.library          = state->library;
  computeInfo.entryPoint       = "paint_cs";
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &state->computePipeline) != GPU_OK ||
      !state->computePipeline) {
    set_status("GPU: failed to create storage-texture compute pipeline", 1);
    return 0;
  }

  attributes[0].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[0].shaderLocation = 0u;
  attributes[0].offset         = offsetof(StorageVertex, position);
  attributes[1].format         = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[1].shaderLocation = 1u;
  attributes[1].offset         = offsetof(StorageVertex, uv);
  vertexLayout.pAttributes     = attributes;
  vertexLayout.strideBytes     = sizeof(StorageVertex);
  vertexLayout.stepMode        = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount  = 2u;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  renderInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize     = sizeof(renderInfo);
  renderInfo.label                = "storage-texture-webgpu-render";
  renderInfo.layout               = state->shaderLayout->pipelineLayout;
  renderInfo.library              = state->library;
  renderInfo.vertexEntry          = "storage_vs";
  renderInfo.fragmentEntry        = "storage_fs";
  renderInfo.pColorTargets        = &color;
  renderInfo.pDepthStencilState   = NULL;
  renderInfo.vertex.pBufferLayouts = &vertexLayout;
  renderInfo.vertex.bufferLayoutCount = 1u;
  renderInfo.colorTargetCount     = 1u;
  renderInfo.depthStencilFormat   = GPU_FORMAT_UNDEFINED;
  renderInfo.primitiveTopology    = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderInfo.cullMode             = GPU_CULL_MODE_NONE;
  renderInfo.frontFace            = GPU_FRONT_FACE_CCW;
  renderInfo.multisample.sampleCount = 1u;
  renderInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &renderInfo,
                              &state->renderPipeline) != GPU_OK ||
      !state->renderPipeline) {
    set_status("GPU: failed to create storage-texture render pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUStorageTexture *state) {
  static const char *textureLabels[STORAGE_TEXTURE_COUNT] = {
    "storage-texture-webgpu-image-0",
    "storage-texture-webgpu-image-1"
  };
  static const char *viewLabels[STORAGE_TEXTURE_COUNT] = {
    "storage-texture-webgpu-view-0",
    "storage-texture-webgpu-view-1"
  };
  GPUBufferCreateInfo          vertexInfo = {0};
  GPUTextureCreateInfo         textureInfo = {0};
  GPUTextureViewCreateInfo     viewInfo = {0};
  GPUSamplerCreateInfo         samplerInfo = {0};
  GPUBindGroupEntry            storageEntries[STORAGE_TEXTURE_COUNT] = {0};
  GPUBindGroupEntry            sampleEntries[STORAGE_TEXTURE_COUNT + 1u] = {0};
  GPUBindGroupCreateInfo       storageInfo = {0};
  GPUBindGroupCreateInfo       sampleInfo = {0};

  vertexInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  vertexInfo.chain.structSize = sizeof(vertexInfo);
  vertexInfo.label            = "storage-texture-webgpu-vertices";
  vertexInfo.sizeBytes        = sizeof(kQuadVertices);
  vertexInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &vertexInfo,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          kQuadVertices,
                          sizeof(kQuadVertices)) != GPU_OK) {
    set_status("GPU: failed to upload storage-texture vertices", 1);
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = STORAGE_TEXTURE_SIZE;
  textureInfo.height           = STORAGE_TEXTURE_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_STORAGE |
                                 GPU_TEXTURE_USAGE_SAMPLED;

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  for (uint32_t i = 0u; i < STORAGE_TEXTURE_COUNT; i++) {
    textureInfo.label = textureLabels[i];
    if (GPUCreateTexture(state->device,
                         &textureInfo,
                         &state->textures[i]) != GPU_OK ||
        !state->textures[i]) {
      set_status("GPU: failed to create a storage texture", 1);
      return 0;
    }

    viewInfo.label = viewLabels[i];
    if (GPUCreateTextureView(state->textures[i],
                             &viewInfo,
                             &state->textureViews[i]) != GPU_OK ||
        !state->textureViews[i]) {
      set_status("GPU: failed to create a storage-texture view", 1);
      return 0;
    }
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "storage-texture-webgpu-sampler";
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
    set_status("GPU: failed to create the storage-texture sampler", 1);
    return 0;
  }

  for (uint32_t i = 0u; i < STORAGE_TEXTURE_COUNT; i++) {
    storageEntries[i].textureView = state->textureViews[i];
    storageEntries[i].binding     = 0u;
    storageEntries[i].arrayIndex  = i;
    storageEntries[i].bindingType = GPU_BINDING_STORAGE_TEXTURE;

    sampleEntries[i].textureView = state->textureViews[i];
    sampleEntries[i].binding     = 0u;
    sampleEntries[i].arrayIndex  = i;
    sampleEntries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  }
  storageInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  storageInfo.chain.structSize = sizeof(storageInfo);
  storageInfo.label            = "storage-texture-webgpu-group0";
  storageInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  storageInfo.pEntries         = storageEntries;
  storageInfo.entryCount       = STORAGE_TEXTURE_COUNT;
  if (GPUCreateBindGroup(state->device,
                         &storageInfo,
                         &state->storageGroup) != GPU_OK ||
      !state->storageGroup) {
    set_status("GPU: failed to create the reflected storage group", 1);
    return 0;
  }

  sampleEntries[STORAGE_TEXTURE_COUNT].sampler     = state->sampler;
  sampleEntries[STORAGE_TEXTURE_COUNT].binding     = 1u;
  sampleEntries[STORAGE_TEXTURE_COUNT].bindingType = GPU_BINDING_SAMPLER;
  sampleInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  sampleInfo.chain.structSize = sizeof(sampleInfo);
  sampleInfo.label            = "storage-texture-webgpu-group1";
  sampleInfo.layout           = state->shaderLayout->bindGroupLayouts[1];
  sampleInfo.pEntries         = sampleEntries;
  sampleInfo.entryCount       = STORAGE_TEXTURE_COUNT + 1u;
  if (GPUCreateBindGroup(state->device,
                         &sampleInfo,
                         &state->sampleGroup) != GPU_OK ||
      !state->sampleGroup) {
    set_status("GPU: failed to create the reflected sample group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUStorageTexture         *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPUComputePassEncoder        *compute;
  GPURenderPassEncoder         *render;
  GPUTextureBarrier             textureBarriers[STORAGE_TEXTURE_COUNT] = {0};
  GPUBarrierBatch               barriers = {0};
  GPUBufferBinding              vertex = {0};
  GPURenderPassColorAttachment  color = {0};
  GPURenderPassCreateInfo       passInfo = {0};

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
                              "storage-texture-webgpu-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  compute = GPUBeginComputePass(cmdb, "storage-texture-webgpu-paint");
  if (!compute) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindComputePipeline(compute, state->computePipeline);
  GPUBindComputeGroup(compute, 0u, state->storageGroup, 0u, NULL);
  GPUDispatch(compute,
              STORAGE_TEXTURE_SIZE / STORAGE_WORKGROUP_SIZE,
              STORAGE_TEXTURE_SIZE / STORAGE_WORKGROUP_SIZE,
              1u);
  GPUEndComputePass(compute);

  for (uint32_t i = 0u; i < STORAGE_TEXTURE_COUNT; i++) {
    textureBarriers[i].texture    = state->textures[i];
    textureBarriers[i].srcAccess  = GPU_ACCESS_SHADER_WRITE;
    textureBarriers[i].dstAccess  = GPU_ACCESS_SHADER_READ;
    textureBarriers[i].mipCount   = 1u;
    textureBarriers[i].layerCount = 1u;
  }
  barriers.pTextureBarriers    = textureBarriers;
  barriers.srcStages           = GPU_STAGE_COMPUTE;
  barriers.dstStages           = GPU_STAGE_FRAGMENT;
  barriers.textureBarrierCount = STORAGE_TEXTURE_COUNT;
  GPUEncodeBarriers(cmdb, &barriers);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.015f;
  color.clearColor.float32[1] = 0.035f;
  color.clearColor.float32[2] = 0.085f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "storage-texture-webgpu-render";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  render = GPUBeginRenderPass(cmdb, &passInfo);
  if (!render) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  vertex.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(render, state->renderPipeline);
  GPUBindVertexBuffers(render, 0u, 1u, &vertex);
  GPUBindRenderGroup(render, 1u, state->sampleGroup, 0u, NULL);
  GPUDraw(render, 6u, 1u, 0u, 0u);
  GPUEndRenderPass(render);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the storage-texture frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK) {
      if (stats.drawCalls != 1u) {
        set_status("GPU: storage-texture draw was not encoded", 1);
        emscripten_cancel_main_loop();
      } else if (stats.hotPathAllocCount != 0u ||
                 stats.hotPathFreeCount != 0u) {
        set_status("GPU: warm storage-texture frame allocated wrapper memory", 1);
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
  WebGPUStorageTexture *state;
  GPURuntimeConfig      runtime = {0};

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
    set_status("GPU: failed to configure WebGPU runtime", 1);
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

  set_status("GPU: WebGPU typed storage texture ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "storage-texture-webgpu-usl";
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
  if (result != GPU_OK) {
    return 1;
  }
  return 0;
}
