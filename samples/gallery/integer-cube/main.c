#include "../../common/sample_platform.h"

#include <stdio.h>

enum {
  CUBE_SIZE         = 32u,
  CUBE_FACE_COUNT   = 6u,
  CUBE_MIP_COUNT    = 3u,
  CUBE_PIXEL_SIZE   = 4u,
  CUBE_MAX_ROW_SIZE = CUBE_SIZE * CUBE_PIXEL_SIZE,
  CUBE_MAX_SIZE     = CUBE_MAX_ROW_SIZE * CUBE_SIZE,
  PANEL_COUNT       = 4u,
  WARM_FRAME_COUNT  = 8u
};

typedef struct WebGPUIntegerCube {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipelines[PANEL_COUNT];
  GPUTexture        *texture;
  GPUTextureView    *view;
  GPUBindGroup      *bindGroup;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
  bool               failed;
} WebGPUIntegerCube;

static WebGPUIntegerCube app;

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  WebGPUIntegerCube *state;

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
resize_canvas(WebGPUIntegerCube *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static uint8_t
darken_channel(uint8_t value, uint8_t amount) {
  return value > amount ? value - amount : value / 2u;
}

static void
fill_cube_face(uint8_t *pixels,
               uint32_t size,
               uint32_t face,
               uint32_t mip) {
  static const uint8_t faceColors[CUBE_FACE_COUNT][3] = {
    {255u,  54u,  26u},
    { 34u, 224u,  92u},
    { 32u, 116u, 255u},
    {255u, 204u,  32u},
    {218u,  66u, 232u},
    { 24u, 214u, 224u}
  };

  for (uint32_t y = 0u; y < size; y++) {
    for (uint32_t x = 0u; x < size; x++) {
      uint32_t checker;
      uint32_t offset;

      checker = ((x >> (mip + 1u)) + (y >> (mip + 1u))) & 1u;
      offset  = (y * size + x) * CUBE_PIXEL_SIZE;
      if (mip == 0u) {
        pixels[offset + 0u] = darken_channel(faceColors[face][0],
                                             checker * 44u);
        pixels[offset + 1u] = darken_channel(faceColors[face][1],
                                             checker * 36u);
        pixels[offset + 2u] = darken_channel(faceColors[face][2],
                                             checker * 36u);
      } else if (mip == 1u) {
        pixels[offset + 0u] = faceColors[face][2] / 2u;
        pixels[offset + 1u] = faceColors[face][0] / 2u;
        pixels[offset + 2u] = faceColors[face][1] / 2u;
      } else {
        pixels[offset + 0u] = 245u;
        pixels[offset + 1u] = 238u - face * 20u;
        pixels[offset + 2u] = 48u + face * 28u;
      }
      pixels[offset + 3u] = 255u;
    }
  }
}

static int
create_shader(WebGPUIntegerCube *state) {
  static const char *fragmentEntries[PANEL_COUNT] = {
    "integer_cube_nearest_fs",
    "integer_cube_level_fs",
    "integer_cube_gradient_fs",
    "integer_cube_bias_fs"
  };
  const GPUBindGroupLayoutEntry *entries;
  GPUColorTargetState            color = {0};
  GPURenderPipelineCreateInfo    info  = {0};
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       entryCount;
  GPUResult                      result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/integer_cube.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /integer_cube.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the integer-cube artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected integer-cube reflection", 1);
    return 0;
  }

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  if (!entries || entryCount != 1u ||
      entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      entries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_CUBE ||
      entries[0].sampledTexture.sampleType != GPU_TEXTURE_SAMPLE_TYPE_UINT) {
    set_status("GPU: integer cube lost its typed reflection", 1);
    return 0;
  }

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = "integer-cube-webgpu-usl-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "integer_cube_vs";
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  for (uint32_t i = 0u; i < PANEL_COUNT; i++) {
    info.fragmentEntry = fragmentEntries[i];
    result = GPUCreateRenderPipeline(state->device,
                                     &info,
                                     &state->pipelines[i]);
    if (result != GPU_OK || !state->pipelines[i]) {
      fprintf(stderr,
              "GPU: integer-cube pipeline %u failed (%d)\n",
              i,
              result);
      set_status("GPU: failed to create an integer-cube pipeline", 1);
      return 0;
    }
  }
  return 1;
}

