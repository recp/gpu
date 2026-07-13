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

#include "bench.h"
#include "render.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <sched.h>
#endif

enum {
  PIPELINE_DEFAULT_COUNT    = 32,
  PIPELINE_DEFAULT_REPEATS  = 3,
  PIPELINE_MAX_PERMUTATIONS = 180
};

typedef struct PipelineStressConfig {
  const char *artifactPath;
  GPUBackend  backend;
  uint32_t    pipelineCount;
  uint32_t    repeats;
} PipelineStressConfig;

typedef struct PipelineStress {
  BenchRender                  bench;
  GPURenderPipelineCreateInfo *infos;
  GPUColorTargetState         *targets;
  GPURenderPipeline          **pipelines;
  GPUPipelineCompileHandle    *handles;
  bool                        *done;
  GPUVertexAttribute           attribute;
  GPUVertexBufferLayout        vertexLayout;
  uint32_t                     pipelineCount;
} PipelineStress;

typedef struct PipelineMetrics {
  double *samples;
  double *cold;
  double *warm;
  double *prewarm;
  double *prewarmLookup;
  double *asyncEnqueue;
  double *asyncReady;
  double *asyncLookup;
} PipelineMetrics;

static bool
pipeline_config(int argc, char *argv[], PipelineStressConfig *config) {
  if (!config || argc < 2 || argc > 5) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s <shader.us> [default|metal|vulkan|dx12] "
              "[pipelines] [repeats]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->artifactPath  = argv[1];
  config->backend       = GPU_BACKEND_DEFAULT;
  config->pipelineCount = PIPELINE_DEFAULT_COUNT;
  config->repeats       = PIPELINE_DEFAULT_REPEATS;
  if ((argc > 2 && !bench_parseBackend(argv[2], &config->backend)) ||
      (argc > 3 && !bench_parseU32(argv[3], 1u, &config->pipelineCount)) ||
      (argc > 4 && !bench_parseU32(argv[4], 1u, &config->repeats)) ||
      config->pipelineCount > PIPELINE_MAX_PERMUTATIONS) {
    fprintf(stderr,
            "invalid pipeline benchmark arguments; maximum pipelines: %u\n",
            PIPELINE_MAX_PERMUTATIONS);
    return false;
  }
  return true;
}

static void
pipeline_initInfo(PipelineStress *stress, uint32_t index) {
  GPURenderPipelineCreateInfo *info;
  GPUColorTargetState         *target;
  uint32_t                     variant;

  info    = &stress->infos[index];
  target  = &stress->targets[index];
  variant = index;

  target->format          = GPU_FORMAT_BGRA8_UNORM;
  target->blend.writeMask = (variant % 15u) + 1u;
  variant                /= 15u;

  info->cullMode = (GPUCullMode)(variant % 3u);
  variant       /= 3u;
  info->frontFace = (GPUFrontFace)(variant % 2u);
  variant        /= 2u;

  target->blend.enabled = (variant % 2u) != 0u;
  if (target->blend.enabled) {
    target->blend.color.srcFactor = GPU_BLEND_FACTOR_SRC_ALPHA;
    target->blend.color.dstFactor = GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    target->blend.color.op        = GPU_BLEND_OP_ADD;
    target->blend.alpha.srcFactor = GPU_BLEND_FACTOR_ONE;
    target->blend.alpha.dstFactor = GPU_BLEND_FACTOR_ZERO;
    target->blend.alpha.op        = GPU_BLEND_OP_ADD;
  }
  info->chain.sType      = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info->chain.structSize = sizeof(*info);
  info->label            = "pipeline-stress";
  info->layout           = stress->bench.pipelineLayout;
  info->library          = stress->bench.library;
  info->vertexEntry      = "api_vs";
  info->fragmentEntry    = "api_fs";
  info->vertex.bufferLayoutCount = 1u;
  info->vertex.pBufferLayouts    = &stress->vertexLayout;
  info->colorTargetCount         = 1u;
  info->pColorTargets            = target;
  info->depthStencilFormat       = GPU_FORMAT_UNDEFINED;
  info->primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info->multisample.sampleCount = 1u;
  info->multisample.sampleMask  = UINT32_MAX;
}

