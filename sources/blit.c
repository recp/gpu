#include "../common/webgpu.h"

#include <stdio.h>

typedef struct BlitVertex {
  float position[2];
  float uv[2];
} BlitVertex;

enum {
  BLIT_SOURCE_SIZE        = 32u,
  BLIT_TARGET_SIZE        = 256u,
  BLIT_PANEL_COUNT        = 3u,
  BLIT_VERTICES_PER_PANEL = 6u,
  BLIT_VERTEX_COUNT       = BLIT_PANEL_COUNT * BLIT_VERTICES_PER_PANEL,
  WARM_FRAME_COUNT        = 8u
};

typedef struct WebGPUBlit {
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
  GPUTexture        *source;
  GPUTexture        *targets[2];
  GPUTextureView    *views[BLIT_PANEL_COUNT];
  GPUSampler        *sampler;
  GPUBindGroup      *groups[BLIT_PANEL_COUNT];
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           layoutWidth;
  uint32_t           layoutHeight;
  uint32_t           frameCount;
  BlitVertex         vertices[BLIT_VERTEX_COUNT];
} WebGPUBlit;

static WebGPUBlit app;
static uint8_t sourcePixels[BLIT_SOURCE_SIZE * BLIT_SOURCE_SIZE * 4u];

static void
fill_panel_vertices(BlitVertex *vertices,
                    float       left,
                    float       right,
                    float       bottom,
                    float       top) {
  vertices[0] = (BlitVertex){{left,  bottom}, {0.0f, 1.0f}};
  vertices[1] = (BlitVertex){{right, bottom}, {1.0f, 1.0f}};
  vertices[2] = (BlitVertex){{left,  top},    {0.0f, 0.0f}};
  vertices[3] = (BlitVertex){{left,  top},    {0.0f, 0.0f}};
  vertices[4] = (BlitVertex){{right, bottom}, {1.0f, 1.0f}};
  vertices[5] = (BlitVertex){{right, top},    {1.0f, 0.0f}};
}

static void
fill_panel_from_pixels(WebGPUBlit *state,
                       uint32_t    panelIndex,
                       float       left,
                       float       top,
                       float       size) {
  float width;
  float height;
  float right;
  float bottom;

  width  = (float)state->width;
  height = (float)state->height;
  right  = left + size;
  bottom = top + size;
  fill_panel_vertices(
    &state->vertices[panelIndex * BLIT_VERTICES_PER_PANEL],
    left / width * 2.0f - 1.0f,
    right / width * 2.0f - 1.0f,
    1.0f - bottom / height * 2.0f,
    1.0f - top / height * 2.0f
  );
}

static int
update_panel_vertices(WebGPUBlit *state) {
  float outputSize;
  float maximumOutputSize;
  float sourceSize;
  float gap;
  float totalWidth;
  float left;
  float top;

  if (!state || !state->queue || !state->vertexBuffer ||
      state->width == 0u || state->height == 0u) {
    return 0;
  }
  if (state->layoutWidth == state->width &&
      state->layoutHeight == state->height) {
    return 1;
  }

  outputSize        = (float)state->height * 0.60f;
  maximumOutputSize = (float)state->width / 2.82f;
  if (outputSize > maximumOutputSize) {
    outputSize = maximumOutputSize;
  }
  sourceSize        = outputSize * 0.46f;
  gap               = outputSize * 0.12f;
  totalWidth        = sourceSize + outputSize * 2.0f + gap * 2.0f;
  left              = ((float)state->width - totalWidth) * 0.5f;

  top = ((float)state->height - sourceSize) * 0.5f;
  fill_panel_from_pixels(state, 0u, left, top, sourceSize);
  left += sourceSize + gap;

  top = ((float)state->height - outputSize) * 0.5f;
  fill_panel_from_pixels(state, 1u, left, top, outputSize);
  left += outputSize + gap;
  fill_panel_from_pixels(state, 2u, left, top, outputSize);

  if (GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          state->vertices,
                          sizeof(state->vertices)) != GPU_OK) {
    return 0;
  }
  state->layoutWidth  = state->width;
  state->layoutHeight = state->height;
  return 1;
}

static int
resize_canvas(WebGPUBlit *state) {
  if (!resize_webgpu_canvas(state->swapchain,
                            &state->width,
                            &state->height)) {
    return 0;
  }
  return !state->vertexBuffer || update_panel_vertices(state);
}