static int
create_resources(WebGPUIntegerCube *state) {
  uint8_t                  pixels[CUBE_MAX_SIZE];
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    write       = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUBindGroupEntry        entry       = {0};
  GPUBindGroupCreateInfo   groupInfo   = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "integer-cube-webgpu-usl-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UINT;
  textureInfo.width            = CUBE_SIZE;
  textureInfo.height           = CUBE_SIZE;
  textureInfo.depthOrLayers    = CUBE_FACE_COUNT;
  textureInfo.mipLevelCount    = CUBE_MIP_COUNT;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->texture) != GPU_OK) {
    set_status("GPU: failed to create the integer cube texture", 1);
    return 0;
  }

  write.aspect     = GPU_TEXTURE_ASPECT_ALL;
  write.depth      = 1u;
  write.layerCount = 1u;
  for (uint32_t mip = 0u; mip < CUBE_MIP_COUNT; mip++) {
    uint32_t mipSize;

    mipSize            = CUBE_SIZE >> mip;
    write.mipLevel     = mip;
    write.width        = mipSize;
    write.height       = mipSize;
    write.bytesPerRow  = mipSize * CUBE_PIXEL_SIZE;
    write.rowsPerImage = mipSize;
    for (uint32_t face = 0u; face < CUBE_FACE_COUNT; face++) {
      uint64_t uploadSize;

      fill_cube_face(pixels, mipSize, face, mip);
      uploadSize                = (uint64_t)write.bytesPerRow * mipSize;
      write.baseArrayLayer      = face;
      if (GPUQueueWriteTexture(state->queue,
                               state->texture,
                               &write,
                               pixels,
                               uploadSize) != GPU_OK) {
        set_status("GPU: failed to upload an integer cube face", 1);
        return 0;
      }
    }
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "integer-cube-webgpu-usl-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_CUBE;
  viewInfo.format           = GPU_FORMAT_RGBA8_UINT;
  viewInfo.mipLevelCount    = CUBE_MIP_COUNT;
  viewInfo.arrayLayerCount  = CUBE_FACE_COUNT;
  if (GPUCreateTextureView(state->texture,
                           &viewInfo,
                           &state->view) != GPU_OK) {
    set_status("GPU: failed to create the integer cube view", 1);
    return 0;
  }

  entry.textureView = state->view;
  entry.binding     = 0u;
  entry.bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "integer-cube-webgpu-usl-group0";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &entry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK) {
    set_status("GPU: failed to create the integer cube bind group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUIntegerCube             *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPURenderPassColorAttachment   color    = {0};
  GPURenderPassCreateInfo        passInfo = {0};
  GPUViewport                    viewport = {0};

  state = userData;
  if (!resize_canvas(state)) return;

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) return;

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "integer-cube-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.018f;
  color.clearColor.float32[2] = 0.045f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "integer-cube-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  viewport.width    = (float)state->width / (float)PANEL_COUNT;
  viewport.height   = (float)state->height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  for (uint32_t i = 0u; i < PANEL_COUNT; i++) {
    viewport.x = viewport.width * (float)i;
    GPUBindRenderPipeline(pass, state->pipelines[i]);
    GPUBindRenderGroup(pass, 0u, state->bindGroup, 0u, NULL);
    GPUSetViewport(pass, &viewport);
    GPUDraw(pass, 6u, 1u, 0u, 0u);
  }
  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU integer-cube frame\n");
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
  WebGPUIntegerCube *state;
  GPURuntimeConfig   runtime = {0};

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
  if (GPUSetDeviceErrorCallback(device, device_error, state) != GPU_OK) {
    set_status("GPU: failed to install the WebGPU error callback", 1);
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
    set_status("GPU: failed to initialize integer-cube resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL integer cube ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "integer-cube-webgpu-usl";
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
