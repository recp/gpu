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

#include "api/cmdqueue_internal.h"
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

typedef enum BenchPath {
  BENCH_PATH_DIRECT = 0,
  BENCH_PATH_VTABLE,
  BENCH_PATH_PUBLIC,
  BENCH_PATH_COUNT
} BenchPath;

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

static double
bench_run(BenchPath path,
          GPURenderPassEncoder *pass,
          uint64_t iterations) {
  double begin;
  double end;

  begin = bench_now();
  switch (path) {
    case BENCH_PATH_DIRECT:
      for (uint64_t i = 0u; i < iterations; i++) {
        bench_draw(pass, GPUPrimitiveTypeTriangle, 0u, 3u, 1u, 0u);
      }
      break;
    case BENCH_PATH_VTABLE:
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
    case BENCH_PATH_PUBLIC:
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
  GPURenderPassEncoder pass;
  GPUCommandBuffer     cmdb;
  GPUQueue             queue;
  GPUDevice            device;
  GPUApi               api;
  double               samples[BENCH_PATH_COUNT][BENCH_REPEATS];
  double               median[BENCH_PATH_COUNT];
  uint64_t             iterations;

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

  api.rce.drawPrimitives = bench_draw;
  device._api            = &api;
  queue._device          = &device;
  cmdb._queue            = &queue;
  pass._cmdb             = &cmdb;
  pass._primitiveType    = GPUPrimitiveTypeTriangle;
  pass._hasPipeline      = true;
  benchApi               = &api;

  for (uint32_t path = 0u; path < BENCH_PATH_COUNT; path++) {
    bench_run((BenchPath)path, &pass, BENCH_WARMUP_ITERATIONS);
  }

  for (uint32_t repeat = 0u; repeat < BENCH_REPEATS; repeat++) {
    if ((repeat & 1u) == 0u) {
      for (uint32_t path = 0u; path < BENCH_PATH_COUNT; path++) {
        samples[path][repeat] = bench_run((BenchPath)path, &pass, iterations);
      }
    } else {
      for (uint32_t path = BENCH_PATH_COUNT; path-- > 0u;) {
        samples[path][repeat] = bench_run((BenchPath)path, &pass, iterations);
      }
    }
  }

  for (uint32_t path = 0u; path < BENCH_PATH_COUNT; path++) {
    median[path] = bench_percentile(samples[path], BENCH_REPEATS, 0.5);
  }

  printf("GPU dispatch microbenchmark\n");
  printf("build: %s, validation: %s\n",
         BENCH_BACKEND_MODE,
         GPU_BUILD_WITH_VALIDATION ? "on" : "off");
  printf("iterations: %" PRIu64 ", repeats: %u\n",
         iterations,
         BENCH_REPEATS);
  printf("direct callback : %8.3f ns/call\n", median[BENCH_PATH_DIRECT]);
  printf("vtable callback : %8.3f ns/call  delta %+7.3f ns\n",
         median[BENCH_PATH_VTABLE],
         median[BENCH_PATH_VTABLE] - median[BENCH_PATH_DIRECT]);
  printf("public GPUDraw  : %8.3f ns/call  delta %+7.3f ns vs vtable\n",
         median[BENCH_PATH_PUBLIC],
         median[BENCH_PATH_PUBLIC] - median[BENCH_PATH_VTABLE]);
  printf("sink: %" PRIu64 "\n", benchSink);
  return EXIT_SUCCESS;
}