static void
fill_source_pixels(void) {
  for (uint32_t y = 0u; y < BLIT_SOURCE_SIZE; y++) {
    for (uint32_t x = 0u; x < BLIT_SOURCE_SIZE; x++) {
      uint32_t offset;
      uint32_t checker;
      uint32_t diagonal;
      uint8_t  red;
      uint8_t  green;
      uint8_t  blue;

      offset   = (y * BLIT_SOURCE_SIZE + x) * 4u;
      checker  = ((x >> 2u) ^ (y >> 2u)) & 1u;
      diagonal = x > y ? x - y : y - x;
      red      = (uint8_t)(40u + x * 190u / (BLIT_SOURCE_SIZE - 1u));
      green    = (uint8_t)(35u + y * 190u / (BLIT_SOURCE_SIZE - 1u));
      blue     = checker ? 230u : 45u;
      if (diagonal <= 1u ||
          x + y == BLIT_SOURCE_SIZE - 1u ||
          x + y == BLIT_SOURCE_SIZE - 2u) {
        red   = 255u;
        green = 220u;
        blue  = 70u;
      }

      sourcePixels[offset + 0u] = red;
      sourcePixels[offset + 1u] = green;
      sourcePixels[offset + 2u] = blue;
      sourcePixels[offset + 3u] = 255u;
    }
  }
}

static int
create_shader(WebGPUBlit *state) {
  void      *artifact;
  uint64_t   artifactSize;
  GPUResult  result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/blit.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /blit.us", 1);
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
      !state->shaderLayout->bindGroupLayouts[0] ||
      !state->shaderLayout->pipelineLayout) {
    set_status("GPU: failed to create blit sample shader layout", 1);
    return 0;
  }
  return 1;
}

