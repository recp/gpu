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
#include "bench.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  QUEUE_SUBMIT_DEFAULT_BATCH      = 8,
  QUEUE_SUBMIT_DEFAULT_ITERATIONS = 1000,
  QUEUE_SUBMIT_DEFAULT_REPEATS    = 7,
  QUEUE_SUBMIT_WARMUP_ITERATIONS  = 16,
  QUEUE_SUBMIT_MAX_BATCH          = 64,
  QUEUE_SUBMIT_MAX_ITERATIONS     = 10000,
  QUEUE_SUBMIT_MAX_REPEATS        = 31
};

typedef enum QueueSubmitPath {
  QUEUE_SUBMIT_SEPARATE = 0,
  QUEUE_SUBMIT_BATCH,
  QUEUE_SUBMIT_PATH_COUNT
} QueueSubmitPath;

typedef struct QueueSubmitConfig {
  GPUBackend backend;
  uint32_t   batchSize;
  uint32_t   iterations;
  uint32_t   repeats;
} QueueSubmitConfig;

typedef struct QueueSubmitBench {
  GPUInstance *instance;
  GPUAdapter  *adapter;
  GPUDevice   *device;
  GPUQueue    *queue;
  GPUFence    *fence;
} QueueSubmitBench;

static bool
queue_submitConfig(int argc, char *argv[], QueueSubmitConfig *config) {
  if (!config || argc > 5) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s [default|metal|vulkan|dx12] "
              "[batch 2..64] [iterations 1..10000] [repeats 1..31]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->backend    = GPU_BACKEND_DEFAULT;
  config->batchSize  = QUEUE_SUBMIT_DEFAULT_BATCH;
  config->iterations = QUEUE_SUBMIT_DEFAULT_ITERATIONS;
  config->repeats    = QUEUE_SUBMIT_DEFAULT_REPEATS;
  if ((argc > 1 && !bench_parseBackend(argv[1], &config->backend)) ||
      (argc > 2 && !bench_parseU32(argv[2], 2u, &config->batchSize)) ||
      (argc > 3 && !bench_parseU32(argv[3], 1u, &config->iterations)) ||
      (argc > 4 && !bench_parseU32(argv[4], 1u, &config->repeats)) ||
      config->batchSize > QUEUE_SUBMIT_MAX_BATCH ||
      config->iterations > QUEUE_SUBMIT_MAX_ITERATIONS ||
      config->repeats > QUEUE_SUBMIT_MAX_REPEATS) {
    fprintf(stderr, "invalid queue-submit benchmark arguments\n");
    return false;
  }
  return true;
}

static GPUAdapter *
queue_selectAdapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  GPUResult   result;
  uint32_t    count;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

static bool
queue_submitInit(QueueSubmitBench        *bench,
                 const QueueSubmitConfig *config,
                 GPUAdapterProperties    *properties) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPURuntimeConfig      runtimeInfo  = {0};

  memset(bench, 0, sizeof(*bench));
  memset(properties, 0, sizeof(*properties));
  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = config->backend;
  if (GPUCreateInstance(&instanceInfo, &bench->instance) != GPU_OK ||
      !bench->instance) {
    return false;
  }

  bench->adapter = queue_selectAdapter(bench->instance);
  if (!bench->adapter) {
    return false;
  }
  bench->device = GPUCreateDeviceWithDefaultQueues(bench->adapter);
  if (!bench->device) {
    return false;
  }
  bench->queue = GPUGetQueue(bench->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!bench->queue) {
    return false;
  }

  runtimeInfo.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeInfo.chain.structSize = sizeof(runtimeInfo);
  runtimeInfo.validationMode   = GPU_VALIDATION_OFF;
  runtimeInfo.enableStats      = true;
  return GPUConfigureRuntime(bench->device, &runtimeInfo) == GPU_OK &&
         GPUGetAdapterProperties(bench->adapter, properties) == GPU_OK &&
         GPUCreateFence(bench->device, NULL, &bench->fence) == GPU_OK &&
         bench->fence;
}

