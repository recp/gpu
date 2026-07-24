#include "../common/webgpu.h"

#include <math.h>
#include <stdio.h>

#ifndef GPU_COMPUTE_ARTIFACT_PATH
#define GPU_COMPUTE_ARTIFACT_PATH "/compute.us"
#endif

#ifndef GPU_COMPUTE_ENTRY_POINT
#define GPU_COMPUTE_ENTRY_POINT "fill_vertices"
#endif

#ifndef GPU_COMPUTE_DISPATCH_X
#define GPU_COMPUTE_DISPATCH_X 3u
#endif

#ifndef GPU_COMPUTE_VERTEX_CAPACITY
#define GPU_COMPUTE_VERTEX_CAPACITY 3u
#endif

#ifndef GPU_COMPUTE_USE_INDIRECT
#define GPU_COMPUTE_USE_INDIRECT 0
#endif

#ifndef GPU_COMPUTE_USE_TIMESTAMPS
#define GPU_COMPUTE_USE_TIMESTAMPS 0
#endif

#ifndef GPU_COMPUTE_REQUIRED_FEATURE
#define GPU_COMPUTE_REQUIRED_FEATURE GPU_FEATURE_COMPUTE
#endif

#ifndef GPU_COMPUTE_UNSUPPORTED_STATUS
#define GPU_COMPUTE_UNSUPPORTED_STATUS \
  "GPU: required WebGPU compute feature unsupported by this adapter"
#endif

#ifndef GPU_COMPUTE_READY_STATUS
#define GPU_COMPUTE_READY_STATUS \
  "GPU: WebGPU USL compute push constants ready"
#endif

#ifndef GPU_COMPUTE_TIMESTAMP_RESOLVED_STATUS
#define GPU_COMPUTE_TIMESTAMP_RESOLVED_STATUS \
  "GPU: WebGPU USL timestamp query resolved"
#endif

#if GPU_COMPUTE_USE_TIMESTAMPS
enum {
  GPU_COMPUTE_TIMESTAMP_QUERY_COUNT    = 4u,
  GPU_COMPUTE_TIMESTAMP_BINDING_OFFSET = 256u,
  GPU_COMPUTE_TIMESTAMP_RESOLVE_OFFSET =
    GPU_COMPUTE_TIMESTAMP_BINDING_OFFSET + sizeof(uint64_t)
};
#endif

typedef struct GeneratedVertex {
  float position[4];
  float color[4];
} GeneratedVertex;

typedef struct ComputeConstants {
  float tint[4];
} ComputeConstants;

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
  GPUPipelineCache   *pipelineCache;
  GPUComputePipeline *computePipeline;
  GPURenderPipeline  *renderPipeline;
  GPUBuffer          *vertexBuffer;
  GPUBuffer          *dispatchBuffer;
#if GPU_COMPUTE_USE_TIMESTAMPS
  GPUBuffer          *timestampBuffer;
  GPUQuerySet        *timestampQuery;
#endif
  GPUBindGroup       *computeGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
#if GPU_COMPUTE_USE_TIMESTAMPS
  bool                timestampRecorded;
#endif
} WebGPUCompute;

static WebGPUCompute app;