static int
create_pipeline(WebGPUBlit *state) {
  GPUVertexAttribute          attributes[2] = {0};
  GPUVertexBufferLayout       vertexLayout = {0};
  GPUColorTargetState         color = {0};
  GPURenderPipelineCreateInfo info = {0};

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[0].offset          = offsetof(BlitVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[1].offset          = offsetof(BlitVertex, uv);
  attributes[1].shaderLocation = 1u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(BlitVertex);
  vertexLayout.attributeCount   = 2u;
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "blit-webgpu-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "blit_panel_vs";
  info.fragmentEntry            = "blit_panel_fs";
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
    set_status("GPU: failed to create blit sample pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_blit_textures(WebGPUBlit *state) {
  GPUCommandBuffer        *cmdb;
  GPUCommandBuffer        *submitBuffers[1];
  GPUTexture              *panelTextures[BLIT_PANEL_COUNT];
  const char              *viewLabels[BLIT_PANEL_COUNT];
  const char              *groupLabels[BLIT_PANEL_COUNT];
  GPUBufferCreateInfo      bufferInfo = {0};
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    write = {0};
  GPUTextureBlitInfo       blit = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUBindGroupEntry        entries[2] = {0};
  GPUBindGroupCreateInfo   groupInfo = {0};
  GPUQueueSubmitInfo       submit = {0};

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "blit-webgpu-panel-vertices";
  bufferInfo.sizeBytes        = sizeof(state->vertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->vertexBuffer) != GPU_OK ||
      !update_panel_vertices(state)) {
    set_status("GPU: failed to upload blit panel vertices", 1);
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "blit-webgpu-source";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = BLIT_SOURCE_SIZE;
  textureInfo.height           = BLIT_SOURCE_SIZE;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_SRC |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->source) != GPU_OK) {
    set_status("GPU: failed to create blit source", 1);
    return 0;
  }

  textureInfo.width  = BLIT_TARGET_SIZE;
  textureInfo.height = BLIT_TARGET_SIZE;
  textureInfo.usage  = GPU_TEXTURE_USAGE_SAMPLED |
                       GPU_TEXTURE_USAGE_COLOR_TARGET |
                       GPU_TEXTURE_USAGE_COPY_DST;
  textureInfo.label  = "blit-webgpu-nearest-target";
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->targets[0]) != GPU_OK) {
    set_status("GPU: failed to create nearest blit target", 1);
    return 0;
  }
  textureInfo.label  = "blit-webgpu-linear-target";
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->targets[1]) != GPU_OK) {
    set_status("GPU: failed to create linear blit target", 1);
    return 0;
  }

  write.aspect       = GPU_TEXTURE_ASPECT_ALL;
  fill_source_pixels();
  write.width        = BLIT_SOURCE_SIZE;
  write.height       = BLIT_SOURCE_SIZE;
  write.depth        = 1u;
  write.layerCount   = 1u;
  write.bytesPerRow  = BLIT_SOURCE_SIZE * 4u;
  write.rowsPerImage = BLIT_SOURCE_SIZE;
  if (GPUQueueWriteTexture(state->queue,
                           state->source,
                           &write,
                           sourcePixels,
                           sizeof(sourcePixels)) != GPU_OK) {
    set_status("GPU: failed to upload blit source", 1);
    return 0;
  }

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "blit-webgpu-setup",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    set_status("GPU: failed to acquire blit setup command buffer", 1);
    return 0;
  }

  blit.src                      = state->source;
  blit.srcRegion.texture.aspect = GPU_TEXTURE_ASPECT_ALL;
  blit.srcRegion.width          = BLIT_SOURCE_SIZE;
  blit.srcRegion.height         = BLIT_SOURCE_SIZE;
  blit.srcRegion.depth          = 1u;
  blit.srcRegion.layerCount     = 1u;
  blit.dstRegion.texture.aspect = GPU_TEXTURE_ASPECT_ALL;
  blit.dstRegion.width          = BLIT_TARGET_SIZE;
  blit.dstRegion.height         = BLIT_TARGET_SIZE;
  blit.dstRegion.depth          = 1u;
  blit.dstRegion.layerCount     = 1u;

  blit.dst    = state->targets[0];
  blit.filter = GPU_FILTER_NEAREST;
  GPUBlit(cmdb, &blit);
  blit.dst    = state->targets[1];
  blit.filter = GPU_FILTER_LINEAR;
  GPUBlit(cmdb, &blit);

  submitBuffers[0]          = cmdb;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = submitBuffers;
  submit.commandBufferCount = 1u;
  if (GPUQueueSubmit(state->queue, &submit) != GPU_OK) {
    set_status("GPU: failed to submit texture blits", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "blit-webgpu-presentation-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.maxAnisotropy = 1u;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    set_status("GPU: failed to create blit presentation sampler", 1);
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  entries[0].binding         = 0u;
  entries[0].bindingType     = GPU_BINDING_SAMPLED_TEXTURE;
  entries[1].binding         = 1u;
  entries[1].bindingType     = GPU_BINDING_SAMPLER;
  entries[1].sampler         = state->sampler;

  panelTextures[0] = state->source;
  panelTextures[1] = state->targets[0];
  panelTextures[2] = state->targets[1];
  viewLabels[0]    = "blit-webgpu-source-view";
  viewLabels[1]    = "blit-webgpu-nearest-view";
  viewLabels[2]    = "blit-webgpu-linear-view";
  groupLabels[0]   = "blit-webgpu-source-group";
  groupLabels[1]   = "blit-webgpu-nearest-group";
  groupLabels[2]   = "blit-webgpu-linear-group";
  for (uint32_t i = 0u; i < BLIT_PANEL_COUNT; i++) {
    viewInfo.label = viewLabels[i];
    if (GPUCreateTextureView(panelTextures[i],
                             &viewInfo,
                             &state->views[i]) != GPU_OK) {
      set_status("GPU: failed to create blit panel view", 1);
      return 0;
    }

    entries[0].textureView = state->views[i];
    groupInfo.label        = groupLabels[i];
    if (GPUCreateBindGroup(state->device,
                           &groupInfo,
                           &state->groups[i]) != GPU_OK) {
      set_status("GPU: failed to create blit panel group", 1);
      return 0;
    }
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUBlit                  *state;
  GPUFrame                    *frame;
  GPUCommandBuffer            *cmdb;
  GPURenderPassEncoder        *pass;
  GPUBufferBinding             vertexBuffer = {0};
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};

  state = userData;
  if (!resize_canvas(state)) {
    return;
  }

  frame = GPUBeginFrame(state->swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(state->queue,
                              "blit-webgpu-frame",
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
  passInfo.label                = "blit-webgpu-present-pass";
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
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  for (uint32_t i = 0u; i < BLIT_PANEL_COUNT; i++) {
    GPUBindRenderGroup(pass, 0u, state->groups[i], 0u, NULL);
    GPUDraw(pass,
            BLIT_VERTICES_PER_PANEL,
            1u,
            i * BLIT_VERTICES_PER_PANEL,
            0u);
  }
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish blit sample frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != BLIT_PANEL_COUNT ||
         stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: blit sample warm path regression", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUBlit      *state;
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
    set_status("GPU: failed to configure blit sample runtime", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create blit sample surface", 1);
    return;
  }
  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain ||
      !create_shader(state) ||
      !create_pipeline(state) ||
      !create_blit_textures(state)) {
    return;
  }

  GPUResetStats(device);
  set_status("GPU: source left, nearest blit center, linear blit right", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "blit-webgpu-usl";
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
