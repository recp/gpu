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
  BINDLESS_DEFAULT_CAPACITY   = 64,
  BINDLESS_DEFAULT_ITERATIONS = 1000,
  BINDLESS_DEFAULT_REPEATS    = 7,
  BINDLESS_WARMUP_ITERATIONS  = 64,
  BINDLESS_MAX_CAPACITY       = 256,
  BINDLESS_MAX_ITERATIONS     = 10000,
  BINDLESS_MAX_REPEATS        = 31
};

typedef enum BindlessUpdatePath {
  BINDLESS_UPDATE_SINGLE = 0,
  BINDLESS_UPDATE_BATCH,
  BINDLESS_UPDATE_PATH_COUNT
} BindlessUpdatePath;

typedef enum BindlessInitResult {
  BINDLESS_INIT_FAILED = 0,
  BINDLESS_INIT_READY,
  BINDLESS_INIT_UNSUPPORTED
} BindlessInitResult;

typedef struct BindlessUpdateConfig {
  GPUBackend backend;
  uint32_t   capacity;
  uint32_t   iterations;
  uint32_t   repeats;
} BindlessUpdateConfig;

typedef struct BindlessUpdateBench {
  GPUInstance        *instance;
  GPUAdapter         *adapter;
  GPUDevice          *device;
  GPUBindGroupLayout *layout;
  GPUBindGroup       *group;
  GPUTexture         *textures[2];
  GPUTextureView     *views[2];
  GPUBindGroupEntry  *batchEntries[2];
  GPUBindGroupEntry   singleEntries[2];
} BindlessUpdateBench;

static bool
bindless_config(int argc, char *argv[], BindlessUpdateConfig *config) {
  if (!config || argc > 5) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s [default|metal|vulkan|dx12] "
              "[capacity 2..256] [iterations 1..10000] "
              "[repeats 1..31]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->backend    = GPU_BACKEND_DEFAULT;
  config->capacity   = BINDLESS_DEFAULT_CAPACITY;
  config->iterations = BINDLESS_DEFAULT_ITERATIONS;
  config->repeats    = BINDLESS_DEFAULT_REPEATS;
  if ((argc > 1 && !bench_parseBackend(argv[1], &config->backend)) ||
      (argc > 2 && !bench_parseU32(argv[2], 2u, &config->capacity)) ||
      (argc > 3 && !bench_parseU32(argv[3], 1u, &config->iterations)) ||
      (argc > 4 && !bench_parseU32(argv[4], 1u, &config->repeats)) ||
      config->capacity > BINDLESS_MAX_CAPACITY ||
      config->iterations > BINDLESS_MAX_ITERATIONS ||
      config->repeats > BINDLESS_MAX_REPEATS) {
    fprintf(stderr, "invalid bindless-update benchmark arguments\n");
    return false;
  }
  return true;
}

static GPUAdapter *
bindless_selectAdapter(GPUInstance *instance) {
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
bindless_createTexture(BindlessUpdateBench *bench, uint32_t index) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "bindless-update-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED;
  if (GPUCreateTexture(bench->device,
                       &textureInfo,
                       &bench->textures[index]) != GPU_OK ||
      !bench->textures[index]) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "bindless-update-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(bench->textures[index],
                              &viewInfo,
                              &bench->views[index]) == GPU_OK &&
         bench->views[index];
}

