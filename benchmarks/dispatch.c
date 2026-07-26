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

#include "api/buffer_internal.h"
#include "api/cmdqueue_internal.h"
#include "api/descr/descriptor_internal.h"
#include "backend/api/gpudef.h"
#include "bench.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_MSC_VER)
#  define BENCH_NOINLINE __declspec(noinline)
#else
#  define BENCH_NOINLINE __attribute__((noinline))
#endif

enum {
  BENCH_REPEATS           = 7,
  BENCH_WARMUP_ITERATIONS = 1000000
};

typedef enum BenchDrawPath {
  BENCH_DRAW_DIRECT = 0,
  BENCH_DRAW_VTABLE,
  BENCH_DRAW_PUBLIC,
  BENCH_DRAW_COUNT
} BenchDrawPath;

typedef enum BenchBindPath {
  BENCH_BIND_DIRECT = 0,
  BENCH_BIND_VTABLE,
  BENCH_BIND_PUBLIC_EMIT,
  BENCH_BIND_PUBLIC_SHADOW,
  BENCH_BIND_PUBLIC_DYNAMIC_EMIT,
  BENCH_BIND_PUBLIC_DYNAMIC_SHADOW,
  BENCH_BIND_COUNT
} BenchBindPath;

typedef enum BenchVertexPath {
  BENCH_VERTEX_DIRECT = 0,
  BENCH_VERTEX_VTABLE,
  BENCH_VERTEX_PUBLIC_EMIT,
  BENCH_VERTEX_PUBLIC_SHADOW,
  BENCH_VERTEX_COUNT
} BenchVertexPath;

typedef enum BenchAllocPath {
  BENCH_ALLOC_DIRECT = 0,
  BENCH_ALLOC_PUBLIC,
  BENCH_ALLOC_COUNT
} BenchAllocPath;

static GPUApi * volatile benchApi;
static volatile uint64_t benchSink;

#if GPU_BACKEND_METAL_ONLY
#  define BENCH_BACKEND_MODE "metal-only"
#elif GPU_BACKEND_VULKAN_ONLY
#  define BENCH_BACKEND_MODE "vulkan-only"
#elif GPU_BACKEND_DX12_ONLY
#  define BENCH_BACKEND_MODE "dx12-only"
#else
#  define BENCH_BACKEND_MODE "multi"
#endif

static BENCH_NOINLINE void
bench_draw(GPURenderPassEncoder *pass,
           GPUPrimitiveType      type,
           size_t                start,
           size_t                count,
           uint32_t              instanceCount,
           uint32_t              firstInstance) {
  benchSink += (uint64_t)(pass != NULL) +
               (uint64_t)type +
               (uint64_t)start +
               (uint64_t)count +
               instanceCount +
               firstInstance;
}

static BENCH_NOINLINE bool
bench_bind(GPURenderPassEncoder *pass,
           GPUPipelineLayout    *pipelineLayout,
           uint32_t              groupIndex,
           GPUBindGroup         *group,
           uint32_t              dynamicOffsetCount,
           const uint32_t       *dynamicOffsets) {
  benchSink += (uint64_t)(pass != NULL) +
               (uint64_t)(pipelineLayout != NULL) +
               groupIndex +
               (uint64_t)(group != NULL) +
               dynamicOffsetCount +
               (uint64_t)(dynamicOffsets != NULL);
  return true;
}

static BENCH_NOINLINE void
bench_vertex(GPURenderPassEncoder *pass,
             GPUBuffer            *buffer,
             uint64_t              offset,
             uint32_t              index) {
  benchSink += (uint64_t)(pass != NULL) +
               (uint64_t)(buffer != NULL) +
               offset +
               index;
}

static BENCH_NOINLINE void
bench_alloc(GPUDevice                *device,
            GPUTransientBufferSlice *slice) {
  uint64_t offset;

  offset                       = device->transientFrameOffset++;
  slice->buffer                = device->transientBuffer;
  slice->cpuPtr                = (uint8_t *)device->transientCpuPtr + offset;
  slice->offset                = offset;
  slice->sizeBytes             = 1u;
}

