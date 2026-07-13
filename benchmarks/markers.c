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
  MARKER_REPEATS           = 7,
  MARKER_WARMUP_ITERATIONS = 1000000
};

#if GPU_BUILD_WITH_DEBUG_MARKERS
static const bool markerModes[] = {
  false,
  true
};

static const char *markerModeNames[] = {
  "disabled",
  "enabled"
};
#else
static const bool markerModes[] = {
  false
};

static const char *markerModeNames[] = {
  "removed"
};
#endif

typedef struct MarkerFixture {
  GPUCommandBuffer      cmdb;
  GPUCommandQueue       queue;
  GPUDevice             device;
  GPUApi                api;
} MarkerFixture;

static const char       *markerCommandLabel;
static const char       *markerComputeLabel;
static const char       *markerCopyLabel;
static volatile uint64_t markerSink;

static GPUCommandBuffer *
marker_newCommandBuffer(GPUCommandQueue                *queue,
                        const char                     *label,
                        void                           *sender,
                        GPUCommandBufferCompletionFn    oncomplete) {
  static GPUCommandBuffer cmdb;

  GPU__UNUSED(sender);
  GPU__UNUSED(oncomplete);
  memset(&cmdb, 0, sizeof(cmdb));
  markerCommandLabel = label;
  markerSink += (uint64_t)(queue != NULL) + (uint64_t)(label != NULL);
  return &cmdb;
}

static BENCH_NOINLINE GPUComputePassEncoder *
marker_beginCompute(GPUCommandBuffer *cmdb, const char *label) {
  static GPUComputePassEncoder pass;

  markerComputeLabel = label;
  markerSink += (uint64_t)(cmdb != NULL) + (uint64_t)(label != NULL);
  return &pass;
}

static GPUCopyPassEncoder *
marker_beginCopy(GPUCommandBuffer *cmdb, const char *label) {
  static GPUCopyPassEncoder pass;

  markerCopyLabel = label;
  markerSink += (uint64_t)(cmdb != NULL) + (uint64_t)(label != NULL);
  return &pass;
}

static double
marker_runDirect(MarkerFixture *fixture,
                 bool           enabled,
                 uint64_t       iterations) {
  const char *label;
  double      begin;

  label = enabled ? "marker-benchmark" : NULL;
  begin = bench_now();
  for (uint64_t i = 0u; i < iterations; i++) {
    GPUComputePassEncoder *pass;

    pass = marker_beginCompute(&fixture->cmdb, label);
    pass->_cmdb = &fixture->cmdb;
    fixture->cmdb._activeEncoder = true;
    fixture->cmdb._activeEncoder = false;
  }
  return (bench_now() - begin) * 1e9 / (double)iterations;
}

static double
marker_runBegin(MarkerFixture *fixture,
                bool           enabled,
                uint64_t       iterations) {
  double begin;

  fixture->device.runtimeConfig.enableDebugMarkers = enabled;
  begin = bench_now();
  for (uint64_t i = 0u; i < iterations; i++) {
    fixture->cmdb._activeEncoder = false;
    GPUBeginComputePass(&fixture->cmdb, "marker-benchmark");
  }
  return (bench_now() - begin) * 1e9 / (double)iterations;
}

static void
marker_init(MarkerFixture *fixture) {
  memset(fixture, 0, sizeof(*fixture));
  fixture->api.cmdque.newCommandBuffer         = marker_newCommandBuffer;
  fixture->api.compute.computeCommandEncoder   = marker_beginCompute;
  fixture->api.renderPass.beginCopyPass         = marker_beginCopy;
  fixture->device._api                         = &fixture->api;
  fixture->queue._device                       = &fixture->device;
  fixture->cmdb._queue                         = &fixture->queue;
}

