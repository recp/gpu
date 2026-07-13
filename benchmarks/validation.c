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
#include "api/descr/descriptor_internal.h"
#include "api/device_internal.h"
#include "backend/api/gpudef.h"
#include "bench.h"

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
  VALIDATION_REPEATS           = 7,
  VALIDATION_WARMUP_ITERATIONS = 1000000
};

#if GPU_BUILD_WITH_VALIDATION
static const GPUValidationMode validationModes[] = {
  GPU_VALIDATION_OFF,
  GPU_VALIDATION_BASIC,
  GPU_VALIDATION_FULL
};

static const char *validationModeNames[] = {
  "off",
  "basic",
  "full"
};
#else
static const GPUValidationMode validationModes[] = {
  GPU_VALIDATION_OFF
};

static const char *validationModeNames[] = {
  "removed"
};
#endif

typedef struct ValidationFixture {
  GPUPipelineLayout     *pipelineLayout;
  GPUBindGroupLayout    *bindGroupLayout;
  GPURenderPassEncoder   render;
  GPUComputePassEncoder  compute;
  GPUCommandBuffer       cmdb;
  GPUQueue               queue;
  GPUDevice              device;
  GPUApi                 api;
} ValidationFixture;

static volatile uint64_t validationSink;

static BENCH_NOINLINE void
validation_draw(GPURenderPassEncoder *pass,
                GPUPrimitiveType      type,
                size_t                firstVertex,
                size_t                vertexCount,
                uint32_t              instanceCount,
                uint32_t              firstInstance) {
  validationSink += (uint64_t)(pass != NULL) +
                    (uint64_t)type +
                    firstVertex +
                    vertexCount +
                    instanceCount +
                    firstInstance;
}

static BENCH_NOINLINE void
validation_dispatch(GPUComputePassEncoder *pass,
                    uint32_t               x,
                    uint32_t               y,
                    uint32_t               z) {
  validationSink += (uint64_t)(pass != NULL) + x + y + z;
}

static double
validation_runDraw(ValidationFixture *fixture,
                   GPUValidationMode  mode,
                   uint64_t           iterations) {
  double begin;

  fixture->device.runtimeConfig.validationMode = mode;
  begin = bench_now();
  for (uint64_t i = 0u; i < iterations; i++) {
    GPUDraw(&fixture->render, 3u, 1u, 0u, 0u);
  }
  return (bench_now() - begin) * 1e9 / (double)iterations;
}

static double
validation_runDispatch(ValidationFixture *fixture,
                       GPUValidationMode  mode,
                       uint64_t           iterations) {
  double begin;

  fixture->device.runtimeConfig.validationMode = mode;
  begin = bench_now();
  for (uint64_t i = 0u; i < iterations; i++) {
    GPUDispatch(&fixture->compute, 1u, 1u, 1u);
  }
  return (bench_now() - begin) * 1e9 / (double)iterations;
}

static double
validation_runDirectDraw(ValidationFixture *fixture, uint64_t iterations) {
  double begin;

  begin = bench_now();
  for (uint64_t i = 0u; i < iterations; i++) {
    validation_draw(&fixture->render,
                    GPUPrimitiveTypeTriangle,
                    0u,
                    3u,
                    1u,
                    0u);
  }
  return (bench_now() - begin) * 1e9 / (double)iterations;
}

static double
validation_runDirectDispatch(ValidationFixture *fixture,
                             uint64_t           iterations) {
  double begin;

  begin = bench_now();
  for (uint64_t i = 0u; i < iterations; i++) {
    validation_dispatch(&fixture->compute, 1u, 1u, 1u);
  }
  return (bench_now() - begin) * 1e9 / (double)iterations;
}

static bool
validation_init(ValidationFixture *fixture) {
  GPUBindGroupLayoutEntry      entry        = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo   = {0};
  GPUPipelineLayoutCreateInfo  pipelineInfo = {0};
  GPUBindGroupLayout          *layouts[1];

  memset(fixture, 0, sizeof(*fixture));
  fixture->api.rce.drawPrimitives    = validation_draw;
  fixture->api.compute.dispatch      = validation_dispatch;
  fixture->device._api               = &fixture->api;
  fixture->queue._device             = &fixture->device;
  fixture->cmdb._queue               = &fixture->queue;
  fixture->render._cmdb              = &fixture->cmdb;
  fixture->render._primitiveType     = GPUPrimitiveTypeTriangle;
  fixture->render._hasPipeline       = true;
  fixture->compute._cmdb             = &fixture->cmdb;
  fixture->compute._hasPipeline      = true;

  entry.binding                       = 0u;
  entry.bindingType                   = GPU_BINDING_UNIFORM_BUFFER;
  entry.visibility                    = GPU_SHADER_STAGE_VERTEX_BIT |
                                        GPU_SHADER_STAGE_FRAGMENT_BIT |
                                        GPU_SHADER_STAGE_COMPUTE_BIT;
  entry.arrayCount                    = 1u;
  layoutInfo.chain.sType              =
    GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize         = sizeof(layoutInfo);
  layoutInfo.entryCount               = 1u;
  layoutInfo.pEntries                 = &entry;
  if (GPUCreateBindGroupLayout(&fixture->device,
                               &layoutInfo,
                               &fixture->bindGroupLayout) != GPU_OK) {
    return false;
  }

  layouts[0]                          = fixture->bindGroupLayout;
  pipelineInfo.chain.sType            =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize       = sizeof(pipelineInfo);
  pipelineInfo.bindGroupLayoutCount   = 1u;
  pipelineInfo.ppBindGroupLayouts     = layouts;
  if (GPUCreatePipelineLayout(&fixture->device,
                              &pipelineInfo,
                              &fixture->pipelineLayout) != GPU_OK) {
    GPUDestroyBindGroupLayout(fixture->bindGroupLayout);
    fixture->bindGroupLayout = NULL;
    return false;
  }

  fixture->render._pipelineLayout        = fixture->pipelineLayout;
  fixture->render._requiredBindGroupMask = 1u;
  fixture->render._boundGroupLayouts[0]  = fixture->bindGroupLayout;
  fixture->compute._pipelineLayout        = fixture->pipelineLayout;
  fixture->compute._requiredBindGroupMask = 1u;
  fixture->compute._boundGroupLayouts[0]  = fixture->bindGroupLayout;
  return true;
}