static void
queue_submitCleanup(QueueSubmitBench *bench) {
  if (!bench) {
    return;
  }

  GPUDestroyFence(bench->fence);
  GPUDestroyDevice(bench->device);
  GPUDestroyInstance(bench->instance);
}

static bool
queue_acquireBatch(QueueSubmitBench *bench,
                   GPUCommandBuffer **buffers,
                   uint32_t          count) {
  for (uint32_t i = 0u; i < count; i++) {
    buffers[i] = NULL;
    if (GPUAcquireCommandBuffer(bench->queue,
                                "queue-submit-bench",
                                &buffers[i]) != GPU_OK ||
        !buffers[i]) {
      return false;
    }
  }
  return true;
}

static bool
queue_submitSeparate(QueueSubmitBench *bench,
                     GPUCommandBuffer **buffers,
                     uint32_t          count,
                     double           *elapsed) {
  GPUQueueSubmitInfo info = {0};
  double             begin;

  info.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  info.chain.structSize   = sizeof(info);
  info.commandBufferCount = 1u;
  begin                   = bench_now();
  for (uint32_t i = 0u; i < count; i++) {
    info.ppCommandBuffers = &buffers[i];
    info.fence            = i + 1u == count ? bench->fence : NULL;
    if (GPUQueueSubmit(bench->queue, &info) != GPU_OK) {
      return false;
    }
  }
  *elapsed = bench_now() - begin;
  return GPUWaitFence(bench->fence, UINT64_MAX) == GPU_OK;
}

static bool
queue_submitBatch(QueueSubmitBench *bench,
                  GPUCommandBuffer **buffers,
                  uint32_t          count,
                  double           *elapsed) {
  GPUQueueSubmitInfo info = {0};
  double             begin;

  info.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  info.chain.structSize   = sizeof(info);
  info.commandBufferCount = count;
  info.ppCommandBuffers   = buffers;
  info.fence              = bench->fence;
  begin                   = bench_now();
  if (GPUQueueSubmit(bench->queue, &info) != GPU_OK) {
    return false;
  }
  *elapsed = bench_now() - begin;
  return GPUWaitFence(bench->fence, UINT64_MAX) == GPU_OK;
}

static bool
queue_submitRun(QueueSubmitBench *bench,
                QueueSubmitPath   path,
                uint32_t          batchSize,
                uint32_t          iterations,
                double           *samples) {
  GPUCommandBuffer *buffers[QUEUE_SUBMIT_MAX_BATCH];
  double            elapsed;

  for (uint32_t i = 0u; i < iterations; i++) {
    if (!queue_acquireBatch(bench, buffers, batchSize)) {
      return false;
    }

    elapsed = 0.0;
    if (path == QUEUE_SUBMIT_SEPARATE) {
      if (!queue_submitSeparate(bench, buffers, batchSize, &elapsed)) {
        return false;
      }
    } else if (!queue_submitBatch(bench, buffers, batchSize, &elapsed)) {
      return false;
    }
    if (samples) {
      samples[i] = elapsed * 1e9 / batchSize;
    }
  }
  return true;
}

