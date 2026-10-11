/*
 * Copyright (C) 2026 Recep Aslantas
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

#undef malloc
#undef free

#include "api/constants_internal.h"
#include "api/compute_internal.h"
#include "api/execution_graph_internal.h"
#include "api/pipeline_cache_internal.h"
#include "api/ray_internal.h"
#include "api/render/pipeline_internal.h"

#define KEY_FIELD(FIELD) \
  appendBytes(expected, &size, &(FIELD), sizeof(FIELD))

typedef enum StateKeyKind {
  STATE_KEY_RENDER,
  STATE_KEY_RAY,
  STATE_KEY_GRAPH
} StateKeyKind;

static const uint32_t nameSizes[]   = {1u, 19u, 235u, 236u, 237u, 256u, 257u, 1024u};
static const uint32_t counts[]      = {0u, 1u, 4u, 16u, 64u, 128u};
static const uint32_t renderNames[] = {1u, 19u, 59u, 60u, 128u, 1024u};
static const uint32_t renderAttrs[] = {0u, 4u, 16u};
static const uint32_t rayCounts[]   = {0u, 1u, 4u, 8u, 16u, 32u};
static const uint32_t rayNames[]    = {1u, 19u, 128u};
static const uint32_t graphNames[]  = {0u, 1u, 19u, 239u, 240u, 256u, 1024u};

static uint32_t allocationCalls;
static uint32_t releaseCalls;
static uint32_t unexpectedCalls;
static uint32_t faultCases;
static bool     failAllocation;

static void
appendBytes(uint8_t *bytes, size_t *size, const void *value, size_t count) {
  memcpy(bytes + *size, value, count);
  *size += count;
}

static void
appendOptional(uint8_t *bytes, size_t *size, const char *value) {
  bool present;

  present = value != NULL;
  appendBytes(bytes, size, &present, sizeof(present));

  if (present) {
    appendBytes(bytes, size, value, strlen(value) + 1u);
  }
}

static GPUResult
findStateKey(GPUPipelineCache    *cache,
             StateKeyKind         kind,
             const void          *info,
             GPUPipelineCacheKey *key) {
  GPURenderPipeline        *render = NULL;
  GPURayTracingPipelineEXT *ray    = NULL;
  GPUExecutionGraphEXT     *graph  = NULL;
  GPUResult                 result;

  switch (kind) {
    case STATE_KEY_RENDER: result = pipelineCacheFindRender(cache, info, key, &render); break;
    case STATE_KEY_RAY:    result = pipelineCacheFindRay(cache, info, key, &ray); break;
    case STATE_KEY_GRAPH:  result = pipelineCacheFindGraph(cache, info, key, &graph); break;
    default: return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (render || ray || graph) {
    unexpectedCalls++;
  }

  return result;
}

static int
checkStateKey(GPUPipelineCache *cache,
              StateKeyKind      kind,
              const void       *info,
              const uint8_t    *expected,
              size_t            size) {
  GPUPipelineCacheKey  key;
  uint64_t             hash;
  size_t               i;
  int                  ok;
  bool                 allocated;

  hash = 14695981039346656037ull;

  for (i = 0u; i < size; i++) {
    hash ^= expected[i];
    hash * = 1099511628211ull;
  }

  allocationCalls = releaseCalls = 0u;

  if (!expected) {
    return findStateKey(cache, kind, info, &key) == GPU_ERROR_OUT_OF_MEMORY
           && !key.data && !key.size && !key.hash && !key.ownsData
           && !allocationCalls && !releaseCalls;
  }

  if (findStateKey(cache, kind, info, &key) != GPU_OK) {
    return 0;
  }

  allocated = size > sizeof(key.inlineData);
  ok        = key.size == size && key.hash == hash && memcmp(key.data, expected, size) == 0
              && key.ownsData == allocated && allocationCalls == (uint32_t)allocated
              && (allocated || key.data == key.inlineData);
  pipelineCacheReleaseKey(&key);

  ok = ok && releaseCalls == (uint32_t)allocated
       && !key.data && !key.size && !key.hash && !key.ownsData;

  if (allocated) {
    failAllocation  = true;
    allocationCalls = releaseCalls = 0u;

    ok = ok && findStateKey(cache, kind, info, &key) == GPU_ERROR_OUT_OF_MEMORY
         && !key.data && key.size == size && !key.hash && !key.ownsData
         && allocationCalls == 1u && releaseCalls == 0u;
    failAllocation = false;
    pipelineCacheReleaseKey(&key);

    ok = ok && !key.data && !key.size && !key.hash && !key.ownsData && !releaseCalls;
  }

  return ok;
}

static int
checkRenderKey(GPUPipelineCache *cache,
               uint32_t          count,
               uint32_t          attributeCount,
               uint32_t          nameSize,
               uint32_t          depth,
               uint32_t          mode) {
  GPUConstant                  values[128];
  GPUConstant                  original[128];
  GPUVertexAttribute           attributes[16] = {0};
  GPUVertexBufferLayout        buffers[2]     = {0};
  GPUColorTargetState          targets[2]     = {0};
  GPUChainedStruct             prefix[3]      = {0};
  GPUPipelineConstants         constants      = {0};
  GPUMeshPipelineEXT           mesh           = {0};
  GPURenderPipelineCreateInfo  info           = {0};
  GPURenderPipelineCreateInfo  originalInfo;
  GPUDepthStencilState         stencil = {0};
  GPUDepthStencilState         empty   = {0};
  uint8_t                      expected[8192];
  char                         storage[1025];
  const GPUDepthStencilState  *state;
  char                        *name;
  uintptr_t                    layout;
  uintptr_t                    library;
  size_t                       size;
  uint32_t                     activeCount;
  uint32_t                     pipelineType;
  uint32_t                     i;
  uint32_t                     j;
  int                          ok;
  bool                         activeMesh;

  name = storage + 1u;
  memset(name, 'e', nameSize - 1u);
  name[nameSize - 1u] = '\0';
  memset(values, 0xa5, sizeof(values));

  for (i = 0u; i < count; i++) {
    values[i].id   = UINT32_MAX - i * 3u;
    values[i].type = (GPUConstantType)(GPU_CONSTANT_BOOL + i % 4u);

    switch (values[i].type) {
      case GPU_CONSTANT_BOOL: values[i].value.boolean = i % 3u != 0u; break;
      case GPU_CONSTANT_I32:  values[i].value.i32 = INT32_MIN + (int32_t)i; break;
      case GPU_CONSTANT_U32:  values[i].value.u32 = UINT32_MAX - i; break;
      case GPU_CONSTANT_F32:  values[i].value.f32 = i ? (float)i : -0.0f; break;
      default: return 0;
    }
  }

  memcpy(original, values, sizeof(original));

  constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.chain.structSize = sizeof(constants);
  constants.pConstants       = values;
  constants.constantCount    = count;

  mesh.chain.sType      = GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT;
  mesh.chain.structSize = sizeof(mesh);
  mesh.chain.pNext      = &constants.chain;
  mesh.taskEntry        = mode == 1u ? NULL : mode == 2u ? "" : name;
  mesh.meshEntry        = name;
  mesh.payloadSizeBytes = 48u;

  info.chain.pNext = mode ? &mesh.chain : &constants.chain;

  for (i = 0u; i < depth; i++) {
    prefix[i].sType  = GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_PIPELINE_EXT;
    prefix[i].pNext  = info.chain.pNext;
    info.chain.pNext = &prefix[i];
  }

  for (i = 0u; i < attributeCount; i++) {
    attributes[i].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
    attributes[i].offset         = i * 16u;
    attributes[i].shaderLocation = i;
  }

  buffers[0].pAttributes    = attributes;
  buffers[0].attributeCount = attributeCount / 2u;
  buffers[0].strideBytes    = attributeCount * 16u;
  buffers[1].pAttributes    = attributes + attributeCount / 2u;
  buffers[1].attributeCount = attributeCount - attributeCount / 2u;
  buffers[1].strideBytes    = attributeCount * 32u;
  buffers[1].stepMode       = GPU_VERTEX_STEP_MODE_INSTANCE;

  for (i = 0u; i < 2u; i++) {
    targets[i].format                = GPU_FORMAT_RGBA8_UNORM;
    targets[i].blend.enabled         = i != 0u;
    targets[i].blend.color.srcFactor = (GPUBlendFactor)(i + 1u);
    targets[i].blend.color.dstFactor = (GPUBlendFactor)(i + 2u);
    targets[i].blend.color.op        = (GPUBlendOp)i;
    targets[i].blend.alpha.srcFactor = (GPUBlendFactor)(i + 3u);
    targets[i].blend.alpha.dstFactor = (GPUBlendFactor)(i + 4u);
    targets[i].blend.alpha.op        = (GPUBlendOp)(i + 1u);
    targets[i].blend.writeMask       = 15u - i;
  }

  stencil.depthTestEnable   = true;
  stencil.depthWriteEnable  = true;
  stencil.depthCompare      = GPU_COMPARE_LESS_EQUAL;
  stencil.stencilTestEnable = true;
  stencil.front.compare     = GPU_COMPARE_EQUAL;
  stencil.front.failOp      = GPU_STENCIL_OP_REPLACE;
  stencil.front.depthFailOp = GPU_STENCIL_OP_INCREMENT_WRAP;
  stencil.front.passOp      = GPU_STENCIL_OP_DECREMENT_CLAMP;
  stencil.back.compare      = GPU_COMPARE_NOT_EQUAL;
  stencil.back.failOp       = GPU_STENCIL_OP_INVERT;
  stencil.back.depthFailOp  = GPU_STENCIL_OP_DECREMENT_WRAP;
  stencil.back.passOp       = GPU_STENCIL_OP_ZERO;
  stencil.stencilReadMask   = 0xabcdu;
  stencil.stencilWriteMask  = 0xf0u;

  info.layout                   = (GPUPipelineLayout *)(uintptr_t)0x1234u;
  info.library                  = (GPUShaderLibrary *)(uintptr_t)0x5678u;
  info.vertexEntry              = name;
  info.fragmentEntry            = name;
  info.vertex.bufferLayoutCount = attributeCount ? 2u : 0u;
  info.vertex.pBufferLayouts    = buffers;
  info.colorTargetCount         = mode % 3u;
  info.pColorTargets            = targets;
  info.pDepthStencilState       = mode % 2u ? &stencil : NULL;
  info.depthStencilFormat       = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_BACK;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 4u;
  info.multisample.sampleMask   = 0x5u;

  info.multisample.alphaToCoverageEnable = true;

  memcpy(&originalInfo, &info, sizeof(info));

  size         = 0u;
  layout       = (uintptr_t)info.layout;
  library      = (uintptr_t)info.library;
  activeMesh   = mode && depth < 3u;
  activeCount  = depth + (uint32_t)(mode != 0u) < 3u ? count : 0u;
  pipelineType = activeMesh ? GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT : GPU_STRUCTURE_TYPE_NONE;
  state        = info.pDepthStencilState ? &stencil : &empty;
  KEY_FIELD(layout);
  KEY_FIELD(library);
  KEY_FIELD(pipelineType);
  KEY_FIELD(activeCount);

  for (i = 0u; i < activeCount; i++) {
    KEY_FIELD(values[i].id);
    KEY_FIELD(values[i].type);

    switch (values[i].type) {
      case GPU_CONSTANT_BOOL: KEY_FIELD(values[i].value.boolean); break;
      case GPU_CONSTANT_I32:  KEY_FIELD(values[i].value.i32); break;
      case GPU_CONSTANT_U32:  KEY_FIELD(values[i].value.u32); break;
      case GPU_CONSTANT_F32:  KEY_FIELD(values[i].value.f32); break;
      default: return 0;
    }
  }

  if (activeMesh) {
    appendBytes(expected,
                &size,
                mesh.taskEntry ? mesh.taskEntry : "",
                mesh.taskEntry ? strlen(mesh.taskEntry) + 1u : 1u);
    appendBytes(expected, &size, name, nameSize);
    KEY_FIELD(mesh.payloadSizeBytes);
  } else {
    appendBytes(expected, &size, name, nameSize);
  }

  appendBytes(expected, &size, name, nameSize);
  KEY_FIELD(info.vertex.bufferLayoutCount);

  for (i = 0u; i < info.vertex.bufferLayoutCount; i++) {
    KEY_FIELD(buffers[i].strideBytes);
    KEY_FIELD(buffers[i].stepMode);
    KEY_FIELD(buffers[i].attributeCount);

    for (j = 0u; j < buffers[i].attributeCount; j++) {
      KEY_FIELD(buffers[i].pAttributes[j].format);
      KEY_FIELD(buffers[i].pAttributes[j].offset);
      KEY_FIELD(buffers[i].pAttributes[j].shaderLocation);
    }
  }

  KEY_FIELD(info.colorTargetCount);

  for (i = 0u; i < info.colorTargetCount; i++) {
    KEY_FIELD(targets[i].format);
    KEY_FIELD(targets[i].blend.enabled);
    KEY_FIELD(targets[i].blend.color.srcFactor);
    KEY_FIELD(targets[i].blend.color.dstFactor);
    KEY_FIELD(targets[i].blend.color.op);
    KEY_FIELD(targets[i].blend.alpha.srcFactor);
    KEY_FIELD(targets[i].blend.alpha.dstFactor);
    KEY_FIELD(targets[i].blend.alpha.op);
    KEY_FIELD(targets[i].blend.writeMask);
  }

  KEY_FIELD(info.depthStencilFormat);
  KEY_FIELD(state->depthTestEnable);
  KEY_FIELD(state->depthWriteEnable);
  KEY_FIELD(state->depthCompare);
  KEY_FIELD(state->stencilTestEnable);
  KEY_FIELD(state->front.compare);
  KEY_FIELD(state->front.failOp);
  KEY_FIELD(state->front.depthFailOp);
  KEY_FIELD(state->front.passOp);
  KEY_FIELD(state->back.compare);
  KEY_FIELD(state->back.failOp);
  KEY_FIELD(state->back.depthFailOp);
  KEY_FIELD(state->back.passOp);
  KEY_FIELD(state->stencilReadMask);
  KEY_FIELD(state->stencilWriteMask);
  KEY_FIELD(info.primitiveTopology);
  KEY_FIELD(info.cullMode);
  KEY_FIELD(info.frontFace);
  KEY_FIELD(info.multisample.sampleCount);
  KEY_FIELD(info.multisample.sampleMask);
  KEY_FIELD(info.multisample.alphaToCoverageEnable);

  ok = checkStateKey(cache, STATE_KEY_RENDER, &info, expected, size)
       && memcmp(values, original, sizeof(values)) == 0
       && memcmp(&info, &originalInfo, sizeof(info)) == 0;

  if (!ok) {
    fprintf(stderr, "render key: count=%u attrs=%u name=%u depth=%u mode=%u\n",
            count, attributeCount, nameSize, depth, mode);
  }

  return ok;
}

static int
checkRayKey(GPUPipelineCache *cache, uint32_t count, uint32_t nameSize, uint32_t mask) {
  GPURayTracingShaderGroupEXT         groups[32] = {0};
  GPURayTracingShaderGroupEXT         original[32];
  GPURayTracingPipelineCreateInfoEXT  info = {0};
  uint8_t                             expected[32768];
  char                                storage[129];
  char                               *name;
  uintptr_t                           layout;
  uintptr_t                           library;
  size_t                              size;
  uint32_t                            i;
  uint32_t                            bits;
  int                                 ok;

  name = storage + 1u;
  memset(name, 'e', nameSize - 1u);
  name[nameSize - 1u] = '\0';

  for (i = 0u; i < count; i++) {
    bits                        = (mask + i) % 16u;
    groups[i].type              = (GPURayTracingShaderGroupTypeEXT)(i % 3u);
    groups[i].generalStage      = GPU_SHADER_STAGE_RAY_GENERATION_BIT;
    groups[i].generalEntry      = bits & 1u ? name : NULL;
    groups[i].closestHitEntry   = bits & 2u ? name : NULL;
    groups[i].anyHitEntry       = bits & 4u ? name : NULL;
    groups[i].intersectionEntry = bits & 8u ? name : NULL;
  }

  memcpy(original, groups, sizeof(groups));

  info.layout                   = (GPUPipelineLayout *)(uintptr_t)0x1234u;
  info.library                  = (GPUShaderLibrary *)(uintptr_t)0x5678u;
  info.pGroups                  = groups;
  info.groupCount               = count;
  info.maxRecursionDepth        = 3u;
  info.maxPayloadSizeBytes      = 64u;
  info.maxHitAttributeSizeBytes = 8u;

  size    = 0u;
  layout  = (uintptr_t)info.layout;
  library = (uintptr_t)info.library;
  KEY_FIELD(layout);
  KEY_FIELD(library);
  KEY_FIELD(info.groupCount);

  for (i = 0u; i < count; i++) {
    KEY_FIELD(groups[i].type);
    KEY_FIELD(groups[i].generalStage);
    appendOptional(expected, &size, groups[i].generalEntry);
    appendOptional(expected, &size, groups[i].closestHitEntry);
    appendOptional(expected, &size, groups[i].anyHitEntry);
    appendOptional(expected, &size, groups[i].intersectionEntry);
  }

  KEY_FIELD(info.maxRecursionDepth);
  KEY_FIELD(info.maxPayloadSizeBytes);
  KEY_FIELD(info.maxHitAttributeSizeBytes);

  ok = checkStateKey(cache, STATE_KEY_RAY, &info, expected, size)
       && memcmp(groups, original, sizeof(groups)) == 0;

  if (!ok) {
    fprintf(stderr, "ray key: count=%u name=%u mask=%u\n", count, nameSize, mask);
  }

  return ok;
}

static int
checkGraphKey(GPUPipelineCache *cache, uint32_t nameSize) {
  GPUExecutionGraphCreateInfoEXT  info = {0};
  uint8_t                         expected[2048];
  char                            storage[1025];
  char                           *name;
  uintptr_t                       layout;
  uintptr_t                       library;
  size_t                          size;

  name = storage + 1u;

  if (nameSize) {
    memset(name, 'e', nameSize - 1u);
    name[nameSize - 1u] = '\0';
  }

  info.layout    = (GPUPipelineLayout *)(uintptr_t)0x1234u;
  info.library   = (GPUShaderLibrary *)(uintptr_t)0x5678u;
  info.graphName = nameSize ? name : NULL;

  size    = 0u;
  layout  = (uintptr_t)info.layout;
  library = (uintptr_t)info.library;
  KEY_FIELD(layout);
  KEY_FIELD(library);
  appendOptional(expected, &size, info.graphName);

  return checkStateKey(cache, STATE_KEY_GRAPH, &info, expected, size);
}

static int
checkInvalidStateKeys(GPUPipelineCache *cache) {
  GPUConstant                         value     = {0};
  GPUPipelineConstants                constants = {0};
  GPURenderPipelineCreateInfo         render    = {0};
  GPURayTracingPipelineCreateInfoEXT  ray       = {0};
  GPUExecutionGraphCreateInfoEXT      graph     = {0};
  GPUChainedStruct                    chain     = {0};
  uint32_t                            i;

  constants.chain.sType   = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.pConstants    = &value;
  constants.constantCount = 1u;

  render.chain.pNext   = &constants.chain;
  render.vertexEntry   = "vertex";
  render.fragmentEntry = "fragment";
  ray.chain.pNext      = &chain;
  graph.chain.pNext    = &chain;

  for (i = 0u; i < 3u; i++) {
    value.type = i == 0u ? (GPUConstantType)0u : i == 1u ? (GPUConstantType)5u
                                                      : (GPUConstantType)UINT32_MAX;

    if (!checkStateKey(cache, STATE_KEY_RENDER, &render, NULL, 0u)) {
      return 0;
    }
  }

  return checkStateKey(cache, STATE_KEY_RAY, &ray, NULL, 0u)
         && checkStateKey(cache, STATE_KEY_GRAPH, &graph, NULL, 0u);
}

static int
checkKey(GPUPipelineCache *cache,
         uint32_t          count,
         uint32_t          type,
         uint32_t          nameSize,
         uint32_t          depth) {
  GPUConstant                  values[128];
  GPUConstant                  original[128];
  GPUChainedStruct             prefix[3] = {0};
  GPUPipelineConstants         constants = {0};
  GPUComputePipelineCreateInfo info      = {0};
  GPUPipelineCacheKey          key;
  uint8_t                      expected[4096];
  char                         name[1024];
  GPUComputePipeline          *pipeline = NULL;
  uintptr_t                    layout;
  uintptr_t                    library;
  size_t                       size;
  uint64_t                     hash;
  uint32_t                     activeCount;
  uint32_t                     i;
  int                          ok;
  bool                         allocated;

  memset(values, 0xa5, sizeof(values));
  memset(name, 'e', nameSize - 1u);
  name[nameSize - 1u] = '\0';

  for (i = 0u; i < count; i++) {
    values[i].id   = UINT32_MAX - i * 3u;
    values[i].type = type == 5u ? (GPUConstantType)(GPU_CONSTANT_BOOL + i % 4u)
                               : (GPUConstantType)type;

    switch (values[i].type) {
      case GPU_CONSTANT_BOOL: values[i].value.boolean = i % 2u != 0u; break;
      case GPU_CONSTANT_I32:  values[i].value.i32 = INT32_MIN + (int32_t)i; break;
      case GPU_CONSTANT_U32:  values[i].value.u32 = UINT32_MAX - i; break;
      case GPU_CONSTANT_F32:  values[i].value.f32 = i ? (float)i : -0.0f; break;
      default: return 0;
    }
  }

  memcpy(original, values, sizeof(original));

  constants.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.chain.structSize = sizeof(constants);
  constants.pConstants       = values;
  constants.constantCount    = count;
  info.chain.pNext           = &constants.chain;
  info.layout                = (GPUPipelineLayout *)(uintptr_t)0x1234u;
  info.library               = (GPUShaderLibrary *)(uintptr_t)0x5678u;
  info.entryPoint            = name;

  for (i = 0u; i < depth; i++) {
    prefix[i].sType   = GPU_STRUCTURE_TYPE_INTERSECTION_FUNCTION_PIPELINE_EXT;
    prefix[i].pNext   = info.chain.pNext;
    info.chain.pNext = &prefix[i];
  }

  size        = 0u;
  layout      = (uintptr_t)info.layout;
  library     = (uintptr_t)info.library;
  activeCount = depth < 3u ? count : 0u;
  KEY_FIELD(layout);
  KEY_FIELD(library);
  appendBytes(expected, &size, name, nameSize);
  KEY_FIELD(activeCount);

  for (i = 0u; i < activeCount; i++) {
    KEY_FIELD(values[i].id);
    KEY_FIELD(values[i].type);

    switch (values[i].type) {
      case GPU_CONSTANT_BOOL: KEY_FIELD(values[i].value.boolean); break;
      case GPU_CONSTANT_I32:  KEY_FIELD(values[i].value.i32); break;
      case GPU_CONSTANT_U32:  KEY_FIELD(values[i].value.u32); break;
      case GPU_CONSTANT_F32:  KEY_FIELD(values[i].value.f32); break;
      default: return 0;
    }
  }

  hash = 14695981039346656037ull;

  for (i = 0u; i < size; i++) {
    hash ^= expected[i];
    hash *= 1099511628211ull;
  }

  allocationCalls = releaseCalls = 0u;

  if (pipelineCacheFindCompute(cache, &info, &key, &pipeline) != GPU_OK) {
    return 0;
  }

  allocated = size > sizeof(key.inlineData);
  ok        = !pipeline && key.size == size && key.hash == hash
              && memcmp(key.data, expected, size) == 0 && key.ownsData == allocated
              && allocationCalls == (uint32_t)allocated
              && (allocated || key.data == key.inlineData)
              && memcmp(values, original, sizeof(values)) == 0;
  pipelineCacheReleaseKey(&key);

  ok = ok && releaseCalls == (uint32_t)allocated
       && !key.data && !key.size && !key.hash && !key.ownsData;

  if (allocated) {
    failAllocation = true;
    allocationCalls = releaseCalls = 0u;

    ok = ok && pipelineCacheFindCompute(cache, &info, &key, &pipeline) == GPU_ERROR_OUT_OF_MEMORY
         && !pipeline && !key.data && !key.hash && !key.ownsData
         && allocationCalls == 1u && releaseCalls == 0u;
    failAllocation = false;
    pipelineCacheReleaseKey(&key);
  }

  if (!ok) {
    fprintf(stderr, "compute key: count=%u type=%u name=%u depth=%u\n", count, type, nameSize, depth);
  }

  return ok;
}

static int
checkInvalid(GPUPipelineCache *cache) {
  GPUConstant                  value     = {0};
  GPUPipelineConstants         constants = {0};
  GPUComputePipelineCreateInfo info      = {0};
  GPUPipelineCacheKey          key;
  GPUComputePipeline          *pipeline  = NULL;
  uint32_t                     i;

  constants.chain.sType   = GPU_STRUCTURE_TYPE_PIPELINE_CONSTANTS;
  constants.pConstants    = &value;
  constants.constantCount = 1u;
  info.chain.pNext        = &constants.chain;
  info.entryPoint         = "entry";

  for (i = 0u; i < 3u; i++) {
    value.type      = i == 0u ? (GPUConstantType)0u : i == 1u ? (GPUConstantType)5u
                                                          : (GPUConstantType)UINT32_MAX;
    allocationCalls = releaseCalls = 0u;

    if (pipelineCacheFindCompute(cache, &info, &key, &pipeline) != GPU_ERROR_OUT_OF_MEMORY
        || pipeline || key.data || key.size || key.hash || key.ownsData
        || allocationCalls || releaseCalls) {
      return 0;
    }
  }

  return 1;
}

/* only the real cache writer/find path runs; native creation stays unused. */
GPUShaderFunction*
shaderFunction(GPUShaderLibrary *library, const char *name) {
  (void)library;
  (void)name;
  unexpectedCalls++;
  return NULL;
}