static int
resize_canvas(WebGPUCompute *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
create_resources(WebGPUCompute *state) {
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPUPipelineCacheCreateInfo   cacheInfo = {0};
  GPURenderPipelineCreateInfo  renderInfo = {0};
  GPUShaderReflection          reflection = {0};
  GPUVertexAttribute           attributes[2] = {0};
  GPUVertexBufferLayout        vertexLayout = {0};
  GPUColorTargetState          color = {0};
  GPUPipelineLayoutCreateInfo  renderLayoutInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
#if GPU_COMPUTE_USE_TIMESTAMPS
  GPUQuerySetCreateInfo         queryInfo = {0};
#endif
  GPUBindGroupEntry             groupEntries[2] = {0};
  GPUBindGroupCreateInfo        groupInfo = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUComputePipeline            *cachedPipeline;
  GPUCacheStats                  stats;
  void                         *artifact;
  uint64_t                      artifactSize;
  uint32_t                      layoutEntryCount;
  GPUResult                     result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file(GPU_COMPUTE_ARTIFACT_PATH, &artifact, &artifactSize)) {
    set_status("GPU: failed to read compute artifact", 1);
    return 0;
  }

  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library ||
      GPUGetShaderReflection(state->library, &reflection) != GPU_OK ||
      reflection.pushConstantSizeBytes != sizeof(ComputeConstants) ||
      reflection.pushConstantStages != GPU_SHADER_STAGE_COMPUTE_BIT) {
    GPUFreeShaderReflection(&reflection);
    set_status("GPU: unexpected compute push-constant reflection", 1);
    return 0;
  }
  GPUFreeShaderReflection(&reflection);

  if (GPUCreateShaderLayout(state->device,
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
  if (!layoutEntries ||
      layoutEntryCount != 1u + GPU_COMPUTE_USE_TIMESTAMPS ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT) {
    set_status("GPU: unexpected compute storage reflection", 1);
    return 0;
  }
#if GPU_COMPUTE_USE_TIMESTAMPS
  if (layoutEntries[1].binding != 1u ||
      layoutEntries[1].bindingType != GPU_BINDING_READ_ONLY_STORAGE_BUFFER ||
      layoutEntries[1].visibility != GPU_SHADER_STAGE_COMPUTE_BIT) {
    set_status("GPU: unexpected timestamp storage reflection", 1);
    return 0;
  }
#endif

  cacheInfo.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  cacheInfo.chain.structSize = sizeof(cacheInfo);
  cacheInfo.label            = "compute-webgpu-usl-cache";
  if (GPUCreatePipelineCache(state->device,
                             &cacheInfo,
                             &state->pipelineCache) != GPU_OK ||
      !state->pipelineCache) {
    set_status("GPU: failed to create WebGPU pipeline cache", 1);
    return 0;
  }

  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "compute-webgpu-usl-fill";
  computeInfo.layout           = state->shaderLayout->pipelineLayout;
  computeInfo.cache            = state->pipelineCache;
  computeInfo.library          = state->library;
  computeInfo.entryPoint       = GPU_COMPUTE_ENTRY_POINT;
  GPUResetStats(state->device);
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &state->computePipeline) != GPU_OK) {
    set_status("GPU: failed to create WebGPU compute pipeline", 1);
    return 0;
  }

  cachedPipeline = NULL;
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &cachedPipeline) != GPU_OK ||
      !cachedPipeline ||
      GPUGetCacheStats(state->device, &stats) != GPU_OK ||
      stats.pipelineCompiles != 1u ||
      stats.pipelineMisses != 1u ||
      stats.pipelineHits != 1u) {
    GPUDestroyComputePipeline(cachedPipeline);
    set_status("GPU: WebGPU compute pipeline cache check failed", 1);
    return 0;
  }
  GPUDestroyComputePipeline(cachedPipeline);

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
  bufferInfo.sizeBytes        = sizeof(GeneratedVertex) *
                                GPU_COMPUTE_VERTEX_CAPACITY;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_VERTEX;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->vertexBuffer) != GPU_OK) {
    set_status("GPU: failed to create WebGPU storage buffer", 1);
    return 0;
  }

#if GPU_COMPUTE_USE_INDIRECT
  {
    static const uint32_t dispatchArgs[] = {
      GPU_COMPUTE_DISPATCH_X, 1u, 1u
    };

    bufferInfo.label     = "compute-webgpu-indirect-args";
    bufferInfo.sizeBytes = sizeof(dispatchArgs);
    bufferInfo.usage     = GPU_BUFFER_USAGE_INDIRECT |
                           GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(state->device,
                        &bufferInfo,
                        &state->dispatchBuffer) != GPU_OK ||
        GPUQueueWriteBuffer(state->queue,
                            state->dispatchBuffer,
                            0u,
                            dispatchArgs,
                            sizeof(dispatchArgs)) != GPU_OK) {
      set_status("GPU: failed to create WebGPU indirect dispatch buffer", 1);
      return 0;
    }
  }