static BindlessInitResult
bindless_init(BindlessUpdateBench        *bench,
              const BindlessUpdateConfig *config,
              GPUAdapterProperties       *properties) {
  GPUFeature                   feature      = GPU_FEATURE_BINDLESS;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPUDeviceCreateInfo          deviceInfo   = {0};
  GPURuntimeConfig             runtimeInfo  = {0};
  GPUBindlessLayoutEXT         bindlessInfo = {0};
  GPUBindGroupLayoutEntry      layoutEntry  = {0};
  GPUBindGroupLayoutCreateInfo layoutInfo   = {0};
  GPUBindGroupCreateInfo       groupInfo    = {0};

  memset(bench, 0, sizeof(*bench));
  memset(properties, 0, sizeof(*properties));
  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = config->backend;
  if (GPUCreateInstance(&instanceInfo, &bench->instance) != GPU_OK ||
      !bench->instance) {
    return BINDLESS_INIT_FAILED;
  }

  bench->adapter = bindless_selectAdapter(bench->instance);
  if (!bench->adapter) {
    return BINDLESS_INIT_FAILED;
  }
  if (!GPUIsFeatureSupported(bench->adapter, GPU_FEATURE_BINDLESS)) {
    return BINDLESS_INIT_UNSUPPORTED;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.featureCount = 1u;
  deviceInfo.required.pFeatures    = &feature;
  if (GPUCreateDevice(bench->adapter, &deviceInfo, &bench->device) != GPU_OK ||
      !bench->device ||
      !GPUIsFeatureEnabled(bench->device, GPU_FEATURE_BINDLESS) ||
      !GPUGetProcAddr(bench->device, "GPUUpdateBindGroupEXT")) {
    return BINDLESS_INIT_FAILED;
  }

  runtimeInfo.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeInfo.chain.structSize = sizeof(runtimeInfo);
  runtimeInfo.validationMode   = GPU_VALIDATION_OFF;
  runtimeInfo.enableStats      = true;
  if (GPUConfigureRuntime(bench->device, &runtimeInfo) != GPU_OK ||
      GPUGetAdapterProperties(bench->adapter, properties) != GPU_OK ||
      !bindless_createTexture(bench, 0u) ||
      !bindless_createTexture(bench, 1u)) {
    return BINDLESS_INIT_FAILED;
  }

  bindlessInfo.chain.sType      = GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT;
  bindlessInfo.chain.structSize = sizeof(bindlessInfo);
  layoutEntry.binding           = 0u;
  layoutEntry.bindingType       = GPU_BINDING_SAMPLED_TEXTURE;
  layoutEntry.visibility        = GPU_SHADER_STAGE_COMPUTE_BIT;
  layoutEntry.arrayCount        = config->capacity;
  layoutInfo.chain.sType        = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize   = sizeof(layoutInfo);
  layoutInfo.chain.pNext        = &bindlessInfo;
  layoutInfo.label              = "bindless-update-layout";
  layoutInfo.entryCount         = 1u;
  layoutInfo.pEntries           = &layoutEntry;
  if (GPUCreateBindGroupLayout(bench->device,
                               &layoutInfo,
                               &bench->layout) != GPU_OK ||
      !bench->layout) {
    return BINDLESS_INIT_FAILED;
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "bindless-update-group";
  groupInfo.layout           = bench->layout;
  if (GPUCreateBindGroup(bench->device, &groupInfo, &bench->group) != GPU_OK ||
      !bench->group) {
    return BINDLESS_INIT_FAILED;
  }

  for (uint32_t variant = 0u; variant < 2u; variant++) {
    bench->batchEntries[variant] = calloc(config->capacity,
                                           sizeof(*bench->batchEntries[variant]));
    if (!bench->batchEntries[variant]) {
      return BINDLESS_INIT_FAILED;
    }

    bench->singleEntries[variant].binding     = 0u;
    bench->singleEntries[variant].arrayIndex  = 0u;
    bench->singleEntries[variant].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    bench->singleEntries[variant].textureView = bench->views[variant];
    for (uint32_t slot = 0u; slot < config->capacity; slot++) {
      GPUBindGroupEntry *entry;

      entry              = &bench->batchEntries[variant][slot];
      entry->binding     = 0u;
      entry->arrayIndex  = slot;
      entry->bindingType = GPU_BINDING_SAMPLED_TEXTURE;
      entry->textureView = bench->views[variant];
    }
  }
  return BINDLESS_INIT_READY;
}

static void
bindless_cleanup(BindlessUpdateBench *bench) {
  if (!bench) {
    return;
  }

  free(bench->batchEntries[1]);
  free(bench->batchEntries[0]);
  GPUDestroyBindGroup(bench->group);
  GPUDestroyBindGroupLayout(bench->layout);
  GPUDestroyTextureView(bench->views[1]);
  GPUDestroyTextureView(bench->views[0]);
  GPUDestroyTexture(bench->textures[1]);
  GPUDestroyTexture(bench->textures[0]);
  GPUDestroyDevice(bench->device);
  GPUDestroyInstance(bench->instance);
}

static bool
bindless_run(BindlessUpdateBench *bench,
             BindlessUpdatePath   path,
             uint32_t             capacity,
             uint32_t             iterations,
             double              *samples) {
  uint32_t entryCount;

  entryCount = path == BINDLESS_UPDATE_SINGLE ? 1u : capacity;
  for (uint32_t i = 0u; i < iterations; i++) {
    const GPUBindGroupEntry *entries;
    double                   begin;
    double                   elapsed;

    entries = path == BINDLESS_UPDATE_SINGLE
                ? &bench->singleEntries[i & 1u]
                : bench->batchEntries[i & 1u];
    begin   = bench_now();
    if (GPUUpdateBindGroupEXT(bench->group, entryCount, entries) != GPU_OK) {
      return false;
    }
    elapsed = bench_now() - begin;
    if (samples) {
      samples[i] = elapsed * 1e9;
    }
  }
  return true;
}

int
main(int argc, char *argv[]) {
  BindlessUpdateConfig config;
  BindlessUpdateBench  bench;
  GPUAdapterProperties properties;
  GPUFrameStats         stats;
  double               *samples[BINDLESS_UPDATE_PATH_COUNT];
  double                median[BINDLESS_UPDATE_PATH_COUNT];
  double                p95[BINDLESS_UPDATE_PATH_COUNT];
  double                p99[BINDLESS_UPDATE_PATH_COUNT];
  size_t                sampleCount;
  BindlessInitResult    initResult;
  bool                  ok;

  memset(&bench, 0, sizeof(bench));
  memset(&properties, 0, sizeof(properties));
  memset(&stats, 0, sizeof(stats));
  memset(samples, 0, sizeof(samples));
  if (!bindless_config(argc, argv, &config)) {
    return EXIT_FAILURE;
  }
  initResult = bindless_init(&bench, &config, &properties);
  if (initResult == BINDLESS_INIT_UNSUPPORTED) {
    fprintf(stderr,
            "bindless-update benchmark skipped: feature unsupported\n");
    bindless_cleanup(&bench);
    return EXIT_SUCCESS;
  }
  if (initResult != BINDLESS_INIT_READY) {
    fprintf(stderr, "failed to initialize bindless-update benchmark\n");
    bindless_cleanup(&bench);
    return EXIT_FAILURE;
  }

  sampleCount = (size_t)config.iterations * config.repeats;
  for (uint32_t path = 0u; path < BINDLESS_UPDATE_PATH_COUNT; path++) {
    samples[path] = calloc(sampleCount, sizeof(*samples[path]));
    if (!samples[path]) {
      fprintf(stderr, "failed to allocate bindless-update samples\n");
      for (uint32_t previous = 0u; previous < path; previous++) {
        free(samples[previous]);
      }
      bindless_cleanup(&bench);
      return EXIT_FAILURE;
    }
  }

  ok = bindless_run(&bench,
                    BINDLESS_UPDATE_SINGLE,
                    config.capacity,
                    BINDLESS_WARMUP_ITERATIONS,
                    NULL) &&
       bindless_run(&bench,
                    BINDLESS_UPDATE_BATCH,
                    config.capacity,
                    BINDLESS_WARMUP_ITERATIONS,
                    NULL);
  GPUResetStats(bench.device);
  for (uint32_t repeat = 0u; ok && repeat < config.repeats; repeat++) {
    if ((repeat & 1u) == 0u) {
      for (uint32_t path = 0u; path < BINDLESS_UPDATE_PATH_COUNT; path++) {
        ok = bindless_run(&bench,
                          (BindlessUpdatePath)path,
                          config.capacity,
                          config.iterations,
                          &samples[path][(size_t)repeat * config.iterations]);
        if (!ok) {
          break;
        }
      }
    } else {
      for (uint32_t path = BINDLESS_UPDATE_PATH_COUNT; path-- > 0u;) {
        ok = bindless_run(&bench,
                          (BindlessUpdatePath)path,
                          config.capacity,
                          config.iterations,
                          &samples[path][(size_t)repeat * config.iterations]);
        if (!ok) {
          break;
        }
      }
    }
  }

  stats = bench.device->currentFrameStats;
  ok    = ok && stats.hotPathAllocCount == 0u &&
          stats.hotPathAllocBytes == 0u &&
          stats.hotPathFreeCount == 0u &&
          stats.hotPathFreeBytes == 0u;
  for (uint32_t path = 0u; path < BINDLESS_UPDATE_PATH_COUNT; path++) {
    median[path] = bench_percentile(samples[path], sampleCount, 0.50);
    p95[path]    = bench_percentile(samples[path], sampleCount, 0.95);
    p99[path]    = bench_percentile(samples[path], sampleCount, 0.99);
  }

  if (ok) {
    printf("GPU bindless-update benchmark\n");
    printf("adapter: %s, backend: %s\n",
           properties.name ? properties.name : "unknown",
           bench_backendName(properties.backend));
    printf("capacity: %u descriptors, iterations: %u, repeats: %u\n",
           config.capacity,
           config.iterations,
           config.repeats);
    printf("single: median %8.2f ns/call, p95 %8.2f, p99 %8.2f\n",
           median[BINDLESS_UPDATE_SINGLE],
           p95[BINDLESS_UPDATE_SINGLE],
           p99[BINDLESS_UPDATE_SINGLE]);
    printf("batch : median %8.2f ns/call, p95 %8.2f, p99 %8.2f\n",
           median[BINDLESS_UPDATE_BATCH],
           p95[BINDLESS_UPDATE_BATCH],
           p99[BINDLESS_UPDATE_BATCH]);
    printf("batch median: %.2f ns/descriptor\n",
           median[BINDLESS_UPDATE_BATCH] / config.capacity);
    printf("warm hot-path allocations: %" PRIu64 ", frees: %" PRIu64 "\n",
           stats.hotPathAllocCount,
           stats.hotPathFreeCount);
  }

  for (uint32_t path = 0u; path < BINDLESS_UPDATE_PATH_COUNT; path++) {
    free(samples[path]);
  }
  bindless_cleanup(&bench);
  if (!ok) {
    fprintf(stderr,
            "bindless-update benchmark failed: %" PRIu64
            " warm allocations, %" PRIu64 " frees\n",
            stats.hotPathAllocCount,
            stats.hotPathFreeCount);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
