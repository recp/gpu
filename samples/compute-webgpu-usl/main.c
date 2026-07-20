#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <gpu/gpu.h>

#include "../common/webgpu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct GeneratedVertex {
  float position[4];
  float color[4];
} GeneratedVertex;

typedef struct WebGPUCompute {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSurface         *surface;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPUPipelineLayout  *renderLayout;
  GPUComputePipeline *computePipeline;
  GPURenderPipeline  *renderPipeline;
  GPUBuffer          *vertexBuffer;
  GPUBindGroup       *computeGroup;
  uint32_t            width;
  uint32_t            height;
} WebGPUCompute;

static WebGPUCompute app;

static int
resize_canvas(WebGPUCompute *state) {
  double   cssWidth;
  double   cssHeight;
  double   scale;
  uint32_t width;
  uint32_t height;

  if (emscripten_get_element_css_size("#canvas", &cssWidth, &cssHeight) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    return 0;
  }

  scale  = emscripten_get_device_pixel_ratio();
  width  = (uint32_t)(cssWidth * scale);
  height = (uint32_t)(cssHeight * scale);
  if (width == 0u || height == 0u) {
    return 0;
  }
  if (width == state->width && height == state->height) {
    return 1;
  }

  emscripten_set_canvas_element_size("#canvas", (int)width, (int)height);
  if (state->swapchain &&
      GPUResizeSwapchain(state->swapchain, width, height) != GPU_OK) {
    return 0;
  }
  state->width  = width;
  state->height = height;
  return 1;
}

static int
create_resources(WebGPUCompute *state) {
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPURenderPipelineCreateInfo  renderInfo = {0};
  GPUVertexAttribute           attributes[2] = {0};
  GPUVertexBufferLayout        vertexLayout = {0};
  GPUColorTargetState          color = {0};
  GPUPipelineLayoutCreateInfo  renderLayoutInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntry = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  void                         *artifact;
  uint64_t                      artifactSize;
  uint32_t                      layoutEntryCount;
  GPUResult                     result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/compute.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /compute.us", 1);
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
    set_status("GPU: failed to create compute shader layout", 1);
    return 0;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT) {
    set_status("GPU: unexpected compute storage reflection", 1);
    return 0;
  }

  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "compute-webgpu-usl-fill";
  computeInfo.layout           = state->shaderLayout->pipelineLayout;
  computeInfo.library          = state->library;
  computeInfo.entryPoint       = "fill_vertices";
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &state->computePipeline) != GPU_OK) {
    set_status("GPU: failed to create WebGPU compute pipeline", 1);
    return 0;
  }

  renderLayoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  renderLayoutInfo.chain.structSize = sizeof(renderLayoutInfo);
  renderLayoutInfo.label            = "compute-webgpu-usl-render-layout";
  if (GPUCreatePipelineLayout(state->device,
                              &renderLayoutInfo,
                              &state->renderLayout) != GPU_OK) {
    set_status("GPU: failed to create WebGPU render layout", 1);
    return 0;
  }

  attributes[0].shaderLocation = 0u;
  attributes[0].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[0].offset         = offsetof(GeneratedVertex, position);
  attributes[1].shaderLocation = 1u;
  attributes[1].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[1].offset         = offsetof(GeneratedVertex, color);
  vertexLayout.strideBytes     = sizeof(GeneratedVertex);
  vertexLayout.stepMode        = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.pAttributes     = attributes;
  vertexLayout.attributeCount  = 2u;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  renderInfo.chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize = sizeof(renderInfo);
  renderInfo.label            = "compute-webgpu-usl-render";
  renderInfo.layout           = state->renderLayout;
  renderInfo.library          = state->library;
  renderInfo.vertexEntry      = "tri_vs";
  renderInfo.fragmentEntry    = "tri_fs";
  renderInfo.vertex.pBufferLayouts = &vertexLayout;
  renderInfo.vertex.bufferLayoutCount = 1u;
  renderInfo.pColorTargets       = &color;
  renderInfo.colorTargetCount    = 1u;
  renderInfo.primitiveTopology   = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderInfo.cullMode            = GPU_CULL_MODE_NONE;
  renderInfo.frontFace           = GPU_FRONT_FACE_CCW;
  renderInfo.multisample.sampleCount = 1u;
  renderInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &renderInfo,
                              &state->renderPipeline) != GPU_OK) {
    set_status("GPU: failed to create WebGPU render pipeline", 1);
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "compute-webgpu-usl-vertices";
  bufferInfo.sizeBytes        = sizeof(GeneratedVertex) * 3u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_VERTEX;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->vertexBuffer) != GPU_OK) {
    set_status("GPU: failed to create WebGPU storage buffer", 1);
    return 0;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = state->vertexBuffer;
  groupEntry.buffer.size   = bufferInfo.sizeBytes;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label             = "compute-webgpu-usl-group0";
  groupInfo.layout            = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries          = &groupEntry;
  groupInfo.entryCount        = 1u;
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->computeGroup) != GPU_OK) {
    set_status("GPU: failed to create WebGPU compute group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUCompute               *state;
  GPUFrame                    *frame;
  GPUCommandBuffer            *cmdb;
  GPUComputePassEncoder       *compute;
  GPURenderPassEncoder        *render;
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPUBufferBinding             vertex = {0};
  GPUBufferBarrier             barrier = {0};
  GPUBarrierBatch              barriers = {0};

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
                              "compute-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  compute = GPUBeginComputePass(cmdb, "compute-webgpu-fill");
  if (!compute) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindComputePipeline(compute, state->computePipeline);
  GPUBindComputeGroup(compute, 0u, state->computeGroup, 0u, NULL);
  GPUDispatch(compute, 3u, 1u, 1u);
  GPUEndComputePass(compute);

  barrier.buffer    = state->vertexBuffer;
  barrier.srcAccess = GPU_ACCESS_SHADER_WRITE;
  barrier.dstAccess = GPU_ACCESS_SHADER_READ;
  barrier.sizeBytes = sizeof(GeneratedVertex) * 3u;
  barriers.srcStages          = GPU_STAGE_COMPUTE;
  barriers.dstStages          = GPU_STAGE_VERTEX;
  barriers.pBufferBarriers    = &barrier;
  barriers.bufferBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &barriers);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.015f;
  color.clearColor.float32[1] = 0.035f;
  color.clearColor.float32[2] = 0.085f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "compute-webgpu-render";
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
  GPUDraw(render, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(render);
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU compute frame\n");
  }
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  WebGPUCompute *state;

  state = userData;
  if (result != GPU_OK || !device) {
    set_status("GPU: failed to request WebGPU device", 1);
    return;
  }

  state->device = device;
  state->queue  = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
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
  if (!state->swapchain || !create_resources(state)) {
    return;
  }

  set_status("GPU: WebGPU USL compute/storage ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  WebGPUCompute *state;

  state = userData;
  if (result != GPU_OK || !adapter) {
    set_status("GPU: failed to request WebGPU adapter", 1);
    return;
  }

  state->adapter = adapter;
  set_status("GPU: WebGPU adapter ready", 0);
  result = GPURequestDevice(adapter, NULL, device_ready, state);
  if (result != GPU_OK) {
    fprintf(stderr, "GPU: failed to start WebGPU device request (%d)\n", result);
  }
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "compute-webgpu-usl";
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
    fprintf(stderr, "GPU: failed to start WebGPU adapter request (%d)\n", result);
    return 1;
  }
  return 0;
}