static double
bench_runDraw(BenchDrawPath         path,
              GPURenderPassEncoder *pass,
              uint64_t              iterations) {
  double begin;
  double end;

  begin = bench_now();
  switch (path) {
    case BENCH_DRAW_DIRECT:
      for (uint64_t i = 0u; i < iterations; i++) {
        bench_draw(pass, GPUPrimitiveTypeTriangle, 0u, 3u, 1u, 0u);
      }
      break;
    case BENCH_DRAW_VTABLE:
      for (uint64_t i = 0u; i < iterations; i++) {
        benchApi->rce.drawPrimitives(
          pass,
          GPUPrimitiveTypeTriangle,
          0u,
          3u,
          1u,
          0u
        );
      }
      break;
    case BENCH_DRAW_PUBLIC:
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUDraw(pass, 3u, 1u, 0u, 0u);
      }
      break;
    default:
      return 0.0;
  }
  end = bench_now();
  return (end - begin) * 1e9 / (double)iterations;
}

static double
bench_runBind(BenchBindPath         path,
              GPURenderPassEncoder *pass,
              GPUBindGroup          *groups[2],
              uint64_t               iterations) {
  uint32_t offsets[2] = {0u, 256u};
  double begin;
  double end;

  pass->_boundGroups[0]              = NULL;
  pass->_boundDynamicOffsetCounts[0] = 0u;
  begin = bench_now();
  switch (path) {
    case BENCH_BIND_DIRECT:
      for (uint64_t i = 0u; i < iterations; i++) {
        bench_bind(pass,
                   pass->_pipelineLayout,
                   0u,
                   groups[i & 1u],
                   0u,
                   NULL);
      }
      break;
    case BENCH_BIND_VTABLE:
      for (uint64_t i = 0u; i < iterations; i++) {
        benchApi->descriptor.bindRenderGroup(pass,
                                             pass->_pipelineLayout,
                                             0u,
                                             groups[i & 1u],
                                             0u,
                                             NULL);
      }
      break;
    case BENCH_BIND_PUBLIC_EMIT:
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUBindRenderGroup(pass, 0u, groups[i & 1u], 0u, NULL);
      }
      break;
    case BENCH_BIND_PUBLIC_SHADOW:
      pass->_boundGroups[0] = groups[0];
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUBindRenderGroup(pass, 0u, groups[0], 0u, NULL);
      }
      break;
    case BENCH_BIND_PUBLIC_DYNAMIC_EMIT:
      pass->_boundGroups[0] = groups[0];
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUBindRenderGroup(pass, 0u, groups[0], 1u, &offsets[i & 1u]);
      }
      break;
    case BENCH_BIND_PUBLIC_DYNAMIC_SHADOW:
      pass->_boundGroups[0]              = groups[0];
      pass->_boundDynamicOffsetCounts[0] = 1u;
      pass->_boundDynamicOffsets[0][0]   = offsets[0];
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUBindRenderGroup(pass, 0u, groups[0], 1u, &offsets[0]);
      }
      break;
    default:
      return 0.0;
  }
  end = bench_now();
  return (end - begin) * 1e9 / (double)iterations;
}

static double
bench_runVertex(BenchVertexPath      path,
                GPURenderPassEncoder *pass,
                GPUBuffer            *buffer,
                uint64_t              iterations) {
  GPUBufferBinding binding;
  double           begin;
  double           end;

  memset(&binding, 0, sizeof(binding));
  binding.buffer = buffer;
  pass->_vertexBufferMask = 0u;
  begin = bench_now();
  switch (path) {
    case BENCH_VERTEX_DIRECT:
      for (uint64_t i = 0u; i < iterations; i++) {
        bench_vertex(pass, buffer, i & 1u, 0u);
      }
      break;
    case BENCH_VERTEX_VTABLE:
      for (uint64_t i = 0u; i < iterations; i++) {
        benchApi->rce.vertexInputBuffer(pass, buffer, i & 1u, 0u);
      }
      break;
    case BENCH_VERTEX_PUBLIC_EMIT:
      for (uint64_t i = 0u; i < iterations; i++) {
        binding.offset = i & 1u;
        GPUBindVertexBuffers(pass, 0u, 1u, &binding);
      }
      break;
    case BENCH_VERTEX_PUBLIC_SHADOW:
      pass->_vertexBuffers[0]       = buffer;
      pass->_vertexBufferOffsets[0] = 0u;
      pass->_vertexBufferMask       = 1u;
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUBindVertexBuffers(pass, 0u, 1u, &binding);
      }
      break;
    default:
      return 0.0;
  }
  end = bench_now();
  return (end - begin) * 1e9 / (double)iterations;
}

