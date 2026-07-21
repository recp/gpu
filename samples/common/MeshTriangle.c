#include "MeshTriangle.h"

#include <string.h>

typedef struct MeshTaskParams {
  uint32_t meshGroups[4];
  float    offset[4];
  float    tint[4];
} MeshTaskParams;

static bool
mesh_layout_matches(const GPUShaderLayout *layout) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;

  if (!layout || layout->bindGroupLayoutCount != 1u ||
      !layout->bindGroupLayouts || !layout->bindGroupLayouts[0] ||
      !layout->pipelineLayout) {
    return false;
  }

  entryCount = 0u;
  entries = GPUGetBindGroupLayoutEntries(layout->bindGroupLayouts[0],
                                         &entryCount);
  for (uint32_t i = 0u; entries && i < entryCount; i++) {
    if (entries[i].binding == 0u && entries[i].arrayCount == 1u &&
        entries[i].bindingType == GPU_BINDING_UNIFORM_BUFFER &&
        entries[i].visibility == GPU_SHADER_STAGE_TASK_BIT) {
      return true;
    }
  }
  return false;
}

static GPUResult
create_pipeline(GPUSampleMeshTriangle *state) {
  GPUMeshPipelineEXT          meshInfo    = {0};
  GPUColorTargetState         colorTarget = {0};
  GPURenderPipelineCreateInfo info        = {0};
  GPUResult                   result;

  meshInfo.chain.sType        = GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT;
  meshInfo.chain.structSize   = sizeof(meshInfo);
  meshInfo.taskEntry          = "task_main";
  meshInfo.meshEntry          = "mesh_main";
  meshInfo.payloadSizeBytes   = 0u;
  colorTarget.format          = GPUGetSwapchainFormat(state->swapchain);
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;
  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.chain.pNext             = &meshInfo.chain;
  info.label                   = "mesh-triangle-pipeline";
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.fragmentEntry           = "fragment_main";
  info.pColorTargets           = &colorTarget;
  info.colorTargetCount        = 1u;
  info.depthStencilFormat      = GPU_FORMAT_UNDEFINED;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  return result == GPU_OK && state->pipeline
           ? GPU_OK
           : (result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE);
}

static GPUResult
create_task_group(GPUSampleMeshTriangle *state) {
  const MeshTaskParams taskParams = {
    .meshGroups = {1u, 1u, 1u, 0u},
    .offset     = {0.12f, 0.0f, 0.0f, 0.0f},
    .tint       = {1.0f, 0.75f, 0.5f, 1.0f}
  };
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUBindGroupEntry      entry      = {0};
  GPUBindGroupCreateInfo groupInfo  = {0};
  GPUResult              result;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "mesh-task-params";
  bufferInfo.sizeBytes        = sizeof(taskParams);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  result = GPUCreateBuffer(state->device, &bufferInfo, &state->taskBuffer);
  if (result != GPU_OK || !state->taskBuffer) {
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  result = GPUQueueWriteBuffer(state->queue,
                               state->taskBuffer,
                               0u,
                               &taskParams,
                               sizeof(taskParams));
  if (result != GPU_OK) {
    return result;
  }

  entry.binding       = 0u;
  entry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entry.buffer.buffer = state->taskBuffer;
  entry.buffer.size   = sizeof(taskParams);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "mesh-task-group";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &entry;
  groupInfo.entryCount       = 1u;
  result = GPUCreateBindGroup(state->device, &groupInfo, &state->taskGroup);
  return result == GPU_OK && state->taskGroup
           ? GPU_OK
           : (result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE);
}

GPUResult
GPUSampleMeshTriangleInit(GPUSampleMeshTriangle *state,
                          GPUDevice             *device,
                          GPUQueue              *queue,
                          GPUSwapchain          *swapchain,
                          GPUShaderLibrary      *library,
                          GPUShaderLayout       *shaderLayout,
                          uint32_t               width,
                          uint32_t               height) {
  GPUResult result;

  if (!state || !device || !queue || !swapchain || !library ||
      !shaderLayout) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(state, 0, sizeof(*state));
  state->device       = device;
  state->queue        = queue;
  state->swapchain    = swapchain;
  state->library      = library;
  state->shaderLayout = shaderLayout;
  state->width        = width;
  state->height       = height;
  if (!mesh_layout_matches(shaderLayout) || width == 0u || height == 0u ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_MESH_SHADER)) {
    GPUSampleMeshTriangleDestroy(state);
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  result = create_pipeline(state);
  if (result == GPU_OK) {
    result = create_task_group(state);
  }
  if (result != GPU_OK) {
    GPUSampleMeshTriangleDestroy(state);
  }
  return result;
}

GPUResult
GPUSampleMeshTriangleResize(GPUSampleMeshTriangle *state,
                            uint32_t               width,
                            uint32_t               height) {
  if (!state || width == 0u || height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  state->width  = width;
  state->height = height;
  return GPU_OK;
}

GPUResult
GPUSampleMeshTriangleRender(GPUSampleMeshTriangle        *state,
                            void                         *completionSender,
                            GPUCommandBufferCompletionFn  completion) {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color    = {0};
  GPURenderPassCreateInfo       passInfo = {0};
  GPUViewport                   viewport = {0};
  GPUScissorRect                scissor  = {0};
  GPUResult                     result;

  if (!state || !state->swapchain || !state->pipeline || !state->taskGroup) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  cmdb   = NULL;
  result = GPUAcquireCommandBuffer(state->queue, "mesh-triangle-frame", &cmdb);
  if (result != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  if (completion) {
    GPUSetCommandBufferCompletionHandler(cmdb, completionSender, completion);
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.020f;
  color.clearColor.float32[1] = 0.020f;
  color.clearColor.float32[2] = 0.025f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "mesh-triangle-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  viewport.width    = (float)state->width;
  viewport.height   = (float)state->height;
  viewport.maxDepth = 1.0f;
  scissor.width     = state->width;
  scissor.height    = state->height;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->taskGroup, 0u, NULL);
  GPUDrawMeshEXT(pass, 1u, 1u, 1u);
  GPUEndRenderPass(pass);

  result = GPUFinishFrame(state->queue, cmdb, frame);
  if (result == GPU_OK) {
    state->frameCount++;
  }
  return result;
}

void
GPUSampleMeshTriangleDestroy(GPUSampleMeshTriangle *state) {
  if (!state) {
    return;
  }

  GPUDestroyBindGroup(state->taskGroup);
  GPUDestroyBuffer(state->taskBuffer);
  GPUDestroyRenderPipeline(state->pipeline);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  memset(state, 0, sizeof(*state));
}