#endif

#if GPU_COMPUTE_USE_TIMESTAMPS
  {
    static const uint64_t timestampSentinel[
      GPU_COMPUTE_TIMESTAMP_QUERY_COUNT + 1u
    ] = {
      UINT64_MAX,
      UINT64_MAX,
      UINT64_MAX,
      UINT64_MAX,
      UINT64_MAX
    };
    double                timestampPeriod;

    queryInfo.chain.sType      = GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO;
    queryInfo.chain.structSize = sizeof(queryInfo);
    queryInfo.label            = "compute-webgpu-timestamps";
    queryInfo.type             = GPU_QUERY_TIMESTAMP;
    queryInfo.count            = GPU_COMPUTE_TIMESTAMP_QUERY_COUNT;
    bufferInfo.label           = "compute-webgpu-timestamp-results";
    bufferInfo.sizeBytes       = GPU_COMPUTE_TIMESTAMP_RESOLVE_OFFSET +
                                 GPU_COMPUTE_TIMESTAMP_QUERY_COUNT *
                                   sizeof(uint64_t);
    bufferInfo.usage           = GPU_BUFFER_USAGE_COPY_DST |
                                 GPU_BUFFER_USAGE_STORAGE;
    if (GPUCreateQuerySet(state->device,
                          &queryInfo,
                          &state->timestampQuery) != GPU_OK ||
        GPUCreateBuffer(state->device,
                        &bufferInfo,
                        &state->timestampBuffer) != GPU_OK ||
        GPUGetTimestampPeriod(state->queue, &timestampPeriod) != GPU_OK ||
        GPUQueueWriteBuffer(state->queue,
                            state->timestampBuffer,
                            GPU_COMPUTE_TIMESTAMP_BINDING_OFFSET,
                            timestampSentinel,
                            sizeof(timestampSentinel)) != GPU_OK ||
        timestampPeriod != 1.0) {
      set_status("GPU: failed to initialize WebGPU timestamps", 1);
      return 0;
    }
  }
#endif

  groupEntries[0].binding       = 0u;
  groupEntries[0].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntries[0].buffer.buffer = state->vertexBuffer;
  groupEntries[0].buffer.size   = sizeof(GeneratedVertex) *
                                  GPU_COMPUTE_VERTEX_CAPACITY;
#if GPU_COMPUTE_USE_TIMESTAMPS
  groupEntries[1].binding       = 1u;
  groupEntries[1].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  groupEntries[1].buffer.buffer = state->timestampBuffer;
  groupEntries[1].buffer.offset = GPU_COMPUTE_TIMESTAMP_BINDING_OFFSET;
  groupEntries[1].buffer.size   = sizeof(uint64_t) +
                                  GPU_COMPUTE_TIMESTAMP_QUERY_COUNT *
                                    sizeof(uint64_t);
#endif
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label             = "compute-webgpu-usl-group0";
  groupInfo.layout            = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries          = groupEntries;
  groupInfo.entryCount        = 1u + GPU_COMPUTE_USE_TIMESTAMPS;
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
#if GPU_COMPUTE_USE_TIMESTAMPS
  GPUComputePassCreateInfo     computeInfo = {0};
  GPUPassTimestampWrites       computeTimestamps = {0};
  GPUPassTimestampWrites       renderTimestamps = {0};
#endif
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPUBufferBinding             vertex = {0};
  GPUBufferBarrier             barrier = {0};
  GPUBarrierBatch              barriers = {0};
  ComputeConstants             constants;
#if !GPU_COMPUTE_USE_TIMESTAMPS
  float                        phase;
#endif

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