GPUResult
createRenderPipeline(GPUDevice                         *device,
                     const GPURenderPipelineCreateInfo *info,
                     GPURenderPipeline                **outPipeline) {
  (void)device;
  (void)info;
  (void)outPipeline;
  unexpectedCalls++;
  return GPU_ERROR_OUT_OF_MEMORY;
}

void
retainRayTracingPipeline(GPURayTracingPipelineEXT *pipeline) {
  (void)pipeline;
  unexpectedCalls++;
}

void
retainExecutionGraph(GPUExecutionGraphEXT *graph) {
  (void)graph;
  unexpectedCalls++;
}

void*
pipelineKeyTestAllocate(size_t size) {
  allocationCalls++;

  if (failAllocation) {
    faultCases++;
  }

  return failAllocation ? NULL : malloc(size);
}

void
pipelineKeyTestFree(void *data) {
  releaseCalls++;
  free(data);
}

int
main(void) {
  GPUPipelineCacheCreateInfo info   = {0};
  GPUDevice                  device = {0};
  GPUPipelineCache           *cache  = NULL;
  uint32_t                   count;
  uint32_t                   type;
  uint32_t                   name;
  uint32_t                   depth;
  uint32_t                   mode;
  uint32_t                   attrs;
  int                        ok = 0;

  if (initPipelineCacheDevice(&device) != GPU_OK
      || GPUCreatePipelineCache(&device, &info, &cache) != GPU_OK) {
    goto cleanup;
  }

  for (count = 0u; count <= 128u; count++) {
    for (type = GPU_CONSTANT_BOOL; type <= 5u; type++) {
      for (name = 0u; name < sizeof(nameSizes) / sizeof(nameSizes[0]); name++) {
        for (depth = 0u; depth <= 3u; depth++) {
          if (!checkKey(cache, count, type, nameSizes[name], depth)) {
            goto cleanup;
          }
        }
      }
    }
  }

  for (count = 0u; count < sizeof(counts) / sizeof(counts[0]); count++) {
    for (attrs = 0u; attrs < sizeof(renderAttrs) / sizeof(renderAttrs[0]); attrs++) {
      for (name = 0u; name < sizeof(renderNames) / sizeof(renderNames[0]); name++) {
        for (depth = 0u; depth <= 3u; depth++) {
          for (mode = 0u; mode < 4u; mode++) {
            if (!checkRenderKey(cache, counts[count], renderAttrs[attrs], renderNames[name], depth, mode)) {
              goto cleanup;
            }
          }
        }
      }
    }
  }

  for (count = 0u; count < sizeof(rayCounts) / sizeof(rayCounts[0]); count++) {
    for (name = 0u; name < sizeof(rayNames) / sizeof(rayNames[0]); name++) {
      for (mode = 0u; mode < 16u; mode++) {
        if (!checkRayKey(cache, rayCounts[count], rayNames[name], mode)) {
          goto cleanup;
        }
      }
    }
  }

  for (name = 0u; name < sizeof(graphNames) / sizeof(graphNames[0]); name++) {
    if (!checkGraphKey(cache, graphNames[name])) {
      goto cleanup;
    }
  }

  if (!checkInvalid(cache) || !checkInvalidStateKeys(cache) || unexpectedCalls) {
    goto cleanup;
  }

  puts("compute keys: 20640 byte/hash/allocation cases and 3 invalid types passed");
  puts("state keys: 1728 render, 288 ray, 7 graph cases and 5 invalid inputs passed");
  printf("allocation faults checked: %u\n", faultCases);
  ok = 1;

cleanup:
  GPUDestroyPipelineCache(cache);
  destroyPipelineCacheDevice(&device);
  return ok ? 0 : 1;
}
