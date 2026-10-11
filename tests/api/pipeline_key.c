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

static const uint32_t nameSizes[] = {1u, 19u, 235u, 236u, 237u, 256u, 257u, 1024u};

static uint32_t allocationCalls;
static uint32_t releaseCalls;
static uint32_t unexpectedCalls;
static bool     failAllocation;

static void
appendBytes(uint8_t *bytes, size_t *size, const void *value, size_t count) {
  memcpy(bytes + *size, value, count);
  *size += count;
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

  if (!checkInvalid(cache) || unexpectedCalls) {
    goto cleanup;
  }

  puts("compute keys: 20640 byte/hash/allocation cases and 3 invalid types passed");
  ok = 1;

cleanup:
  GPUDestroyPipelineCache(cache);
  destroyPipelineCacheDevice(&device);
  return ok ? 0 : 1;
}