#if GPU_COMPUTE_USE_TIMESTAMPS
  if (!state->timestampRecorded) {
    computeTimestamps.querySet   = state->timestampQuery;
    computeTimestamps.beginIndex = 0u;
    computeTimestamps.endIndex   = 1u;
    computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PASS_CREATE_INFO;
    computeInfo.chain.structSize = sizeof(computeInfo);
    computeInfo.label            = "compute-webgpu-fill";
    computeInfo.timestampWrites  = &computeTimestamps;
    compute = GPUBeginComputePassWithInfo(cmdb, &computeInfo);
  } else {
    compute = GPUBeginComputePass(cmdb, "compute-webgpu-fill");
  }
#else
  compute = GPUBeginComputePass(cmdb, "compute-webgpu-fill");
#endif
  if (!compute) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindComputePipeline(compute, state->computePipeline);
  GPUBindComputeGroup(compute, 0u, state->computeGroup, 0u, NULL);
#if GPU_COMPUTE_USE_TIMESTAMPS
  constants.tint[0] = 1.0f;
  constants.tint[1] = 1.0f;
  constants.tint[2] = 1.0f;
#else
  phase             = (float)(emscripten_get_now() * 0.001);
  constants.tint[0] = 0.60f + 0.40f * sinf(phase);
  constants.tint[1] = 0.60f + 0.40f * sinf(phase + 2.0943951f);
  constants.tint[2] = 0.60f + 0.40f * sinf(phase + 4.1887902f);
#endif
  constants.tint[3] = 1.0f;
  GPUSetComputePushConstants(compute,
                             0u,
                             (uint32_t)sizeof(constants),
                             &constants);
#if GPU_COMPUTE_USE_INDIRECT
  GPUDispatchIndirect(compute, state->dispatchBuffer, 0u);
#else
  GPUDispatch(compute, GPU_COMPUTE_DISPATCH_X, 1u, 1u);
#endif
  GPUEndComputePass(compute);

  barrier.buffer    = state->vertexBuffer;
  barrier.srcAccess = GPU_ACCESS_SHADER_WRITE;
  barrier.dstAccess = GPU_ACCESS_SHADER_READ;
  barrier.sizeBytes = sizeof(GeneratedVertex) *
                      GPU_COMPUTE_VERTEX_CAPACITY;
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
#if GPU_COMPUTE_USE_TIMESTAMPS
  if (!state->timestampRecorded) {
    renderTimestamps.querySet   = state->timestampQuery;
    renderTimestamps.beginIndex = 2u;
    renderTimestamps.endIndex   = 3u;
    passInfo.timestampWrites    = &renderTimestamps;
  }
#endif
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
#if GPU_COMPUTE_USE_TIMESTAMPS
  if (!state->timestampRecorded) {
    GPUResolveQuerySet(cmdb,
                       state->timestampQuery,
                       0u,
                       GPU_COMPUTE_TIMESTAMP_QUERY_COUNT,
                       state->timestampBuffer,
                       GPU_COMPUTE_TIMESTAMP_RESOLVE_OFFSET);
  }
#endif
  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    fprintf(stderr, "GPU: failed to finish WebGPU compute frame\n");
    return;
  }
#if GPU_COMPUTE_USE_TIMESTAMPS
  if (!state->timestampRecorded) {
    state->timestampRecorded = true;
    set_status(GPU_COMPUTE_TIMESTAMP_RESOLVED_STATUS, 0);
  }
#endif
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUCompute *state;

  state = userData;
  if (result != GPU_OK || !adapter || !device) {
    set_status(!adapter ? "GPU: failed to request WebGPU adapter"
                        : "GPU: failed to request WebGPU device",
               1);
    return;
  }

  state->adapter = adapter;
  state->device  = device;
  if (!GPUIsFeatureEnabled(device, GPU_COMPUTE_REQUIRED_FEATURE)) {
    set_status(GPU_COMPUTE_UNSUPPORTED_STATUS, 1);
    return;
  }
  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
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

  set_status(GPU_COMPUTE_READY_STATUS, 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
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