static bool
marker_checkMode(MarkerFixture *fixture, bool enabled, bool expectLabel) {
  GPUCommandBuffer *cmdb;

  fixture->device.runtimeConfig.enableDebugMarkers = enabled;
  markerCommandLabel = NULL;
  markerComputeLabel = NULL;
  markerCopyLabel    = NULL;

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(&fixture->queue,
                              "marker-benchmark",
                              &cmdb) != GPU_OK || !cmdb) {
    return false;
  }
  fixture->cmdb._activeEncoder = false;
  GPUBeginComputePass(&fixture->cmdb, "marker-benchmark");
  fixture->cmdb._activeEncoder = false;
  GPUBeginCopyPass(&fixture->cmdb, "marker-benchmark");

  if ((markerCommandLabel != NULL) != expectLabel ||
      (markerComputeLabel != NULL) != expectLabel ||
      (markerCopyLabel != NULL) != expectLabel) {
    return false;
  }
  return true;
}

static bool
marker_checkBehavior(MarkerFixture *fixture) {
  if (!marker_checkMode(fixture, false, false)) {
    return false;
  }

#if GPU_BUILD_WITH_DEBUG_MARKERS
  return marker_checkMode(fixture, true, true);
#else
  return marker_checkMode(fixture, true, false);
#endif
}

int
main(int argc, char *argv[]) {
  enum { MODE_COUNT = (int)GPU_ARRAY_LEN(markerModes) };
  MarkerFixture fixture;
  double        directSamples[MODE_COUNT][MARKER_REPEATS];
  double        beginSamples[MODE_COUNT][MARKER_REPEATS];
  uint32_t      iterations;

  iterations = 20000000u;
  if (argc > 2 ||
      (argc == 2 && !bench_parseU32(argv[1], 10000u, &iterations))) {
    fprintf(stderr, "usage: %s [iterations >= 10000]\n", argv[0]);
    return EXIT_FAILURE;
  }

  marker_init(&fixture);
  if (!marker_checkBehavior(&fixture)) {
    fprintf(stderr, "debug marker label gate failed\n");
    return EXIT_FAILURE;
  }

  for (uint32_t mode = 0u; mode < (uint32_t)MODE_COUNT; mode++) {
    marker_runDirect(&fixture,
                     markerModes[mode],
                     MARKER_WARMUP_ITERATIONS);
    marker_runBegin(&fixture,
                    markerModes[mode],
                    MARKER_WARMUP_ITERATIONS);
  }

  for (uint32_t repeat = 0u; repeat < MARKER_REPEATS; repeat++) {
    if ((repeat & 1u) == 0u) {
      for (uint32_t mode = 0u; mode < (uint32_t)MODE_COUNT; mode++) {
        directSamples[mode][repeat] =
          marker_runDirect(&fixture, markerModes[mode], iterations);
        beginSamples[mode][repeat] =
          marker_runBegin(&fixture, markerModes[mode], iterations);
      }
    } else {
      for (uint32_t mode = (uint32_t)MODE_COUNT; mode-- > 0u;) {
        beginSamples[mode][repeat] =
          marker_runBegin(&fixture, markerModes[mode], iterations);
        directSamples[mode][repeat] =
          marker_runDirect(&fixture, markerModes[mode], iterations);
      }
    }
  }

  printf("GPU debug marker overhead microbenchmark\n");
  printf("markers: %s, iterations: %u, repeats: %u\n",
         GPU_BUILD_WITH_DEBUG_MARKERS ? "compiled" : "removed",
         iterations,
         MARKER_REPEATS);
  for (uint32_t mode = 0u; mode < (uint32_t)MODE_COUNT; mode++) {
    double direct;
    double begin;

    direct = bench_percentile(directSamples[mode], MARKER_REPEATS, 0.5);
    begin  = bench_percentile(beginSamples[mode], MARKER_REPEATS, 0.5);
    printf("markers %-8s direct: %8.3f ns  begin: %8.3f ns  delta %+7.3f ns\n",
           markerModeNames[mode],
           direct,
           begin,
           begin - direct);
  }
  printf("sink: %" PRIu64 "\n", markerSink);
  return EXIT_SUCCESS;
}
