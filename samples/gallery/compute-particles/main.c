#include "../../common/sample_platform.h"

#include <math.h>
#include <stdio.h>

enum {
  PARTICLE_COUNT = 256u,
  PARTICLE_WORKGROUP_SIZE = 64u,
  WARM_FRAME_COUNT = 8u
};

typedef struct Particle {
  float position[2];
  float velocity[2];
  float color[4];
} Particle;

typedef struct Simulation {
  float    deltaTime;
  float    time;
  float    aspect;
  uint32_t count;
} Simulation;

typedef struct WebGPUParticles {
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
  GPUBuffer          *particleBuffer;
  GPUBuffer          *drawBuffer;
  GPUBindGroup       *computeGroup;
  GPUBindGroup       *renderGroup;
  WebGPURequest       request;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
  double              previousTime;
} WebGPUParticles;

static WebGPUParticles app;

static uint32_t
particle_hash(uint32_t value) {
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  value *= 0x846ca68bu;
  return value ^ (value >> 16u);
}

static float
particle_unit(uint32_t value) {
  return (float)(particle_hash(value) & 0x00ffffffu) / 16777215.0f;
}

static int
resize_canvas(WebGPUParticles *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
create_resources(WebGPUParticles *state) {
  static const uint32_t drawArgs[] = {
    6u, PARTICLE_COUNT, 0u, 0u
  };
  GPUComputePipelineCreateInfo computeInfo = {0};
  GPURenderPipelineCreateInfo  renderInfo = {0};
  GPUColorTargetState          color = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntry = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUShaderReflection          reflection = {0};
  Particle                     particles[PARTICLE_COUNT];
  void                        *artifact;
  uint64_t                     artifactSize;
  uint32_t                     i;
  GPUResult                    result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/particles.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read particles.us", 1);
    return 0;
  }
  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library ||
      GPUGetShaderReflection(state->library, &reflection) != GPU_OK ||
      reflection.pushConstantSizeBytes != sizeof(Simulation) ||
      reflection.pushConstantStages !=
        (GPU_SHADER_STAGE_VERTEX_BIT | GPU_SHADER_STAGE_COMPUTE_BIT)) {
    GPUFreeShaderReflection(&reflection);
    set_status("GPU: unexpected particles reflection", 1);
    return 0;
  }
  GPUFreeShaderReflection(&reflection);

  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 2u) {
    set_status("GPU: failed to create particles shader layout", 1);
    return 0;
  }

  computeInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  computeInfo.chain.structSize = sizeof(computeInfo);
  computeInfo.label            = "webgpu-particle-simulation";
  computeInfo.layout           = state->shaderLayout->pipelineLayout;
  computeInfo.library          = state->library;
  computeInfo.entryPoint       = "simulate_particles";
  if (GPUCreateComputePipeline(state->device,
                               &computeInfo,
                               &state->computePipeline) != GPU_OK) {
    set_status("GPU: failed to create particle compute pipeline", 1);
    return 0;
  }

  color.format                 = GPUGetSwapchainFormat(state->swapchain);
  color.blend.enabled          = true;
  color.blend.color.srcFactor  = GPU_BLEND_FACTOR_SRC_ALPHA;
  color.blend.color.dstFactor  = GPU_BLEND_FACTOR_ONE;
  color.blend.color.op         = GPU_BLEND_OP_ADD;
  color.blend.alpha.srcFactor  = GPU_BLEND_FACTOR_ONE;
  color.blend.alpha.dstFactor  = GPU_BLEND_FACTOR_ONE;
  color.blend.alpha.op         = GPU_BLEND_OP_ADD;
  color.blend.writeMask        = GPU_COLOR_WRITE_ALL;
  renderInfo.chain.sType       = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  renderInfo.chain.structSize  = sizeof(renderInfo);
  renderInfo.label             = "webgpu-particle-render";
  renderInfo.layout            = state->shaderLayout->pipelineLayout;
  renderInfo.library           = state->library;
  renderInfo.vertexEntry       = "particle_vs";
  renderInfo.fragmentEntry     = "particle_fs";
  renderInfo.pColorTargets     = &color;
  renderInfo.colorTargetCount  = 1u;
  renderInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderInfo.cullMode          = GPU_CULL_MODE_NONE;
  renderInfo.frontFace         = GPU_FRONT_FACE_CCW;
  renderInfo.multisample.sampleCount = 1u;
  renderInfo.multisample.sampleMask  = UINT32_MAX;
  if (GPUCreateRenderPipeline(state->device,
                              &renderInfo,
                              &state->renderPipeline) != GPU_OK) {
    set_status("GPU: failed to create particle render pipeline", 1);
    return 0;
  }

  for (i = 0u; i < PARTICLE_COUNT; i++) {
    float angle;
    float speed;
    float radius;

    angle  = particle_unit(i * 7u + 1u) * 6.28318530718f;
    radius = 0.08f + 0.82f * particle_unit(i * 7u + 2u);
    speed  = 0.08f + 0.22f * particle_unit(i * 7u + 3u);
    particles[i].position[0] = cosf(angle) * radius;
    particles[i].position[1] = sinf(angle) * radius;
    particles[i].velocity[0] = -sinf(angle) * speed;
    particles[i].velocity[1] =  cosf(angle) * speed;
    particles[i].color[0]    = 0.25f + 0.75f * particle_unit(i * 7u + 4u);
    particles[i].color[1]    = 0.25f + 0.75f * particle_unit(i * 7u + 5u);
    particles[i].color[2]    = 0.35f + 0.65f * particle_unit(i * 7u + 6u);
    particles[i].color[3]    = particle_unit(i * 7u + 7u);
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "webgpu-particles";
  bufferInfo.sizeBytes        = sizeof(particles);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->particleBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->particleBuffer,
                          0u,
                          particles,
                          sizeof(particles)) != GPU_OK) {
    set_status("GPU: failed to upload particles", 1);
    return 0;
  }

  bufferInfo.label     = "webgpu-particle-draw";
  bufferInfo.sizeBytes = sizeof(drawArgs);
  bufferInfo.usage     = GPU_BUFFER_USAGE_INDIRECT |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->drawBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->drawBuffer,
                          0u,
                          drawArgs,
                          sizeof(drawArgs)) != GPU_OK) {
    set_status("GPU: failed to upload particle draw command", 1);
    return 0;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = state->particleBuffer;
  groupEntry.buffer.size   = sizeof(particles);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "webgpu-particle-group";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &groupEntry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->computeGroup) != GPU_OK) {
    set_status("GPU: failed to create particle compute group", 1);
    return 0;
  }

  groupEntry.bindingType = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  groupInfo.label        = "webgpu-particle-render-group";
  groupInfo.layout       = state->shaderLayout->bindGroupLayouts[1];
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->renderGroup) != GPU_OK) {
    set_status("GPU: failed to create particle render group", 1);
    return 0;
  }
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUParticles             *state;
  GPUFrame                    *frame;
  GPUCommandBuffer            *cmdb;
  GPUComputePassEncoder       *compute;
  GPURenderPassEncoder        *render;
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};
  GPUBufferBarrier             barrier = {0};
  GPUBarrierBatch              barriers = {0};
  Simulation                   simulation;
  double                       now;

  state = userData;
  if (!resize_canvas(state)) {
    return;
  }

  now = emscripten_get_now() * 0.001;
  simulation.deltaTime = state->previousTime > 0.0
                           ? (float)(now - state->previousTime)
                           : 1.0f / 60.0f;
  simulation.deltaTime = fminf(simulation.deltaTime, 1.0f / 30.0f);
  simulation.time      = (float)now;
  simulation.aspect    = (float)state->width / (float)state->height;
  simulation.count     = PARTICLE_COUNT;
  state->previousTime  = now;

  frame = GPUBeginFrame(state->swapchain);
  cmdb  = NULL;
  if (!frame ||
      GPUAcquireCommandBuffer(state->queue,
                              "webgpu-particle-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  compute = GPUBeginComputePass(cmdb, "webgpu-particle-simulation");
  if (!compute) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindComputePipeline(compute, state->computePipeline);
  GPUBindComputeGroup(compute, 0u, state->computeGroup, 0u, NULL);
  GPUSetComputePushConstants(compute,
                             0u,
                             (uint32_t)sizeof(simulation),
                             &simulation);
  GPUDispatch(compute,
              (PARTICLE_COUNT + PARTICLE_WORKGROUP_SIZE - 1u) /
                PARTICLE_WORKGROUP_SIZE,
              1u,
              1u);
  GPUEndComputePass(compute);

  barrier.buffer    = state->particleBuffer;
  barrier.srcAccess = GPU_ACCESS_SHADER_WRITE;
  barrier.dstAccess = GPU_ACCESS_SHADER_READ;
  barrier.sizeBytes = sizeof(Particle) * PARTICLE_COUNT;
  barriers.srcStages          = GPU_STAGE_COMPUTE;
  barriers.dstStages          = GPU_STAGE_VERTEX;
  barriers.pBufferBarriers    = &barrier;
  barriers.bufferBarrierCount = 1u;
  GPUEncodeBarriers(cmdb, &barriers);

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.014f;
  color.clearColor.float32[2] = 0.034f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.label                = "webgpu-particle-render";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  render = GPUBeginRenderPass(cmdb, &passInfo);
  if (!render) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }
  GPUBindRenderPipeline(render, state->renderPipeline);
  GPUBindRenderGroup(render, 1u, state->renderGroup, 0u, NULL);
  GPUSetRenderPushConstants(render,
                            0u,
                            (uint32_t)sizeof(simulation),
                            &simulation);
  GPUDrawIndirect(render, state->drawBuffer, 0u);
  GPUEndRenderPass(render);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish particle frame", 1);
    return;
  }

  state->frameCount++;
  if (state->frameCount > WARM_FRAME_COUNT) {
    GPUFrameStats stats;

    if (GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.drawCalls != 1u ||
         stats.hotPathAllocCount != 0u ||
         stats.hotPathFreeCount != 0u)) {
      set_status("GPU: particle warm path regression", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUParticles *state;
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
    set_status("GPU: failed to configure particle runtime", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create particle canvas surface", 1);
    return;
  }
  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain || !create_resources(state)) {
    return;
  }

  GPUResetStats(device);
  set_status("GPU: WebGPU USL compute particles ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "compute-particles-webgpu-usl";
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