static bool
pipeline_init(PipelineStress             *stress,
              const PipelineStressConfig *config) {
  BenchRenderConfig renderConfig;
  size_t            count;

  memset(stress, 0, sizeof(*stress));
  memset(&renderConfig, 0, sizeof(renderConfig));
  renderConfig.artifactPath = config->artifactPath;
  renderConfig.backend      = config->backend;
  if (!bench_renderInit(&stress->bench, &renderConfig, 1u, 1u)) {
    return false;
  }

  count                 = config->pipelineCount;
  stress->pipelineCount = config->pipelineCount;
  stress->infos         = calloc(count, sizeof(*stress->infos));
  stress->targets       = calloc(count, sizeof(*stress->targets));
  stress->pipelines     = calloc(count, sizeof(*stress->pipelines));
  stress->handles       = calloc(count, sizeof(*stress->handles));
  stress->done          = calloc(count, sizeof(*stress->done));
  if (!stress->infos || !stress->targets || !stress->pipelines ||
      !stress->handles || !stress->done) {
    return false;
  }

  stress->attribute.shaderLocation    = 0u;
  stress->attribute.format            = GPU_VERTEX_FORMAT_FLOAT2;
  stress->vertexLayout.strideBytes    = 2u * sizeof(float);
  stress->vertexLayout.stepMode       = GPU_VERTEX_STEP_MODE_VERTEX;
  stress->vertexLayout.attributeCount = 1u;
  stress->vertexLayout.pAttributes    = &stress->attribute;
  for (uint32_t i = 0u; i < config->pipelineCount; i++) {
    pipeline_initInfo(stress, i);
  }
  return true;
}

static void
pipeline_cleanup(PipelineStress *stress) {
  if (!stress) {
    return;
  }
  if (stress->pipelines) {
    for (uint32_t i = 0u; i < stress->pipelineCount; i++) {
      GPUDestroyRenderPipeline(stress->pipelines[i]);
    }
  }
  free(stress->done);
  free(stress->handles);
  free(stress->pipelines);
  free(stress->targets);
  free(stress->infos);
  bench_renderCleanup(&stress->bench);
}

static bool
pipeline_createCache(PipelineStress   *stress,
                     uint32_t          pipelineCount,
                     GPUPipelineCache **outCache) {
  GPUPipelineCacheCreateInfo info;

  memset(&info, 0, sizeof(info));
  info.chain.sType      = GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "pipeline-stress-cache";
  info.maxEntries       = pipelineCount;
  return GPUCreatePipelineCache(stress->bench.device,
                                &info,
                                outCache) == GPU_OK &&
         *outCache != NULL;
}

static bool
pipeline_createAll(PipelineStress   *stress,
                   GPUPipelineCache *cache,
                   uint32_t          pipelineCount,
                   double           *outNs) {
  double begin;
  double end;

  memset(stress->pipelines,
         0,
         (size_t)pipelineCount * sizeof(*stress->pipelines));
  begin = bench_now();
  for (uint32_t i = 0u; i < pipelineCount; i++) {
    GPURenderPipelineCreateInfo info;

    info       = stress->infos[i];
    info.cache = cache;
    if (GPUCreateRenderPipeline(stress->bench.device,
                                &info,
                                &stress->pipelines[i]) != GPU_OK ||
        !stress->pipelines[i]) {
      return false;
    }
  }
  end = bench_now();

  for (uint32_t i = 0u; i < pipelineCount; i++) {
    GPUDestroyRenderPipeline(stress->pipelines[i]);
    stress->pipelines[i] = NULL;
  }
  *outNs = (end - begin) * 1e9;
  return true;
}

static bool
pipeline_statsMatch(PipelineStress *stress,
                    uint64_t        hits,
                    uint64_t        misses,
                    uint64_t        compiles) {
  GPUCacheStats stats;

  if (GPUGetCacheStats(stress->bench.device, &stats) != GPU_OK) {
    return false;
  }
  if (stats.pipelineHits == hits &&
      stats.pipelineMisses == misses &&
      stats.pipelineCompiles == compiles) {
    return true;
  }
  fprintf(stderr,
          "cache stats mismatch: got %" PRIu64 "/%" PRIu64 "/%" PRIu64
          ", expected %" PRIu64 "/%" PRIu64 "/%" PRIu64 "\n",
          stats.pipelineHits,
          stats.pipelineMisses,
          stats.pipelineCompiles,
          hits,
          misses,
          compiles);
  return false;
}

static bool
pipeline_prewarm(PipelineStress   *stress,
                 GPUPipelineCache *cache,
                 uint32_t          pipelineCount,
                 double           *outNs) {
  double    begin;
  GPUResult result;

  begin  = bench_now();
  result = GPUPrewarmRenderPipelines(stress->bench.device,
                                     cache,
                                     pipelineCount,
                                     stress->infos);
  *outNs = (bench_now() - begin) * 1e9;
  return result == GPU_OK;
}

static void
pipeline_yield(void) {
#if defined(_WIN32) || defined(WIN32)
  SwitchToThread();
#else
  sched_yield();
#endif
}

