#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <gpu/gpu.h>

#include "../common/webgpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct TriangleVertex {
  float position[2];
} TriangleVertex;

typedef struct InstanceData {
  float offset[2];
  float color[4];
} InstanceData;

typedef struct DrawUniformBlock {
  float   transform[4];
  float   tint[4];
  uint8_t padding[224];
} DrawUniformBlock;

typedef struct WebGPUInstancing {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBindGroup      *drawGroup;
  GPUBuffer         *vertexBuffer;
  GPUBuffer         *instanceBuffer;
  GPUBuffer         *uniformBuffer;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUInstancing;

enum {
  INSTANCE_COUNT       = 4u,
  UNIFORM_DATA_SIZE    = 32u,
  UNIFORM_STRIDE       = 256u,
  WARM_FRAME_COUNT     = 8u
};

_Static_assert(sizeof(DrawUniformBlock) == UNIFORM_STRIDE,
               "dynamic uniform stride must remain 256 bytes");

static const TriangleVertex kTriangleVertices[] = {
  {{-0.13f, -0.12f}},
  {{ 0.13f, -0.12f}},
  {{ 0.00f,  0.15f}}
};

static const InstanceData kInstances[] = {
  {{-0.16f, -0.19f}, {1.00f, 0.30f, 0.12f, 1.0f}},
  {{ 0.16f, -0.19f}, {1.00f, 0.72f, 0.10f, 1.0f}},
  {{-0.16f,  0.19f}, {0.98f, 0.18f, 0.48f, 1.0f}},
  {{ 0.16f,  0.19f}, {0.78f, 0.36f, 1.00f, 1.0f}},
  {{-0.16f, -0.19f}, {0.18f, 0.72f, 1.00f, 1.0f}},
  {{ 0.16f, -0.19f}, {0.14f, 1.00f, 0.70f, 1.0f}},
  {{-0.16f,  0.19f}, {0.30f, 0.48f, 1.00f, 1.0f}},
  {{ 0.16f,  0.19f}, {0.10f, 0.90f, 1.00f, 1.0f}}
};

static const DrawUniformBlock kDrawUniforms[] = {
  {{-0.43f, 0.00f, 1.00f, 0.0f}, {1.00f, 0.82f, 0.60f, 1.0f}, {0}},
  {{ 0.43f, 0.00f, 0.88f, 0.0f}, {0.66f, 0.92f, 1.00f, 1.0f}, {0}}
};

static WebGPUInstancing app;

static int
resize_canvas(WebGPUInstancing *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
create_pipeline(WebGPUInstancing *state) {
  GPUVertexAttribute          vertexAttribute = {0};
  GPUVertexAttribute          instanceAttributes[2] = {0};
  GPUVertexBufferLayout       vertexLayouts[2] = {0};
  GPUColorTargetState         color = {0};
  GPURenderPipelineCreateInfo info = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  void                       *artifact;
  uint64_t                    artifactSize;
  uint32_t                    layoutEntryCount;
  GPUResult                   result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/instancing.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /instancing.us", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the instancing artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected instancing shader reflection", 1);
    return 0;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      !layoutEntries[0].hasDynamicOffset) {
    set_status("GPU: USL dynamic-offset reflection mismatch", 1);
    return 0;
  }

  vertexAttribute.format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  vertexAttribute.offset          = offsetof(TriangleVertex, position);
  vertexAttribute.shaderLocation  = 0u;
  vertexLayouts[0].pAttributes    = &vertexAttribute;
  vertexLayouts[0].strideBytes    = sizeof(TriangleVertex);
  vertexLayouts[0].stepMode       = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayouts[0].attributeCount = 1u;

  instanceAttributes[0].format         = GPU_VERTEX_FORMAT_FLOAT32X2;
  instanceAttributes[0].offset         = offsetof(InstanceData, offset);
  instanceAttributes[0].shaderLocation = 1u;
  instanceAttributes[1].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
  instanceAttributes[1].offset         = offsetof(InstanceData, color);
  instanceAttributes[1].shaderLocation = 2u;
  vertexLayouts[1].pAttributes         = instanceAttributes;
  vertexLayouts[1].strideBytes         = sizeof(InstanceData);
  vertexLayouts[1].stepMode            = GPU_VERTEX_STEP_MODE_INSTANCE;
  vertexLayouts[1].attributeCount      = 2u;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "instancing-webgpu-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "instanced_vs";
  info.fragmentEntry            = "instanced_fs";
  info.pColorTargets            = &color;
  info.vertex.pBufferLayouts    = vertexLayouts;
  info.vertex.bufferLayoutCount = 2u;
  info.colorTargetCount         = 1u;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_NONE;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create the instancing pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUInstancing *state) {
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUBindGroupEntry      groupEntry = {0};
  GPUBindGroupCreateInfo groupInfo  = {0};

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "instancing-webgpu-vertices";
  bufferInfo.sizeBytes        = sizeof(kTriangleVertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          kTriangleVertices,
                          sizeof(kTriangleVertices)) != GPU_OK) {
    set_status("GPU: failed to upload instancing vertices", 1);
    return 0;
  }

  bufferInfo.label     = "instancing-webgpu-instances";
  bufferInfo.sizeBytes = sizeof(kInstances);
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->instanceBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->instanceBuffer,
                          0u,
                          kInstances,
                          sizeof(kInstances)) != GPU_OK) {
    set_status("GPU: failed to upload instance data", 1);
    return 0;
  }

  bufferInfo.label     = "instancing-webgpu-dynamic-uniforms";
  bufferInfo.sizeBytes = sizeof(kDrawUniforms);
  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->uniformBuffer,
                          0u,
                          kDrawUniforms,
                          sizeof(kDrawUniforms)) != GPU_OK) {
    set_status("GPU: failed to upload dynamic uniforms", 1);
    return 0;
  }

  groupEntry.buffer.buffer = state->uniformBuffer;
  groupEntry.buffer.size   = UNIFORM_DATA_SIZE;
  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label             = "instancing-webgpu-group0";
  groupInfo.layout            = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries          = &groupEntry;
  groupInfo.entryCount        = 1u;
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->drawGroup) != GPU_OK ||
      !state->drawGroup) {
    set_status("GPU: failed to create the dynamic bind group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUInstancing              *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPUBufferBinding               vertexBuffers[2] = {0};
  GPURenderPassColorAttachment   color = {0};
  GPURenderPassCreateInfo        passInfo = {0};
  uint32_t                       dynamicOffset;

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
                              "instancing-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.015f;
  color.clearColor.float32[1] = 0.035f;
  color.clearColor.float32[2] = 0.085f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "instancing-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  vertexBuffers[0].buffer = state->vertexBuffer;
  vertexBuffers[1].buffer = state->instanceBuffer;
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindVertexBuffers(pass, 0u, 2u, vertexBuffers);

  dynamicOffset = 0u;
  GPUBindRenderGroup(pass, 0u, state->drawGroup, 1u, &dynamicOffset);
  GPUDraw(pass, 3u, INSTANCE_COUNT, 0u, 0u);

  dynamicOffset = UNIFORM_STRIDE;
  GPUBindRenderGroup(pass, 0u, state->drawGroup, 1u, &dynamicOffset);
  GPUDraw(pass, 3u, INSTANCE_COUNT, 0u, INSTANCE_COUNT);

  GPUEndRenderPass(pass);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the instancing frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.hotPathAllocCount != 0u || stats.hotPathFreeCount != 0u)) {
      set_status("GPU: warm instancing frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  WebGPUInstancing *state;
  GPURuntimeConfig  runtime = {0};

  state = userData;
  if (result != GPU_OK || !device) {
    set_status("GPU: failed to request WebGPU device", 1);
    return;
  }

  state->device = device;
  state->queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
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
  if (!state->swapchain || !create_pipeline(state) ||
      !create_resources(state)) {
    set_status("GPU: failed to initialize instancing resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL instancing ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  WebGPUInstancing *state;

  state = userData;
  if (result != GPU_OK || !adapter) {
    set_status("GPU: failed to request WebGPU adapter", 1);
    return;
  }

  state->adapter = adapter;
  set_status("GPU: WebGPU adapter ready", 0);
  result = GPURequestDevice(adapter, NULL, device_ready, state);
  if (result != GPU_OK) {
    set_status("GPU: failed to start WebGPU device request", 1);
  }
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "instancing-webgpu-usl";
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  result = GPUCreateInstance(&info, &app.instance);
  if (result != GPU_OK || !app.instance) {
    set_status("GPU: failed to create WebGPU instance", 1);
    return 1;
  }

  set_status("GPU: requesting WebGPU adapter", 0);
  result = GPURequestAdapter(app.instance, adapter_ready, &app);
  if (result != GPU_OK) {
    set_status("GPU: failed to start WebGPU adapter request", 1);
    return 1;
  }
  return 0;
}