static double
bench_runAlloc(BenchAllocPath path,
               GPUDevice     *device,
               uint64_t       iterations) {
  GPUTransientBufferSlice slice;
  double                  begin;
  double                  end;

  memset(&slice, 0, sizeof(slice));
  device->transientFrameOffset = 0u;
  begin = bench_now();
  switch (path) {
    case BENCH_ALLOC_DIRECT:
      for (uint64_t i = 0u; i < iterations; i++) {
        bench_alloc(device, &slice);
      }
      break;
    case BENCH_ALLOC_PUBLIC:
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUAllocateTransientBuffer(device,
                                   GPU_BUFFER_USAGE_UNIFORM,
                                   1u,
                                   1u,
                                   &slice);
      }
      break;
    default:
      return 0.0;
  }
  end = bench_now();
  benchSink += slice.offset;
  return (end - begin) * 1e9 / (double)iterations;
}

static int
bench_parseIterations(const char *value, uint64_t *outIterations) {
  unsigned long long parsed;
  char              *end;

  if (!value || !outIterations) {
    return 0;
  }
  errno  = 0;
  parsed = strtoull(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < 10000u) {
    return 0;
  }
  *outIterations = (uint64_t)parsed;
  return 1;
}

int
main(int argc, char *argv[]) {
  GPUBindGroupLayoutEntry layoutEntry;
  GPUBindGroupLayoutPriv  bindGroupLayoutPriv;
  GPUPipelineLayoutPriv   pipelineLayoutPriv;
  GPUBindGroupPriv        bindGroupPriv[2];
  GPUBindGroupLayout     *pipelineLayouts[1];
  GPUBindGroup           *bindGroups[2];
  GPUBindGroupLayout      bindGroupLayout;
  GPUPipelineLayout       pipelineLayout;
  GPUBindGroup            bindGroup[2];
  GPUBuffer               buffer;
  void                   *transientBytes;
  GPURenderPassEncoder   pass;
  GPUCommandBuffer       cmdb;
  GPUQueue               queue;
  GPUDevice              device;
  GPUApi                 api;
  double                 drawSamples[BENCH_DRAW_COUNT][BENCH_REPEATS];
  double                 bindSamples[BENCH_BIND_COUNT][BENCH_REPEATS];
  double                 vertexSamples[BENCH_VERTEX_COUNT][BENCH_REPEATS];
  double                 allocSamples[BENCH_ALLOC_COUNT][BENCH_REPEATS];
  double                 drawMedian[BENCH_DRAW_COUNT];
  double                 bindMedian[BENCH_BIND_COUNT];
  double                 vertexMedian[BENCH_VERTEX_COUNT];
  double                 allocMedian[BENCH_ALLOC_COUNT];
  uint64_t               iterations;

  iterations = 20000000u;
  if (argc > 2 || (argc == 2 && !bench_parseIterations(argv[1], &iterations))) {
    fprintf(stderr, "usage: %s [iterations >= 10000]\n", argv[0]);
    return EXIT_FAILURE;
  }

  memset(&api, 0, sizeof(api));
  memset(&device, 0, sizeof(device));
  memset(&queue, 0, sizeof(queue));
  memset(&cmdb, 0, sizeof(cmdb));
  memset(&pass, 0, sizeof(pass));
  memset(&layoutEntry, 0, sizeof(layoutEntry));
  memset(&bindGroupLayoutPriv, 0, sizeof(bindGroupLayoutPriv));
  memset(&pipelineLayoutPriv, 0, sizeof(pipelineLayoutPriv));
  memset(&bindGroupPriv, 0, sizeof(bindGroupPriv));
  memset(&bindGroupLayout, 0, sizeof(bindGroupLayout));
  memset(&pipelineLayout, 0, sizeof(pipelineLayout));
  memset(&bindGroup, 0, sizeof(bindGroup));
  memset(&buffer, 0, sizeof(buffer));
  transientBytes = NULL;

  if (iterations == UINT64_MAX || iterations > SIZE_MAX ||
      !(transientBytes = malloc((size_t)iterations + 1u))) {
    fprintf(stderr, "failed to allocate transient benchmark storage\n");
    return EXIT_FAILURE;
  }

  layoutEntry.binding                       = 0u;
  layoutEntry.bindingType                   = GPU_BINDING_UNIFORM_BUFFER;
  layoutEntry.visibility                    = GPU_SHADER_STAGE_FRAGMENT_BIT;
  layoutEntry.arrayCount                    = 1u;
  bindGroupLayoutPriv.entries               = &layoutEntry;
  bindGroupLayoutPriv.count                 = 1u;
  bindGroupLayout._priv                     = &bindGroupLayoutPriv;
  pipelineLayouts[0]                        = &bindGroupLayout;
  pipelineLayoutPriv.bindGroupLayouts     = pipelineLayouts;
  pipelineLayoutPriv.bindGroupLayoutCount = 1u;
  pipelineLayout._priv                   = &pipelineLayoutPriv;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(bindGroup); i++) {
    bindGroupPriv[i].layout = &bindGroupLayout;
    bindGroup[i]._priv      = &bindGroupPriv[i];
    bindGroups[i]           = &bindGroup[i];
  }

  api.rce.drawPrimitives             = bench_draw;
  api.rce.vertexInputBuffer          = bench_vertex;
  api.descriptor.bindRenderGroup     = bench_bind;
  device._api                        = &api;
  device.transientBuffer             = &buffer;
  device.transientCpuPtr             = transientBytes;
  device.transientBufferUsage        = GPU_BUFFER_USAGE_UNIFORM;
  device.transientConfig.ringBytesPerFrame = iterations + 1u;
  device.transientFrameStride             = iterations + 1u;
  device.transientConfigured              = true;
  queue._device                      = &device;
  cmdb._queue                        = &queue;
  pass._api                          = &api;
  pass._device                       = &device;
  pass._cmdb                         = &cmdb;
  pass._pipelineLayout               = &pipelineLayout;
  pass._drawPrimitives               = bench_draw;
  pass._vertexInputBuffer            = bench_vertex;
  pass._bindRenderGroup              = bench_bind;
  pass._primitiveType                = GPUPrimitiveTypeTriangle;
  pass._hasPipeline                  = true;
  pass._boundGroupLayouts[0]         = &bindGroupLayout;
  benchApi                           = &api;

  for (uint32_t path = 0u; path < BENCH_DRAW_COUNT; path++) {
    bench_runDraw((BenchDrawPath)path, &pass, BENCH_WARMUP_ITERATIONS);
  }
  for (uint32_t path = 0u; path < BENCH_BIND_COUNT; path++) {
    bench_runBind((BenchBindPath)path,
                  &pass,
                  bindGroups,
                  BENCH_WARMUP_ITERATIONS);
  }
  for (uint32_t path = 0u; path < BENCH_VERTEX_COUNT; path++) {
    bench_runVertex((BenchVertexPath)path,
                    &pass,
                    &buffer,
                    BENCH_WARMUP_ITERATIONS);
  }
  for (uint32_t path = 0u; path < BENCH_ALLOC_COUNT; path++) {
    bench_runAlloc((BenchAllocPath)path, &device, BENCH_WARMUP_ITERATIONS);
  }

  for (uint32_t repeat = 0u; repeat < BENCH_REPEATS; repeat++) {
    if ((repeat & 1u) == 0u) {
      for (uint32_t path = 0u; path < BENCH_DRAW_COUNT; path++) {
        drawSamples[path][repeat] =
          bench_runDraw((BenchDrawPath)path, &pass, iterations);
      }
      for (uint32_t path = 0u; path < BENCH_BIND_COUNT; path++) {
        bindSamples[path][repeat] =
          bench_runBind((BenchBindPath)path, &pass, bindGroups, iterations);
      }
      for (uint32_t path = 0u; path < BENCH_VERTEX_COUNT; path++) {
        vertexSamples[path][repeat] =
          bench_runVertex((BenchVertexPath)path, &pass, &buffer, iterations);
      }
      for (uint32_t path = 0u; path < BENCH_ALLOC_COUNT; path++) {
        allocSamples[path][repeat] =
          bench_runAlloc((BenchAllocPath)path, &device, iterations);
      }
    } else {
      for (uint32_t path = BENCH_ALLOC_COUNT; path-- > 0u;) {
        allocSamples[path][repeat] =
          bench_runAlloc((BenchAllocPath)path, &device, iterations);
      }
      for (uint32_t path = BENCH_VERTEX_COUNT; path-- > 0u;) {
        vertexSamples[path][repeat] =
          bench_runVertex((BenchVertexPath)path, &pass, &buffer, iterations);
      }
      for (uint32_t path = BENCH_BIND_COUNT; path-- > 0u;) {
        bindSamples[path][repeat] =
          bench_runBind((BenchBindPath)path, &pass, bindGroups, iterations);
      }
      for (uint32_t path = BENCH_DRAW_COUNT; path-- > 0u;) {
        drawSamples[path][repeat] =
          bench_runDraw((BenchDrawPath)path, &pass, iterations);
      }
    }
  }

  for (uint32_t path = 0u; path < BENCH_DRAW_COUNT; path++) {
    drawMedian[path] =
      bench_percentile(drawSamples[path], BENCH_REPEATS, 0.5);
  }
  for (uint32_t path = 0u; path < BENCH_BIND_COUNT; path++) {
    bindMedian[path] =
      bench_percentile(bindSamples[path], BENCH_REPEATS, 0.5);
  }
  for (uint32_t path = 0u; path < BENCH_VERTEX_COUNT; path++) {
    vertexMedian[path] =
      bench_percentile(vertexSamples[path], BENCH_REPEATS, 0.5);
  }
  for (uint32_t path = 0u; path < BENCH_ALLOC_COUNT; path++) {
    allocMedian[path] =
      bench_percentile(allocSamples[path], BENCH_REPEATS, 0.5);
  }

  printf("GPU dispatch microbenchmark\n");
  printf("build: %s, validation: %s\n",
         BENCH_BACKEND_MODE,
         GPU_BUILD_WITH_VALIDATION ? "on" : "off");
  printf("iterations: %" PRIu64 ", repeats: %u\n",
         iterations,
         BENCH_REPEATS);
  printf("direct draw callback : %8.3f ns/call\n",
         drawMedian[BENCH_DRAW_DIRECT]);
  printf("vtable callback : %8.3f ns/call  delta %+7.3f ns\n",
         drawMedian[BENCH_DRAW_VTABLE],
         drawMedian[BENCH_DRAW_VTABLE] - drawMedian[BENCH_DRAW_DIRECT]);
  printf("public GPUDraw  : %8.3f ns/call  delta %+7.3f ns vs vtable\n",
         drawMedian[BENCH_DRAW_PUBLIC],
         drawMedian[BENCH_DRAW_PUBLIC] - drawMedian[BENCH_DRAW_VTABLE]);
  printf("direct bind callback : %8.3f ns/call\n",
         bindMedian[BENCH_BIND_DIRECT]);
  printf("vtable bind callback : %8.3f ns/call  delta %+7.3f ns\n",
         bindMedian[BENCH_BIND_VTABLE],
         bindMedian[BENCH_BIND_VTABLE] - bindMedian[BENCH_BIND_DIRECT]);
  printf("public bind emission : %8.3f ns/call  delta %+7.3f ns vs vtable\n",
         bindMedian[BENCH_BIND_PUBLIC_EMIT],
         bindMedian[BENCH_BIND_PUBLIC_EMIT] -
           bindMedian[BENCH_BIND_VTABLE]);
  printf("public bind shadow   : %8.3f ns/call\n",
         bindMedian[BENCH_BIND_PUBLIC_SHADOW]);
  printf("dynamic bind emission: %8.3f ns/call\n",
         bindMedian[BENCH_BIND_PUBLIC_DYNAMIC_EMIT]);
  printf("dynamic bind shadow  : %8.3f ns/call\n",
         bindMedian[BENCH_BIND_PUBLIC_DYNAMIC_SHADOW]);
  printf("direct vertex callback: %8.3f ns/call\n",
         vertexMedian[BENCH_VERTEX_DIRECT]);
  printf("vtable vertex callback: %8.3f ns/call  delta %+7.3f ns\n",
         vertexMedian[BENCH_VERTEX_VTABLE],
         vertexMedian[BENCH_VERTEX_VTABLE] - vertexMedian[BENCH_VERTEX_DIRECT]);
  printf("public vertex emission: %8.3f ns/call  delta %+7.3f ns vs vtable\n",
         vertexMedian[BENCH_VERTEX_PUBLIC_EMIT],
         vertexMedian[BENCH_VERTEX_PUBLIC_EMIT] -
           vertexMedian[BENCH_VERTEX_VTABLE]);
  printf("public vertex shadow  : %8.3f ns/call\n",
         vertexMedian[BENCH_VERTEX_PUBLIC_SHADOW]);
  printf("direct transient alloc: %8.3f ns/call\n",
         allocMedian[BENCH_ALLOC_DIRECT]);
  printf("public transient alloc: %8.3f ns/call  delta %+7.3f ns\n",
         allocMedian[BENCH_ALLOC_PUBLIC],
         allocMedian[BENCH_ALLOC_PUBLIC] - allocMedian[BENCH_ALLOC_DIRECT]);
  printf("sink: %" PRIu64 "\n", benchSink);
  free(transientBytes);
  return EXIT_SUCCESS;
}