static bool
pipeline_compileAsync(PipelineStress   *stress,
                      GPUPipelineCache *cache,
                      uint32_t          pipelineCount,
                      double           *outEnqueueNs,
                      double           *outReadyNs) {
  uint32_t completed;
  double   begin;
  double   deadline;

  memset(stress->done, 0, (size_t)pipelineCount * sizeof(*stress->done));
  begin = bench_now();
  for (uint32_t i = 0u; i < pipelineCount; i++) {
    if (GPUCompileRenderPipelineAsync(stress->bench.device,
                                      cache,
                                      &stress->infos[i],
                                      &stress->handles[i]) != GPU_OK ||
        stress->handles[i].id == 0u) {
      return false;
    }
  }
  *outEnqueueNs = (bench_now() - begin) * 1e9;

  completed = 0u;
  deadline  = begin + 60.0;
  while (completed < pipelineCount && bench_now() < deadline) {
    bool progressed;

    progressed = false;
    for (uint32_t i = 0u; i < pipelineCount; i++) {
      GPUPipelineCompileStatus status;
      GPURenderPipeline       *pipeline;

      if (stress->done[i]) {
        continue;
      }
      pipeline = NULL;
      if (GPUPollRenderPipelineCompile(stress->bench.device,
                                       stress->handles[i],
                                       &status,
                                       &pipeline) != GPU_OK) {
        return false;
      }
      if (status == GPU_PIPELINE_COMPILE_PENDING) {
        continue;
      }
      if (status != GPU_PIPELINE_COMPILE_READY || !pipeline) {
        return false;
      }
      GPUDestroyRenderPipeline(pipeline);
      stress->done[i] = true;
      completed++;
      progressed = true;
    }
    if (!progressed && completed < pipelineCount) {
      pipeline_yield();
    }
  }
  *outReadyNs = (bench_now() - begin) * 1e9;
  return completed == pipelineCount;
}

static bool
pipeline_metricsInit(PipelineMetrics *metrics, uint32_t repeats) {
  size_t count;

  memset(metrics, 0, sizeof(*metrics));
  count            = (size_t)repeats * 7u;
  metrics->samples = calloc(count, sizeof(*metrics->samples));
  if (!metrics->samples) {
    return false;
  }
  metrics->cold          = metrics->samples;
  metrics->warm          = metrics->cold + repeats;
  metrics->prewarm       = metrics->warm + repeats;
  metrics->prewarmLookup = metrics->prewarm + repeats;
  metrics->asyncEnqueue  = metrics->prewarmLookup + repeats;
  metrics->asyncReady    = metrics->asyncEnqueue + repeats;
  metrics->asyncLookup   = metrics->asyncReady + repeats;
  return true;
}

static bool
pipeline_run(PipelineStress             *stress,
             const PipelineStressConfig *config,
             PipelineMetrics            *metrics) {
  uint64_t count;

  count = config->pipelineCount;
  for (uint32_t repeat = 0u; repeat < config->repeats; repeat++) {
    GPUPipelineCache *cache;

    cache = NULL;
    if (!pipeline_createCache(stress, config->pipelineCount, &cache)) {
      return false;
    }
    GPUResetStats(stress->bench.device);
    if (!pipeline_createAll(stress,
                            cache,
                            config->pipelineCount,
                            &metrics->cold[repeat]) ||
        !pipeline_statsMatch(stress, 0u, count, count)) {
      GPUDestroyPipelineCache(cache);
      return false;
    }
    GPUResetStats(stress->bench.device);
    if (!pipeline_createAll(stress,
                            cache,
                            config->pipelineCount,
                            &metrics->warm[repeat]) ||
        !pipeline_statsMatch(stress, count, 0u, 0u)) {
      GPUDestroyPipelineCache(cache);
      return false;
    }
    GPUDestroyPipelineCache(cache);

    cache = NULL;
    if (!pipeline_createCache(stress, config->pipelineCount, &cache)) {
      return false;
    }
    GPUResetStats(stress->bench.device);
    if (!pipeline_prewarm(stress,
                          cache,
                          config->pipelineCount,
                          &metrics->prewarm[repeat]) ||
        !pipeline_statsMatch(stress, 0u, count, count)) {
      GPUDestroyPipelineCache(cache);
      return false;
    }
    GPUResetStats(stress->bench.device);
    if (!pipeline_createAll(stress,
                            cache,
                            config->pipelineCount,
                            &metrics->prewarmLookup[repeat]) ||
        !pipeline_statsMatch(stress, count, 0u, 0u)) {
      GPUDestroyPipelineCache(cache);
      return false;
    }
    GPUDestroyPipelineCache(cache);

    cache = NULL;
    if (!pipeline_createCache(stress, config->pipelineCount, &cache)) {
      return false;
    }
    GPUResetStats(stress->bench.device);
    if (!pipeline_compileAsync(stress,
                               cache,
                               config->pipelineCount,
                               &metrics->asyncEnqueue[repeat],
                               &metrics->asyncReady[repeat]) ||
        !pipeline_statsMatch(stress, 0u, count, count)) {
      GPUDestroyPipelineCache(cache);
      return false;
    }
    GPUResetStats(stress->bench.device);
    if (!pipeline_createAll(stress,
                            cache,
                            config->pipelineCount,
                            &metrics->asyncLookup[repeat]) ||
        !pipeline_statsMatch(stress, count, 0u, 0u)) {
      GPUDestroyPipelineCache(cache);
      return false;
    }
    GPUDestroyPipelineCache(cache);
  }
  return true;
}