static void
validation_destroy(ValidationFixture *fixture) {
  GPUDestroyPipelineLayout(fixture->pipelineLayout);
  GPUDestroyBindGroupLayout(fixture->bindGroupLayout);
}

int
main(int argc, char *argv[]) {
  enum { MODE_COUNT = (int)GPU_ARRAY_LEN(validationModes) };
  ValidationFixture fixture;
  double            drawSamples[MODE_COUNT][VALIDATION_REPEATS];
  double            dispatchSamples[MODE_COUNT][VALIDATION_REPEATS];
  double            directDrawSamples[VALIDATION_REPEATS];
  double            directDispatchSamples[VALIDATION_REPEATS];
  double            directDraw;
  double            directDispatch;
  uint32_t          iterations;

  iterations = 20000000u;
  if (argc > 2 ||
      (argc == 2 && !bench_parseU32(argv[1], 10000u, &iterations))) {
    fprintf(stderr, "usage: %s [iterations >= 10000]\n", argv[0]);
    return EXIT_FAILURE;
  }

  if (!validation_init(&fixture)) {
    fprintf(stderr, "failed to initialize validation benchmark\n");
    return EXIT_FAILURE;
  }
  validation_runDirectDraw(&fixture, VALIDATION_WARMUP_ITERATIONS);
  validation_runDirectDispatch(&fixture, VALIDATION_WARMUP_ITERATIONS);
  for (uint32_t mode = 0u; mode < (uint32_t)MODE_COUNT; mode++) {
    validation_runDraw(&fixture,
                       validationModes[mode],
                       VALIDATION_WARMUP_ITERATIONS);
    validation_runDispatch(&fixture,
                           validationModes[mode],
                           VALIDATION_WARMUP_ITERATIONS);
  }

  for (uint32_t repeat = 0u; repeat < VALIDATION_REPEATS; repeat++) {
    directDrawSamples[repeat] =
      validation_runDirectDraw(&fixture, iterations);
    directDispatchSamples[repeat] =
      validation_runDirectDispatch(&fixture, iterations);
    if ((repeat & 1u) == 0u) {
      for (uint32_t mode = 0u; mode < (uint32_t)MODE_COUNT; mode++) {
        drawSamples[mode][repeat] =
          validation_runDraw(&fixture, validationModes[mode], iterations);
        dispatchSamples[mode][repeat] =
          validation_runDispatch(&fixture, validationModes[mode], iterations);
      }
    } else {
      for (uint32_t mode = (uint32_t)MODE_COUNT; mode-- > 0u;) {
        dispatchSamples[mode][repeat] =
          validation_runDispatch(&fixture, validationModes[mode], iterations);
        drawSamples[mode][repeat] =
          validation_runDraw(&fixture, validationModes[mode], iterations);
      }
    }
  }

  directDraw = bench_percentile(directDrawSamples,
                                VALIDATION_REPEATS,
                                0.5);
  directDispatch = bench_percentile(directDispatchSamples,
                                    VALIDATION_REPEATS,
                                    0.5);
  printf("GPU validation overhead microbenchmark\n");
  printf("validation: %s, iterations: %u, repeats: %u\n",
         GPU_BUILD_WITH_VALIDATION ? "compiled" : "removed",
         iterations,
         VALIDATION_REPEATS);
  printf("direct draw callback    : %8.3f ns/call\n", directDraw);
  for (uint32_t mode = 0u; mode < (uint32_t)MODE_COUNT; mode++) {
    double median;

    median = bench_percentile(drawSamples[mode], VALIDATION_REPEATS, 0.5);
    printf("GPUDraw validation %-7s: %8.3f ns/call  delta %+7.3f ns\n",
           validationModeNames[mode],
           median,
           median - directDraw);
  }
  printf("direct dispatch callback: %8.3f ns/call\n", directDispatch);
  for (uint32_t mode = 0u; mode < (uint32_t)MODE_COUNT; mode++) {
    double median;

    median = bench_percentile(dispatchSamples[mode],
                              VALIDATION_REPEATS,
                              0.5);
    printf("GPUDispatch validation %-7s: %8.3f ns/call  delta %+7.3f ns\n",
           validationModeNames[mode],
           median,
           median - directDispatch);
  }
  printf("sink: %" PRIu64 "\n", validationSink);
  validation_destroy(&fixture);
  return EXIT_SUCCESS;
}
