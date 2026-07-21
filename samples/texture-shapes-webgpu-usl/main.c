#include "../common/webgpu.h"

#include <stdio.h>

enum {
  SHAPE_SIZE        = 8u,
  SHAPE_ROW_PITCH   = SHAPE_SIZE * 4u,
  SHAPE_SLICE_SIZE  = SHAPE_ROW_PITCH * SHAPE_SIZE,
  CUBE_FACE_COUNT   = 6u,
  VOLUME_DEPTH      = 8u,
  WARM_FRAME_COUNT  = 8u
};

typedef struct WebGPUTextureShapes {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPURenderPipeline  *pipeline;
  GPUTexture         *cubeTexture;
  GPUTextureView     *cubeView;
  GPUTexture         *volumeTexture;
  GPUTextureView     *volumeView;
  GPUSampler         *sampler;
  GPUBindGroup       *bindGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
} WebGPUTextureShapes;

static WebGPUTextureShapes app;

static int
resize_canvas(WebGPUTextureShapes *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static void
fill_cube_face(uint8_t *pixels, uint32_t face) {
  static const uint8_t colors[CUBE_FACE_COUNT][2][4] = {
    {{255u,  72u,  30u, 255u}, {255u, 172u,  34u, 255u}},
    {{ 22u, 184u,  96u, 255u}, {108u, 232u,  92u, 255u}},
    {{ 32u, 106u, 232u, 255u}, { 32u, 206u, 248u, 255u}},
    {{250u, 202u,  38u, 255u}, {255u, 118u,  26u, 255u}},
    {{205u,  60u, 224u, 255u}, {255u, 105u, 170u, 255u}},
    {{ 12u, 154u, 174u, 255u}, { 38u, 226u, 212u, 255u}}
  };

  for (uint32_t y = 0u; y < SHAPE_SIZE; y++) {
    for (uint32_t x = 0u; x < SHAPE_SIZE; x++) {
      const uint8_t *color;
      uint32_t       offset;

      color  = colors[face][((x >> 1u) + (y >> 1u)) & 1u];
      offset = (y * SHAPE_SIZE + x) * 4u;
      pixels[offset + 0u] = color[0];
      pixels[offset + 1u] = color[1];
      pixels[offset + 2u] = color[2];
      pixels[offset + 3u] = color[3];
    }
  }
}

static void
fill_volume(uint8_t pixels[VOLUME_DEPTH][SHAPE_SLICE_SIZE]) {
  for (uint32_t z = 0u; z < VOLUME_DEPTH; z++) {
    for (uint32_t y = 0u; y < SHAPE_SIZE; y++) {
      for (uint32_t x = 0u; x < SHAPE_SIZE; x++) {
        uint32_t offset;

        offset = (y * SHAPE_SIZE + x) * 4u;
        pixels[z][offset + 0u] = (uint8_t)(28u + x * 24u);
        pixels[z][offset + 1u] = (uint8_t)(34u + y * 23u);
        pixels[z][offset + 2u] = (uint8_t)(54u + z * 27u);
        pixels[z][offset + 3u] = 255u;
      }
    }
  }
}

static int
create_shader(WebGPUTextureShapes *state) {
  const GPUBindGroupLayoutEntry *entries;
  GPUColorTargetState            color = {0};
  GPURenderPipelineCreateInfo    info  = {0};
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       entryCount;
  GPUResult                      result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/texture_shapes.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /texture_shapes.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the texture-shapes artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected texture-shapes reflection", 1);
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 3u ||
      entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_CUBE ||
      entries[0].sampledTexture.sampleType != GPU_TEXTURE_SAMPLE_TYPE_FLOAT ||
      entries[1].binding != 1u ||
      entries[1].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[1].sampledTexture.viewType != GPU_TEXTURE_VIEW_3D ||
      entries[1].sampledTexture.sampleType != GPU_TEXTURE_SAMPLE_TYPE_FLOAT ||
      entries[2].binding != 2u ||
      entries[2].bindingType != GPU_BINDING_SAMPLER) {
    set_status("GPU: texture-shapes reflection lost its typed layout", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "texture-shapes-webgpu-usl-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "shapes_vs";
  info.fragmentEntry           = "shapes_fs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create the texture-shapes pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUTextureShapes *state) {
  uint8_t                  cubePixels[CUBE_FACE_COUNT][SHAPE_SLICE_SIZE];
  uint8_t                  volumePixels[VOLUME_DEPTH][SHAPE_SLICE_SIZE];
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUSamplerCreateInfo     samplerInfo = {0};
  GPUBindGroupEntry        entries[3]  = {0};
  GPUBindGroupCreateInfo   groupInfo   = {0};

  for (uint32_t face = 0u; face < CUBE_FACE_COUNT; face++) {
    fill_cube_face(cubePixels[face], face);
  }
  fill_volume(volumePixels);

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "texture-shapes-webgpu-usl-cube";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = SHAPE_SIZE;
  textureInfo.height           = SHAPE_SIZE;
  textureInfo.depthOrLayers    = CUBE_FACE_COUNT;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->cubeTexture) != GPU_OK) {
    set_status("GPU: failed to create the cube texture", 1);
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = SHAPE_SIZE;
  writeRegion.height       = SHAPE_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = SHAPE_ROW_PITCH;
  writeRegion.rowsPerImage = SHAPE_SIZE;
  for (uint32_t face = 0u; face < CUBE_FACE_COUNT; face++) {
    writeRegion.baseArrayLayer = face;
    if (GPUQueueWriteTexture(state->queue,
                             state->cubeTexture,
                             &writeRegion,
                             cubePixels[face],
                             SHAPE_SLICE_SIZE) != GPU_OK) {
      set_status("GPU: failed to upload a cube face", 1);
      return 0;
    }
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "texture-shapes-webgpu-usl-cube-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_CUBE;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = CUBE_FACE_COUNT;
  if (GPUCreateTextureView(state->cubeTexture,
                           &viewInfo,
                           &state->cubeView) != GPU_OK) {
    set_status("GPU: failed to create the cube view", 1);
    return 0;
  }

  textureInfo.label         = "texture-shapes-webgpu-usl-volume";
  textureInfo.dimension     = GPU_TEXTURE_DIMENSION_3D;
  textureInfo.depthOrLayers = VOLUME_DEPTH;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->volumeTexture) != GPU_OK) {
    set_status("GPU: failed to create the 3D texture", 1);
    return 0;
  }

  writeRegion.depth          = VOLUME_DEPTH;
  writeRegion.baseArrayLayer = 0u;
  if (GPUQueueWriteTexture(state->queue,
                           state->volumeTexture,
                           &writeRegion,
                           volumePixels,
                           sizeof(volumePixels)) != GPU_OK) {
    set_status("GPU: failed to upload the 3D texture", 1);
    return 0;
  }

  viewInfo.label           = "texture-shapes-webgpu-usl-volume-view";
  viewInfo.viewType        = GPU_TEXTURE_VIEW_3D;
  viewInfo.arrayLayerCount = 1u;
  if (GPUCreateTextureView(state->volumeTexture,
                           &viewInfo,
                           &state->volumeView) != GPU_OK) {
    set_status("GPU: failed to create the 3D view", 1);
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "texture-shapes-webgpu-usl-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_LINEAR;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    set_status("GPU: failed to create the texture-shapes sampler", 1);
    return 0;
  }

  entries[0].textureView = state->cubeView;
  entries[0].binding     = 0u;
  entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entries[1].textureView = state->volumeView;
  entries[1].binding     = 1u;
  entries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  entries[2].sampler     = state->sampler;
  entries[2].binding     = 2u;
  entries[2].bindingType = GPU_BINDING_SAMPLER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "texture-shapes-webgpu-usl-group0";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = GPU_ARRAY_LEN(entries);
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK) {
    set_status("GPU: failed to create the texture-shapes bind group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUTextureShapes           *state;
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
                              "texture-shapes-webgpu-frame",
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
  passInfo.label                = "texture-shapes-webgpu-pass";
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
    fprintf(stderr, "GPU: failed to finish WebGPU texture-shapes frame\n");
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
  WebGPUTextureShapes *state;
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
    set_status("GPU: failed to initialize texture-shapes resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL texture shapes ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "texture-shapes-webgpu-usl";
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