static void
pipeline_print(const PipelineStressConfig *config,
               PipelineStress             *stress,
               PipelineMetrics            *metrics) {
  double cold;
  double firstMiss;
  double warm;
  double prewarm;
  double prewarmLookup;
  double asyncEnqueue;
  double asyncReady;
  double asyncLookup;
  double count;

  firstMiss     = metrics->cold[0];
  cold          = bench_percentile(metrics->cold, config->repeats, 0.5);
  warm          = bench_percentile(metrics->warm, config->repeats, 0.5);
  prewarm       = bench_percentile(metrics->prewarm, config->repeats, 0.5);
  prewarmLookup = bench_percentile(metrics->prewarmLookup,
                                   config->repeats,
                                   0.5);
  asyncEnqueue  = bench_percentile(metrics->asyncEnqueue,
                                   config->repeats,
                                   0.5);
  asyncReady    = bench_percentile(metrics->asyncReady,
                                   config->repeats,
                                   0.5);
  asyncLookup   = bench_percentile(metrics->asyncLookup,
                                   config->repeats,
                                   0.5);
  count         = config->pipelineCount;

  printf("GPU pipeline stress benchmark\n");
  printf("adapter: %s, backend: %s, validation: %s\n",
         stress->bench.adapterProperties.name
           ? stress->bench.adapterProperties.name
           : "unknown",
         bench_backendName(stress->bench.adapterProperties.backend),
         GPU_BUILD_WITH_VALIDATION ? "compiled" : "removed");
  printf("pipelines: %u, repeats: %u\n",
         config->pipelineCount,
         config->repeats);
  printf("first cache-miss pass: %.3f ms total, %.3f us/pipeline\n",
         firstMiss / 1e6,
         firstMiss / count / 1e3);
  printf("cache-miss median    : %.3f ms total, %.3f us/pipeline\n",
         cold / 1e6,
         cold / count / 1e3);
  printf("cache-hit lookup     : %.3f us total, %.3f us/pipeline, "
         "%.2fx faster\n",
         warm / 1e3,
         warm / count / 1e3,
         warm > 0.0 ? cold / warm : 0.0);
  printf("prewarm    : %.3f ms total\n", prewarm / 1e6);
  printf("prewarmed lookup: %.3f us total, %.3f us/pipeline\n",
         prewarmLookup / 1e3,
         prewarmLookup / count / 1e3);
  printf("async enqueue: %.3f us total, %.3f us/pipeline\n",
         asyncEnqueue / 1e3,
         asyncEnqueue / count / 1e3);
  printf("async ready  : %.3f ms enqueue-to-ready\n", asyncReady / 1e6);
  printf("async lookup : %.3f us total, %.3f us/pipeline\n",
         asyncLookup / 1e3,
         asyncLookup / count / 1e3);
  printf("cache checks : cold/prewarm/async misses=%" PRIu64
         ", warm hits=%" PRIu64 "\n",
         (uint64_t)config->pipelineCount,
         (uint64_t)config->pipelineCount);
}

int
main(int argc, char *argv[]) {
  PipelineStressConfig config;
  PipelineStress       stress;
  PipelineMetrics      metrics;
  bool                 ok;

  memset(&stress, 0, sizeof(stress));
  memset(&metrics, 0, sizeof(metrics));
  if (!pipeline_config(argc, argv, &config) ||
      !pipeline_init(&stress, &config) ||
      !pipeline_metricsInit(&metrics, config.repeats)) {
    free(metrics.samples);
    pipeline_cleanup(&stress);
    return EXIT_FAILURE;
  }

  ok = pipeline_run(&stress, &config, &metrics);
  if (ok) {
    pipeline_print(&config, &stress, &metrics);
  }
  free(metrics.samples);
  pipeline_cleanup(&stress);
  if (!ok) {
    fprintf(stderr, "pipeline stress benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