int
main(int argc, char *argv[]) {
  QueueSubmitConfig    config;
  QueueSubmitBench     bench;
  GPUAdapterProperties properties;
  GPUFrameStats        stats;
  double              *samples[QUEUE_SUBMIT_PATH_COUNT];
  double               median[QUEUE_SUBMIT_PATH_COUNT];
  double               p95[QUEUE_SUBMIT_PATH_COUNT];
  double               p99[QUEUE_SUBMIT_PATH_COUNT];
  double               reduction;
  size_t               sampleCount;
  bool                 ok;

  memset(&bench, 0, sizeof(bench));
  memset(&properties, 0, sizeof(properties));
  memset(&stats, 0, sizeof(stats));
  memset(samples, 0, sizeof(samples));
  if (!queue_submitConfig(argc, argv, &config) ||
      !queue_submitInit(&bench, &config, &properties)) {
    fprintf(stderr, "failed to initialize queue-submit benchmark\n");
    queue_submitCleanup(&bench);
    return EXIT_FAILURE;
  }

  sampleCount = (size_t)config.iterations * config.repeats;
  for (uint32_t path = 0u; path < QUEUE_SUBMIT_PATH_COUNT; path++) {
    samples[path] = calloc(sampleCount, sizeof(*samples[path]));
    if (!samples[path]) {
      fprintf(stderr, "failed to allocate queue-submit samples\n");
      for (uint32_t previous = 0u; previous < path; previous++) {
        free(samples[previous]);
      }
      queue_submitCleanup(&bench);
      return EXIT_FAILURE;
    }
  }

  ok = queue_submitRun(&bench,
                       QUEUE_SUBMIT_SEPARATE,
                       config.batchSize,
                       QUEUE_SUBMIT_WARMUP_ITERATIONS,
                       NULL) &&
       queue_submitRun(&bench,
                       QUEUE_SUBMIT_BATCH,
                       config.batchSize,
                       QUEUE_SUBMIT_WARMUP_ITERATIONS,
                       NULL);
  GPUResetStats(bench.device);
  for (uint32_t repeat = 0u; ok && repeat < config.repeats; repeat++) {
    if ((repeat & 1u) == 0u) {
      for (uint32_t path = 0u; path < QUEUE_SUBMIT_PATH_COUNT; path++) {
        ok = queue_submitRun(&bench,
                             (QueueSubmitPath)path,
                             config.batchSize,
                             config.iterations,
                             &samples[path][(size_t)repeat *
                                            config.iterations]);
        if (!ok) {
          break;
        }
      }
    } else {
      for (uint32_t path = QUEUE_SUBMIT_PATH_COUNT; path-- > 0u;) {
        ok = queue_submitRun(&bench,
                             (QueueSubmitPath)path,
                             config.batchSize,
                             config.iterations,
                             &samples[path][(size_t)repeat *
                                            config.iterations]);
        if (!ok) {
          break;
        }
      }
    }
  }

  stats = bench.device->currentFrameStats;
  ok    = ok && stats.hotPathAllocCount == 0u &&
          stats.hotPathFreeCount == 0u;
  for (uint32_t path = 0u; path < QUEUE_SUBMIT_PATH_COUNT; path++) {
    median[path] = bench_percentile(samples[path], sampleCount, 0.50);
    p95[path]    = bench_percentile(samples[path], sampleCount, 0.95);
    p99[path]    = bench_percentile(samples[path], sampleCount, 0.99);
  }
  reduction = median[QUEUE_SUBMIT_SEPARATE] > 0.0
                ? (1.0 - median[QUEUE_SUBMIT_BATCH] /
                         median[QUEUE_SUBMIT_SEPARATE]) * 100.0
                : 0.0;

  if (ok) {
    printf("GPU queue-submit benchmark\n");
    printf("adapter: %s, backend: %s\n",
           properties.name ? properties.name : "unknown",
           bench_backendName(properties.backend));
    printf("batch: %u command buffers, iterations: %u, repeats: %u\n",
           config.batchSize,
           config.iterations,
           config.repeats);
    printf("separate: median %8.2f ns/cmdb, p95 %8.2f, p99 %8.2f\n",
           median[QUEUE_SUBMIT_SEPARATE],
           p95[QUEUE_SUBMIT_SEPARATE],
           p99[QUEUE_SUBMIT_SEPARATE]);
    printf("batch   : median %8.2f ns/cmdb, p95 %8.2f, p99 %8.2f\n",
           median[QUEUE_SUBMIT_BATCH],
           p95[QUEUE_SUBMIT_BATCH],
           p99[QUEUE_SUBMIT_BATCH]);
    printf("median submit CPU reduction: %.2f%%\n", reduction);
    printf("warm hot-path allocations: %" PRIu64 ", frees: %" PRIu64 "\n",
           stats.hotPathAllocCount,
           stats.hotPathFreeCount);
  }

  for (uint32_t path = 0u; path < QUEUE_SUBMIT_PATH_COUNT; path++) {
    free(samples[path]);
  }
  queue_submitCleanup(&bench);
  if (!ok) {
    fprintf(stderr,
            "queue-submit benchmark failed: %" PRIu64
            " warm allocations, %" PRIu64 " frees\n",
            stats.hotPathAllocCount,
            stats.hotPathFreeCount);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
