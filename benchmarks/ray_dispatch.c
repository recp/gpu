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

#include "api/device_internal.h"
#include "api/ray_internal.h"
#include "backend/api/gpudef.h"
#include "bench.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#  define BENCH_NOINLINE __declspec(noinline)
#else
#  define BENCH_NOINLINE __attribute__((noinline))
#endif

enum {
  RAY_DISPATCH_REPEATS           = 7,
  RAY_DISPATCH_WARMUP_ITERATIONS = 1000000
};

typedef enum RayDispatchPath {
  RAY_DISPATCH_DIRECT = 0,
  RAY_DISPATCH_VTABLE,
  RAY_DISPATCH_PUBLIC,
  RAY_DISPATCH_PATH_COUNT
} RayDispatchPath;

static GPUApi * volatile rayDispatchApi;
static volatile uint64_t rayDispatchSink;

#if GPU_BACKEND_METAL_ONLY
#  define RAY_DISPATCH_BACKEND_MODE "metal-only"
#elif GPU_BACKEND_VULKAN_ONLY
#  define RAY_DISPATCH_BACKEND_MODE "vulkan-only"
#elif GPU_BACKEND_DX12_ONLY
#  define RAY_DISPATCH_BACKEND_MODE "dx12-only"
#else
#  define RAY_DISPATCH_BACKEND_MODE "multi"
#endif

static BENCH_NOINLINE void
ray_dispatch(GPURayTracingPassEncoderEXT *pass,
             GPUShaderTableEXT           *table,
             uint32_t                     width,
             uint32_t                     height,
             uint32_t                     depth) {
  rayDispatchSink += (uint64_t)(pass != NULL) +
                     (uint64_t)(table != NULL) +
                     width + height + depth;
}

static double
ray_run(RayDispatchPath             path,
        GPURayTracingPassEncoderEXT *pass,
        GPUShaderTableEXT           *table,
        uint64_t                     iterations) {
  double begin;
  double end;

  begin = bench_now();
  switch (path) {
    case RAY_DISPATCH_DIRECT:
      for (uint64_t i = 0u; i < iterations; i++) {
        ray_dispatch(pass, table, 1u, 1u, 1u);
      }
      break;
    case RAY_DISPATCH_VTABLE:
      for (uint64_t i = 0u; i < iterations; i++) {
        rayDispatchApi->rayTracing.dispatch(pass, table, 1u, 1u, 1u);
      }
      break;
    case RAY_DISPATCH_PUBLIC:
      for (uint64_t i = 0u; i < iterations; i++) {
        GPUDispatchRaysEXT(pass, table, 1u, 1u, 1u);
      }
      break;
    default:
      return 0.0;
  }
  end = bench_now();
  return (end - begin) * 1e9 / (double)iterations;
}

static bool
ray_parseIterations(const char *value, uint64_t *outIterations) {
  unsigned long long parsed;
  char              *end;

  if (!value || !outIterations) {
    return false;
  }
  errno  = 0;
  parsed = strtoull(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < 10000u) {
    return false;
  }
  *outIterations = (uint64_t)parsed;
  return true;
}

int
main(int argc, char *argv[]) {
  GPURayTracingPassEncoderEXT pass;
  GPURayTracingPipelineEXT    pipeline;
  GPUShaderTableEXT           table;
  GPUDevice                   device;
  GPUApi                      api;
  double                      samples[RAY_DISPATCH_PATH_COUNT]
                                     [RAY_DISPATCH_REPEATS];
  double                      median[RAY_DISPATCH_PATH_COUNT];
  uint64_t                    iterations;

  iterations = 20000000u;
  if (argc > 2 ||
      (argc == 2 && !ray_parseIterations(argv[1], &iterations))) {
    fprintf(stderr, "usage: %s [iterations >= 10000]\n", argv[0]);
    return EXIT_FAILURE;
  }

  memset(&api, 0, sizeof(api));
  memset(&device, 0, sizeof(device));
  memset(&pipeline, 0, sizeof(pipeline));
  memset(&table, 0, sizeof(table));
  memset(&pass, 0, sizeof(pass));

  api.rayTracing.dispatch    = ray_dispatch;
  device._api               = &api;
  device.enabledFeatureMask = 1ull << GPU_FEATURE_RAY_TRACING_PIPELINE;
  pipeline._api             = &api;
  pipeline.device           = &device;
  table._api                = &api;
  table.device              = &device;
  table.pipeline            = &pipeline;
  pass._api                 = &api;
  pass.device               = &device;
  pass._pipeline            = &pipeline;
  pass.hasPipeline          = true;
  rayDispatchApi            = &api;

  for (uint32_t path = 0u; path < RAY_DISPATCH_PATH_COUNT; path++) {
    ray_run((RayDispatchPath)path,
            &pass,
            &table,
            RAY_DISPATCH_WARMUP_ITERATIONS);
  }

  for (uint32_t repeat = 0u; repeat < RAY_DISPATCH_REPEATS; repeat++) {
    if ((repeat & 1u) == 0u) {
      for (uint32_t path = 0u; path < RAY_DISPATCH_PATH_COUNT; path++) {
        samples[path][repeat] = ray_run((RayDispatchPath)path,
                                        &pass,
                                        &table,
                                        iterations);
      }
    } else {
      for (uint32_t path = RAY_DISPATCH_PATH_COUNT; path-- > 0u;) {
        samples[path][repeat] = ray_run((RayDispatchPath)path,
                                        &pass,
                                        &table,
                                        iterations);
      }
    }
  }

  for (uint32_t path = 0u; path < RAY_DISPATCH_PATH_COUNT; path++) {
    median[path] = bench_percentile(samples[path],
                                    RAY_DISPATCH_REPEATS,
                                    0.5);
  }

  printf("GPU ray dispatch microbenchmark\n");
  printf("build: %s, validation: %s\n",
         RAY_DISPATCH_BACKEND_MODE,
         GPU_BUILD_WITH_VALIDATION ? "on" : "off");
  printf("iterations: %" PRIu64 ", repeats: %u\n",
         iterations,
         RAY_DISPATCH_REPEATS);
  printf("direct callback       : %8.3f ns/call\n",
         median[RAY_DISPATCH_DIRECT]);
  printf("vtable callback       : %8.3f ns/call  delta %+7.3f ns\n",
         median[RAY_DISPATCH_VTABLE],
         median[RAY_DISPATCH_VTABLE] - median[RAY_DISPATCH_DIRECT]);
  printf("public dispatch rays  : %8.3f ns/call  delta %+7.3f ns vs vtable\n",
         median[RAY_DISPATCH_PUBLIC],
         median[RAY_DISPATCH_PUBLIC] - median[RAY_DISPATCH_VTABLE]);
  printf("sink: %" PRIu64 "\n", rayDispatchSink);
  return EXIT_SUCCESS;
}
