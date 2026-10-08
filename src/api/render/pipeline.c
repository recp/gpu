/*
 * Copyright (C) 2020 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../constants_internal.h"
#include "../../common.h"
#include "../../backend/mt/binding_limits.h"
#include "pipeline_internal.h"
#include "../descr/descriptor_internal.h"
#include "../library_internal.h"
#include "../pipeline_cache_internal.h"
#include "../ray_internal.h"
#include "../vertex_internal.h"

#include <us/compiler.h>

#define GPU_RENDER_PIPELINE_MAX_COLOR_TARGETS 8u

static bool
blendStateIsValid(const GPUBlendState *blend) {
  return (uint32_t)blend->color.srcFactor <= GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
         && (uint32_t)blend->color.dstFactor <= GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
         && (uint32_t)blend->alpha.srcFactor <= GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
         && (uint32_t)blend->alpha.dstFactor <= GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
         && (uint32_t)blend->color.op <= GPU_BLEND_OP_MAX
         && (uint32_t)blend->alpha.op <= GPU_BLEND_OP_MAX
         && (blend->writeMask == GPU_COLOR_WRITE_NONE
             || (blend->writeMask & ~GPU_COLOR_WRITE_ALL) == 0u);
}

static bool
depthStencilStateIsValid(GPUFormat                   format,
                         const GPUDepthStencilState *state) {
  bool hasDepth;
  bool hasStencil;

  hasDepth   = format == GPU_FORMAT_DEPTH16_UNORM
               || format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
               || format == GPU_FORMAT_DEPTH32_FLOAT
               || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  hasStencil = format == GPU_FORMAT_STENCIL8
               || format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
               || format == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;

  if (format != GPU_FORMAT_UNDEFINED && !hasDepth && !hasStencil) {
    return false;
  }

  if (!state) {
    return true;
  }

  if ((uint32_t)state->depthCompare > GPU_COMPARE_ALWAYS
      || (uint32_t)state->front.compare > GPU_COMPARE_ALWAYS
      || (uint32_t)state->back.compare > GPU_COMPARE_ALWAYS
      || (uint32_t)state->front.failOp > GPU_STENCIL_OP_DECREMENT_WRAP
      || (uint32_t)state->front.depthFailOp > GPU_STENCIL_OP_DECREMENT_WRAP
      || (uint32_t)state->front.passOp > GPU_STENCIL_OP_DECREMENT_WRAP
      || (uint32_t)state->back.failOp > GPU_STENCIL_OP_DECREMENT_WRAP
      || (uint32_t)state->back.depthFailOp > GPU_STENCIL_OP_DECREMENT_WRAP
      || (uint32_t)state->back.passOp > GPU_STENCIL_OP_DECREMENT_WRAP
      || state->stencilReadMask > UINT8_MAX
      || state->stencilWriteMask > UINT8_MAX) {
    return false;
  }

  if (!state->depthTestEnable && !state->depthWriteEnable
      && !state->stencilTestEnable) {
    return true;
  }

  if ((state->depthTestEnable || state->depthWriteEnable) && !hasDepth) {
    return false;
  }

  return !state->stencilTestEnable || hasStencil;
}

static GPUResult
validatePipelineFormats(const GPUDevice                   *device,
                        const GPURenderPipelineCreateInfo *info) {
  GPUFormatCapabilities caps;
  GPUResult             result;
  uint32_t              sampleCount;
  uint32_t              i;

  sampleCount = info->multisample.sampleCount ? info->multisample.sampleCount : 1u;

  for (i = 0; i < info->colorTargetCount; i++) {
    result = GPUGetFormatCapabilities(device->adapter,
                                      info->pColorTargets[i].format,
                                      &caps);

    if (result != GPU_OK) {
      return result;
    }

    if (!caps.colorAttachment
        || (caps.supportedSampleCounts & sampleCount) == 0u
        || (info->pColorTargets[i].blend.enabled && !caps.blendable)) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    result = GPUGetFormatCapabilities(device->adapter,
                                      info->depthStencilFormat,
                                      &caps);

    if (result != GPU_OK) {
      return result;
    }

    if (!caps.depthStencil
        || (caps.supportedSampleCounts & sampleCount) == 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  return GPU_OK;
}

static bool
bindingTypeIsBuffer(GPUBindingType type) {
  return type == GPU_BINDING_UNIFORM_BUFFER
         || type == GPU_BINDING_READ_ONLY_STORAGE_BUFFER
         || type == GPU_BINDING_STORAGE_BUFFER;
}

static GPUResult
metalVertexResourceSlotMask(const GPURenderPipelineCreateInfo *info,
                            uint32_t                          *outMask) {
  GPUShaderReflection                reflection;
  const GPUShaderResourceReflection *resource;
  GPUShaderStageFlags                stage;
  uint32_t                           mask;
  uint32_t                           i;

  if (!info || !outMask) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!shaderLibraryHasEntryResourceInfo(info->library)) {
    *outMask = pipelineLayoutBackendSlotMask(info->layout,
                                             GPUBindKindBuffer,
                                             GPU_SHADER_STAGE_VERTEX_BIT);
    return GPU_OK;
  }

  if (!shaderEntryView(info->library,
                       info->vertexEntry,
                       &stage,
                       &reflection)
      || stage != GPU_SHADER_STAGE_VERTEX_BIT) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  mask = 0u;

  for (i = 0u; i < reflection.resourceCount; i++) {
    uint32_t binding;

    resource = &reflection.pResources[i];

    if (!bindingTypeIsBuffer(resource->bindingType)
        || (resource->visibility & GPU_SHADER_STAGE_VERTEX_BIT) == 0u) {
      continue;
    }

    binding = resource->binding;

    if (!getShaderResourceBackendBinding(info->library,
                                         resource,
                                         &binding)
        || binding >= MT_BIND_GROUP_BUFFER_COUNT) {
      return GPU_ERROR_UNSUPPORTED;
    }

    mask |= 1u << binding;
  }

  *outMask = mask;

  return GPU_OK;
}

static GPUResult
validateMetalVertexBindings(const GPUApi                      *api,
                            const GPURenderPipelineCreateInfo *info) {
  GPUResult result;
  uint32_t  resourceSlotMask;
  uint32_t  i;
  uint32_t  nativeIndex;

  if (!api || api->backend != GPU_BACKEND_METAL) {
    return GPU_OK;
  }

  result = metalVertexResourceSlotMask(info, &resourceSlotMask);

  if (result != GPU_OK) {
    return result;
  }

  for (i = 0u; i < info->vertex.bufferLayoutCount; i++) {
    if (info->vertex.pBufferLayouts[i].attributeCount == 0u) {
      continue;
    }

    nativeIndex = mt_vertexBufferIndex(i);

    if (nativeIndex == UINT32_MAX
        || (resourceSlotMask & (1u << nativeIndex)) != 0u) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  return GPU_OK;
}

static bool
vertexFormatIsValid(GPUVertexFormat format) {
  return (uint32_t)format > GPU_VERTEX_FORMAT_UNDEFINED
         && (uint32_t)format < GPU_VERTEX_FORMAT_COUNT;
}

static bool
vertexStepModeIsValid(GPUVertexStepMode mode) {
  return mode == GPU_VERTEX_STEP_MODE_VERTEX
         || mode == GPU_VERTEX_STEP_MODE_INSTANCE;
}

static bool
vertexStateIsValid(const GPUVertexState *state) {
  const GPUVertexBufferLayout *layout;
  uint32_t                     i;
  uint32_t                     j;

  if (!state) {
    return false;
  }

  if (state->bufferLayoutCount == 0) {
    return true;
  }

  if (!state->pBufferLayouts) {
    return false;
  }

  for (i = 0; i < state->bufferLayoutCount; i++) {
    layout = &state->pBufferLayouts[i];

    if (!vertexStepModeIsValid(layout->stepMode)) {
      return false;
    }

    if (layout->attributeCount > 0 && !layout->pAttributes) {
      return false;
    }

    for (j = 0; j < layout->attributeCount; j++) {
      if (!vertexFormatIsValid(layout->pAttributes[j].format)) {
        return false;
      }
    }
  }

  return true;
}

static GPUVertexDescriptor*
createVertexDescriptorFromState(GPUApi               *api,
                                const GPUVertexState *state) {
  GPUVertexDescriptor         *desc;
  const GPUVertexBufferLayout *layout;
  const GPUVertexAttribute    *attr;
  uint32_t                     i;
  uint32_t                     j;

  if (state->bufferLayoutCount == 0)
    return NULL;

  if (!api || !state->pBufferLayouts
      || !api->vertex.newVertexDesc
      || !api->vertex.destroyVertexDesc
      || !api->vertex.attrib
      || !api->vertex.layout
      || !api->vertex.vertexDesc)
    return NULL;

  if (!(desc = createVertexDesc(api)))
    return NULL;

  for (i = 0; i < state->bufferLayoutCount; i++) {
    layout = &state->pBufferLayouts[i];

    if (layout->attributeCount > 0 && !layout->pAttributes) {
      gpuDestroyVertexDesc(api, desc);
      return NULL;
    }

    vertexDescLayout(api,
                     desc,
                     i,
                     layout->strideBytes,
                     layout->stepMode);

    for (j = 0; j < layout->attributeCount; j++) {
      attr = &layout->pAttributes[j];

      vertexDescAttrib(api,
                       desc,
                       attr->shaderLocation,
                       attr->format,
                       attr->offset,
                       i);
    }
  }

  return desc;
}

static bool
primitiveTopologyIsValid(GPUPrimitiveTopology topology) {
  return topology == GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
         || topology == GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP
         || topology == GPU_PRIMITIVE_TOPOLOGY_LINE_LIST
         || topology == GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP
         || topology == GPU_PRIMITIVE_TOPOLOGY_POINT_LIST;
}

static bool
cullModeIsValid(GPUCullMode mode) {
  return mode == GPU_CULL_MODE_NONE
         || mode == GPU_CULL_MODE_FRONT
         || mode == GPU_CULL_MODE_BACK;
}

static bool
frontFaceIsValid(GPUFrontFace face) {
  return face == GPU_FRONT_FACE_CCW
         || face == GPU_FRONT_FACE_CW;
}

static bool
renderPipelineEntriesMatchStages(const GPURenderPipelineCreateInfo *info,
                                 const GPUMeshPipelineEXT          *mesh) {
  GPUShaderStageFlags stage;

  if (mesh) {
    if (mesh->taskEntry
        && getShaderLibraryEntryStage(info->library, mesh->taskEntry, &stage)
        && stage != GPU_SHADER_STAGE_TASK_BIT) {
      return false;
    }

    if (getShaderLibraryEntryStage(info->library, mesh->meshEntry, &stage)
        && stage != GPU_SHADER_STAGE_MESH_BIT) {
      return false;
    }
  } else {
    if (getShaderLibraryEntryStage(info->library,
                                   info->vertexEntry,
                                   &stage)
        && stage != GPU_SHADER_STAGE_VERTEX_BIT) {
      return false;
    }
  }

  if (getShaderLibraryEntryStage(info->library, info->fragmentEntry, &stage)
      && stage != GPU_SHADER_STAGE_FRAGMENT_BIT) {
    return false;
  }

  return true;
}

static GPUResult
resolveMeshPayloadSize(const GPURenderPipelineCreateInfo *info,
                       const GPUMeshPipelineEXT          *mesh,
                       uint32_t                          *outSizeBytes) {
  const char *taskType;
  const char *meshType;
  uint32_t    taskSize;
  uint32_t    meshSize;
  bool        taskKnown;
  bool        meshKnown;

  if (!info || !outSizeBytes) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSizeBytes = 0u;

  if (!mesh) {
    return GPU_OK;
  }

  taskType  = NULL;
  meshType  = NULL;
  taskSize  = 0u;
  meshSize  = 0u;
  meshKnown = getShaderLibraryPayloadInfo(info->library,
                                          mesh->meshEntry,
                                          GPU_SHADER_STAGE_MESH_BIT,
                                          &meshSize,
                                          &meshType) != 0;

  if (!mesh->taskEntry) {
    return mesh->payloadSizeBytes == 0u && (!meshKnown || meshSize == 0u)
             ? GPU_OK
             : GPU_ERROR_INVALID_ARGUMENT;
  }

  taskKnown = getShaderLibraryPayloadInfo(info->library,
                                          mesh->taskEntry,
                                          GPU_SHADER_STAGE_TASK_BIT,
                                          &taskSize,
                                          &taskType) != 0;

  if (taskKnown != meshKnown) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!taskKnown) {
    *outSizeBytes = mesh->payloadSizeBytes;
    return GPU_OK;
  }

  if ((taskSize == 0u) != (meshSize == 0u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (taskSize == 0u) {
    return mesh->payloadSizeBytes == 0u ? GPU_OK : GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!taskType || !meshType || taskSize != meshSize
      || strcmp(taskType, meshType) != 0
      || (mesh->payloadSizeBytes > 0u
          && mesh->payloadSizeBytes < taskSize)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outSizeBytes = mesh->payloadSizeBytes > 0u ? mesh->payloadSizeBytes : taskSize;

  return GPU_OK;
}

static bool
workgroupFits(const uint32_t size[3],
              const uint32_t limit[3],
              uint32_t       maxInvocations) {
  uint64_t invocations;
  uint32_t i;

  if (!size[0] || !size[1] || !size[2]) {
    return false;
  }

  for (i = 0u; i < 3u; i++) {
    if (limit[i] && size[i] > limit[i]) {
      return false;
    }
  }

  invocations = (uint64_t)size[0] * size[1] * size[2];

  return !maxInvocations || invocations <= maxInvocations;
}

static GPUResult
validateMeshInterface(const GPUDevice                   *device,
                      const GPURenderPipelineCreateInfo *info,
                      const GPUMeshPipelineEXT          *mesh,
                      uint32_t                           payloadSizeBytes) {
  uint32_t             workgroupSize[3];
  const GPUMeshLimits *limits;
  uint32_t             topology;
  uint32_t             maxVertices;
  uint32_t             maxPrimitives;

  if (!device || !mesh) {
    return GPU_OK;
  }

  limits = &device->meshLimits;

  if (mesh->taskEntry
      && getShaderLibraryWorkgroupSize(info->library,
                                       mesh->taskEntry,
                                       GPU_SHADER_STAGE_TASK_BIT,
                                       workgroupSize)) {
    if (!limits->maxTaskWorkgroupInvocations
        || !workgroupFits(workgroupSize,
                          limits->taskWorkgroupSize,
                          limits->maxTaskWorkgroupInvocations)) {
      return GPU_ERROR_UNSUPPORTED;
    }
  }

  if (getShaderLibraryWorkgroupSize(info->library,
                                    mesh->meshEntry,
                                    GPU_SHADER_STAGE_MESH_BIT,
                                    workgroupSize)
      && !workgroupFits(workgroupSize,
                        limits->meshWorkgroupSize,
                        limits->maxMeshWorkgroupInvocations)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (limits->maxPayloadSizeBytes
      && payloadSizeBytes > limits->maxPayloadSizeBytes) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!getShaderLibraryMeshOutputInfo(info->library,
                                      mesh->meshEntry,
                                      &topology,
                                      &maxVertices,
                                      &maxPrimitives)) {
    return GPU_OK;
  }

  if (maxVertices == 0u || maxPrimitives == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((limits->maxOutputVertices
       && maxVertices > limits->maxOutputVertices)
      || (limits->maxOutputPrimitives
          && maxPrimitives > limits->maxOutputPrimitives)) {
    return GPU_ERROR_UNSUPPORTED;
  }

  switch (topology) {
    case USL_RUNTIME_MESH_TOPOLOGY_POINT:
      return info->primitiveTopology == GPU_PRIMITIVE_TOPOLOGY_POINT_LIST
               ? GPU_OK
               : GPU_ERROR_INVALID_ARGUMENT;
    case USL_RUNTIME_MESH_TOPOLOGY_LINE:
      return info->primitiveTopology == GPU_PRIMITIVE_TOPOLOGY_LINE_LIST
               ? GPU_OK
               : GPU_ERROR_INVALID_ARGUMENT;
    case USL_RUNTIME_MESH_TOPOLOGY_TRIANGLE:
      return info->primitiveTopology == GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
               ? GPU_OK
               : GPU_ERROR_INVALID_ARGUMENT;
    default:
      return GPU_ERROR_INVALID_ARGUMENT;
  }
}

static void
setMeshPipelineInfo(GPURenderPipeline        *pipeline,
                    const GPUShaderLibrary   *library,
                    const GPUMeshPipelineEXT *mesh,
                    uint32_t                  payloadSizeBytes) {
  if (!pipeline) {
    return;
  }

  pipeline->_mesh                 = mesh != NULL;
  pipeline->_task                 = mesh && mesh->taskEntry;
  pipeline->_payloadSizeBytes     = payloadSizeBytes;
  pipeline->_taskWorkgroupSize[0] = 1u;
  pipeline->_taskWorkgroupSize[1] = 1u;
  pipeline->_taskWorkgroupSize[2] = 1u;
  pipeline->_meshWorkgroupSize[0] = 1u;
  pipeline->_meshWorkgroupSize[1] = 1u;
  pipeline->_meshWorkgroupSize[2] = 1u;

  if (!mesh) {
    return;
  }

  if (mesh->taskEntry) {
    getShaderLibraryWorkgroupSize(library,
                                  mesh->taskEntry,
                                  GPU_SHADER_STAGE_TASK_BIT,
                                  pipeline->_taskWorkgroupSize);
  }

  getShaderLibraryWorkgroupSize(library,
                                mesh->meshEntry,
                                GPU_SHADER_STAGE_MESH_BIT,
                                pipeline->_meshWorkgroupSize);
}

static GPUResult
renderPipelineExtensions(GPUDevice                                 *device,
                         const GPURenderPipelineCreateInfo         *info,
                         const GPUMeshPipelineEXT                 **outMesh,
                         const GPUIntersectionFunctionPipelineEXT **outIntersection) {
  const GPUChainedStruct                   *chain;
  const GPUMeshPipelineEXT                 *mesh;
  const GPUIntersectionFunctionPipelineEXT *intersection;
  uint32_t                                  i;

  if (!device || !info || !outMesh || !outIntersection) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  mesh         = NULL;
  intersection = NULL;

  for (chain = info->chain.pNext; chain; chain = chain->pNext) {
    switch (chain->sType) {
      case GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT:
        if (mesh
            || (chain->structSize != 0u
                && chain->structSize < sizeof(GPUMeshPipelineEXT))) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }

        mesh = (const GPUMeshPipelineEXT *)chain;
        break;
      case GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_PIPELINE_EXT:
        if (intersection
            || (chain->structSize != 0u
                && chain->structSize < sizeof(GPUIntersectionFunctionPipelineEXT))) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }

        intersection = (const GPUIntersectionFunctionPipelineEXT *)chain;
        break;
      case GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS:
        break;
      default:
        return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  if (mesh) {
    if (!mesh->meshEntry || info->vertexEntry
        || info->vertex.bufferLayoutCount != 0u) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    if (!GPUIsFeatureEnabled(device, GPU_FEATURE_MESH_SHADER)) {
      return GPU_ERROR_UNSUPPORTED;
    }
  } else if (!info->vertexEntry) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (intersection) {
    if (!GPUIsFeatureEnabled(device,
                             GPU_FEATURE_INTERSECTION_FUNCTION_TABLE)) {
      return GPU_ERROR_UNSUPPORTED;
    }

    if (mesh) {
      for (i = 0u; i < intersection->functionCount; i++) {
        if (intersection->pFunctions
            && intersection->pFunctions[i].stage == GPU_SHADER_STAGE_VERTEX_BIT) {
          return GPU_ERROR_INVALID_ARGUMENT;
        }
      }
    }
  }

  *outMesh         = mesh;
  *outIntersection = intersection;

  return GPU_OK;
}

static bool
pipelineInfoIsSupported(const GPURenderPipelineCreateInfo *info,
                        bool                               mesh) {
  uint32_t i;

  if (info->chain.sType != GPU_STRUCTURE_TYPE_NONE
      && info->chain.sType != GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO) {
    return false;
  }

  if (info->chain.structSize != 0 && info->chain.structSize < sizeof(*info))
    return false;

  if (info->colorTargetCount > GPU_RENDER_PIPELINE_MAX_COLOR_TARGETS
      || (info->colorTargetCount > 0u && !info->pColorTargets)
      || (info->colorTargetCount == 0u
          && info->depthStencilFormat == GPU_FORMAT_UNDEFINED))
    return false;

  if (!vertexStateIsValid(&info->vertex))
    return false;

  if (!primitiveTopologyIsValid(info->primitiveTopology)
      || !cullModeIsValid(info->cullMode)
      || !frontFaceIsValid(info->frontFace))
    return false;

  if (mesh
      && info->primitiveTopology != GPU_PRIMITIVE_TOPOLOGY_POINT_LIST
      && info->primitiveTopology != GPU_PRIMITIVE_TOPOLOGY_LINE_LIST
      && info->primitiveTopology != GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
    return false;

  if (info->multisample.sampleCount != 0u
      && info->multisample.sampleCount != 1u
      && info->multisample.sampleCount != 2u
      && info->multisample.sampleCount != 4u
      && info->multisample.sampleCount != 8u)
    return false;

  if (info->multisample.sampleMask != 0u
      && info->multisample.sampleMask != UINT32_MAX)
    return false;

  if (!depthStencilStateIsValid(info->depthStencilFormat,
                                info->pDepthStencilState))
    return false;

  for (i = 0; i < info->colorTargetCount; i++) {
    if (info->pColorTargets[i].format == GPU_FORMAT_UNDEFINED)
      return false;

    if (!blendStateIsValid(&info->pColorTargets[i].blend))
      return false;
  }

  return true;
}

GPU_HIDE
GPURenderPipeline*
createRenderPipelineDesc(GPUApi *api, GPUFormat pixelFormat, bool mesh) {
  if (!api || !api->render.newRenderPipeline)
    return NULL;

  return api->render.newRenderPipeline(pixelFormat, mesh);
}

GPU_HIDE
GPURenderPipelineState*
compileRenderPipelineState(GPUDevice         *__restrict device,
                           GPURenderPipeline *__restrict pipeline) {
  GPUApi *api;

  if (!device || !pipeline || !(api = deviceApi(device))
      || pipeline->_api != api || !api->render.newRenderState)
    return NULL;

  return api->render.newRenderState(device, pipeline);
}

GPU_HIDE
void
pipelineSetFunction(GPURenderPipeline *__restrict pipeline,
                    GPUShaderFunction *__restrict func,
                    GPUFunctionType               functionType) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.setFunction)
    return;

  api->render.setFunction(pipeline, func, functionType);
}

GPU_HIDE
void
pipelineSetColorFormat(GPURenderPipeline *__restrict pipeline,
                       uint32_t                      index,
                       GPUFormat                     pixelFormat) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.colorFormat)
    return;

  api->render.colorFormat(pipeline, index, pixelFormat);
}

GPU_HIDE
void
pipelineSetDepthFormat(GPURenderPipeline *__restrict pipeline,
                       GPUFormat                     pixelFormat) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.depthFormat)
    return;

  api->render.depthFormat(pipeline, pixelFormat);
}

GPU_HIDE
void
pipelineSetStencilFormat(GPURenderPipeline *__restrict pipeline,
                         GPUFormat                     pixelFormat) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.stencilFormat)
    return;

  api->render.stencilFormat(pipeline, pixelFormat);
}

GPU_HIDE
void
pipelineSetSampleCount(GPURenderPipeline *__restrict pipeline,
                       uint32_t                      sampleCount) {
  GPUApi *api;

  if (!pipeline || !(api = pipeline->_api) || !api->render.sampleCount)
    return;

  api->render.sampleCount(pipeline, sampleCount);
}

GPU_HIDE
GPUResult
createRenderPipeline(GPUDevice                         *__restrict device,
                     const GPURenderPipelineCreateInfo *__restrict info,
                     GPURenderPipeline                **__restrict outPipeline) {
  GPUPipelineCacheKey                       cacheKey;
  const GPUPipelineConstants               *constants;
  GPUApi                                   *api;
  GPURenderPipelineState                   *state;
  GPURenderPipeline                        *pipeline;
  const GPUMeshPipelineEXT                 *mesh;
  const GPUIntersectionFunctionPipelineEXT *intersection;
  GPUVertexDescriptor                      *vertexDesc;
  GPUShaderFunction                        *vertexFunc;
  GPUShaderFunction                        *fragmentFunc;
  GPUShaderFunction                        *taskFunc;
  GPUShaderFunction                        *meshFunc;
  GPUFormat                                 colorFormat;
  GPUResult                                 result;
  uint32_t                                  i;
  uint32_t                                  payloadSizeBytes;
  uint32_t                                  requiredBindGroupMask;
  uint32_t                                  sampleCount;
  uint32_t                                  entryCount;

  if (!outPipeline)
    return GPU_ERROR_INVALID_ARGUMENT;

  *outPipeline = NULL;
  memset(&cacheKey, 0, sizeof(cacheKey));

  if (!device || !info || !info->layout || !info->library
      || !info->fragmentEntry || info->layout->_device != device
      || info->library->_device != device)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (info->cache && info->cache->device != device)
    return GPU_ERROR_INVALID_ARGUMENT;

  result = renderPipelineExtensions(device,
                                    info,
                                    &mesh,
                                    &intersection);

  if (result != GPU_OK)
    return result;

  if (!pipelineInfoIsSupported(info, mesh != NULL))
    return GPU_ERROR_INVALID_ARGUMENT;

  result = validatePipelineFormats(device, info);

  if (result != GPU_OK)
    return result;

  if (!(api = deviceApi(device)))
    return GPU_ERROR_BACKEND_FAILURE;

  if (!mesh) {
    result = validateMetalVertexBindings(api, info);

    if (result != GPU_OK)
      return result;
  }

  if (info->cache && !intersection) {
    result = pipelineCacheFindRender(info->cache,
                                     info,
                                     &cacheKey,
                                     &pipeline);

    if (result != GPU_OK) {
      return result;
    }

    if (pipeline) {
      pipelineCacheReleaseKey(&cacheKey);

      *outPipeline = pipeline;
      return GPU_OK;
    }
  }

  if (!renderPipelineEntriesMatchStages(info, mesh)) {
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = resolveMeshPayloadSize(info, mesh, &payloadSizeBytes);

  if (result != GPU_OK) {
    pipelineCacheReleaseKey(&cacheKey);
    return result;
  }

  result = validateMeshInterface(device,
                                 info,
                                 mesh,
                                 payloadSizeBytes);

  if (result != GPU_OK) {
    pipelineCacheReleaseKey(&cacheKey);
    return result;
  }

  if (mesh) {
    const char *entries[3];

    entryCount = 0u;

    if (mesh->taskEntry) {
      entries[entryCount++] = mesh->taskEntry;
    }

    entries[entryCount++] = mesh->meshEntry;
    entries[entryCount++] = info->fragmentEntry;

    if (!pipelineLayoutMatchesShaderEntries(info->layout,
                                            info->library,
                                            entries,
                                            entryCount,
                                            GPU_SHADER_STAGE_TASK_BIT
                                                 | GPU_SHADER_STAGE_MESH_BIT
                                                 | GPU_SHADER_STAGE_FRAGMENT_BIT,
                                            &requiredBindGroupMask)) {
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  } else {
    const char *vertexEntries[] = {info->vertexEntry, info->fragmentEntry};

    if (!pipelineLayoutMatchesShaderEntries(info->layout,
                                            info->library,
                                            vertexEntries,
                                            (uint32_t)GPU_ARRAY_LEN(vertexEntries),
                                            GPU_SHADER_STAGE_VERTEX_BIT | GPU_SHADER_STAGE_FRAGMENT_BIT,
                                            &requiredBindGroupMask)) {
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  sampleCount = info->multisample.sampleCount > 0 ? info->multisample.sampleCount : 1u;

  if (intersection && api->render.createPipeline) {
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_UNSUPPORTED;
  }

  if (api->render.createPipeline) {
    if (!(pipeline = calloc(1, sizeof(*pipeline)))) {
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_OUT_OF_MEMORY;
    }

    pipeline->_api      = api;
    pipeline->_refCount = 1u;
    setMeshPipelineInfo(pipeline,
                        info->library,
                        mesh,
                        payloadSizeBytes);
    result = api->render.createPipeline(device,
                                        info,
                                        requiredBindGroupMask,
                                        pipeline);

    if (result != GPU_OK) {
      free(pipeline);
      pipelineCacheReleaseKey(&cacheKey);
      return result;
    }

    goto ready;
  }

  constants    = pipelineConstants(info->chain.pNext);
  vertexFunc   = mesh ? NULL : shaderVariant(info->library, info->vertexEntry, constants);
  fragmentFunc = shaderVariant(info->library, info->fragmentEntry, constants);
  taskFunc     = mesh && mesh->taskEntry ? shaderVariant(info->library, mesh->taskEntry, constants) : NULL;
  meshFunc     = mesh ? shaderVariant(info->library, mesh->meshEntry, constants) : NULL;

  if ((!mesh && !vertexFunc) || !fragmentFunc
      || (mesh && (!meshFunc || (mesh->taskEntry && !taskFunc)))) {
    destroyShaderFunction(info->library, vertexFunc);
    destroyShaderFunction(info->library, fragmentFunc);
    destroyShaderFunction(info->library, taskFunc);
    destroyShaderFunction(info->library, meshFunc);
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  colorFormat = info->colorTargetCount > 0u ? info->pColorTargets[0].format : GPU_FORMAT_UNDEFINED;

  if (!(pipeline = createRenderPipelineDesc(api, colorFormat, mesh != NULL))) {
    destroyShaderFunction(info->library, vertexFunc);
    destroyShaderFunction(info->library, fragmentFunc);
    destroyShaderFunction(info->library, taskFunc);
    destroyShaderFunction(info->library, meshFunc);
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  pipeline->_api      = api;
  pipeline->_refCount = 1u;
  setMeshPipelineInfo(pipeline,
                      info->library,
                      mesh,
                      payloadSizeBytes);

  if (mesh) {
    if (taskFunc)
      pipelineSetFunction(pipeline, taskFunc, GPU_FUNCTION_TASK);

    pipelineSetFunction(pipeline, meshFunc, GPU_FUNCTION_MESH);
  } else {
    pipelineSetFunction(pipeline, vertexFunc, GPU_FUNCTION_VERT);
  }

  pipelineSetFunction(pipeline, fragmentFunc, GPU_FUNCTION_FRAG);
  destroyShaderFunction(info->library, vertexFunc);
  destroyShaderFunction(info->library, fragmentFunc);
  destroyShaderFunction(info->library, taskFunc);
  destroyShaderFunction(info->library, meshFunc);

  if (intersection) {
    result = attachRenderIntersectionFunctions(device,
                                               info->library,
                                               intersection,
                                               pipeline);

    if (result != GPU_OK) {
      GPUDestroyRenderPipeline(pipeline);
      pipelineCacheReleaseKey(&cacheKey);
      return result;
    }
  }

  if (!mesh) {
    vertexDesc = createVertexDescriptorFromState(api, &info->vertex);

    if (info->vertex.bufferLayoutCount > 0 && !vertexDesc) {
      GPUDestroyRenderPipeline(pipeline);
      pipelineCacheReleaseKey(&cacheKey);
      return GPU_ERROR_INVALID_ARGUMENT;
    }

    if (vertexDesc)
      pipelineSetVertexDesc(pipeline, vertexDesc);

    gpuDestroyVertexDesc(api, vertexDesc);
  }

  for (i = 0; i < info->colorTargetCount; i++)
    pipelineSetColorFormat(pipeline, i, info->pColorTargets[i].format);

  if (info->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
    if (info->depthStencilFormat != GPU_FORMAT_STENCIL8)
      pipelineSetDepthFormat(pipeline, info->depthStencilFormat);

    if (info->depthStencilFormat == GPU_FORMAT_STENCIL8
        || info->depthStencilFormat == GPU_FORMAT_DEPTH24_UNORM_STENCIL8
        || info->depthStencilFormat == GPU_FORMAT_DEPTH32_FLOAT_STENCIL8)
      pipelineSetStencilFormat(pipeline, info->depthStencilFormat);
  }

  if (info->pDepthStencilState)
    pipeline->_depthStencilState = *info->pDepthStencilState;

  pipeline->_sampleCount           = sampleCount;
  pipeline->_primitiveTopology     = info->primitiveTopology;
  pipeline->_alphaToCoverageEnable = info->multisample.alphaToCoverageEnable;
  pipeline->_colorTargetCount      = info->colorTargetCount;

  for (i = 0; i < info->colorTargetCount; i++) {
    pipeline->_colorTargetFormats[i] = info->pColorTargets[i].format;
    pipeline->_colorTargetBlends[i]  = info->pColorTargets[i].blend;
  }

  pipelineSetSampleCount(pipeline, sampleCount);

  pipeline->_cache = info->cache;
  state            = compileRenderPipelineState(device, pipeline);
  pipeline->_cache = NULL;

  if (!state) {
    GPUDestroyRenderPipeline(pipeline);
    pipelineCacheReleaseKey(&cacheKey);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  free(state);

ready:
  pipeline->_layout                = info->layout;
  pipeline->_requiredBindGroupMask = requiredBindGroupMask;
  pipeline->_colorTargetCount      = info->colorTargetCount;

  for (i = 0; i < info->colorTargetCount; i++) {
    pipeline->_colorTargetFormats[i] = info->pColorTargets[i].format;
    pipeline->_colorTargetBlends[i]  = info->pColorTargets[i].blend;
  }

  pipeline->_depthStencilFormat    = info->depthStencilFormat;
  pipeline->_sampleCount           = sampleCount;
  pipeline->_alphaToCoverageEnable = info->multisample.alphaToCoverageEnable;
  pipeline->_primitiveTopology     = info->primitiveTopology;
  pipeline->_cullMode              = info->cullMode;
  pipeline->_frontFace             = info->frontFace;
  getPipelineLayoutPushConstants(info->layout,
                                 &pipeline->_pushConstantSizeBytes,
                                 &pipeline->_pushConstantStages);

  if (info->cache && !intersection) {
    pipeline = pipelineCacheStoreRender(info->cache, &cacheKey, pipeline);
  } else {
    recordPipelineCompile(device, info->cache);
  }

  *outPipeline = pipeline;

  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyRenderPipeline(GPURenderPipeline *pipeline) {
  GPUApi *api;

  if (!pipeline)
    return;

  if (!releaseRenderPipeline(pipeline)) {
    return;
  }

  if ((api = pipeline->_api) && api->render.destroyRenderPipeline) {
    api->render.destroyRenderPipeline(pipeline);
    return;
  }

  free(pipeline);
}

GPU_EXPORT
GPUResult
GPUCreateRenderPipeline(GPUDevice                         *device,
                        const GPURenderPipelineCreateInfo *info,
                        GPURenderPipeline                **outPipeline) {
  GPURenderPipelineCreateInfo snapshot;
  GPUPreparedConstants        prepared;
  GPUResult                   result;

  if (!outPipeline) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outPipeline = NULL;

  if (!info || !info->library) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = prepareConstants(info->library, info->chain.pNext, false, &prepared);

  if (result != GPU_OK) {
    return result;
  }

  snapshot             = *info;
  snapshot.chain.pNext = prepared.chain;
  result               = createRenderPipeline(device, &snapshot, outPipeline);
  free(prepared.values);

  return result;
}
