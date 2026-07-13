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

#include "render.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  BINDING_GROUP_COUNT = 2
};

typedef struct BindingChurn {
  GPURenderPipeline  *pipeline;
  GPUBindGroupLayout *layout;
  GPUBuffer          *buffers[BINDING_GROUP_COUNT];
  GPUBindGroup       *groups[BINDING_GROUP_COUNT];
  GPUBindGroup       *aliases[BINDING_GROUP_COUNT];
  GPUCacheStats       setupCacheStats;
} BindingChurn;

static bool
binding_createLayout(BenchRender *bench, BindingChurn *churn) {
  const GPUBindGroupLayoutEntry *entries;
  uint32_t                       entryCount;
  uint32_t                       layoutCount;

  GPUDestroyPipelineLayout(bench->pipelineLayout);
  bench->pipelineLayout = NULL;

  layoutCount = 0u;
  if (GPUCreateBindGroupLayoutsFromReflection(bench->device,
                                               bench->library,
                                               &layoutCount,
                                               NULL) != GPU_OK ||
      layoutCount != 1u) {
    return false;
  }
  if (GPUCreateBindGroupLayoutsFromReflection(bench->device,
                                               bench->library,
                                               &layoutCount,
                                               &churn->layout) != GPU_OK ||
      layoutCount != 1u || !churn->layout) {
    return false;
  }

  entries = GPUGetBindGroupLayoutEntries(churn->layout, &entryCount);
  if (!entries || entryCount != 1u || entries[0].binding != 0u ||
      entries[0].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      entries[0].visibility != GPU_SHADER_STAGE_FRAGMENT_BIT ||
      entries[0].arrayCount != 1u || entries[0].hasDynamicOffset) {
    return false;
  }

  return GPUCreatePipelineLayoutFromReflection(bench->device,
                                                bench->library,
                                                1u,
                                                &churn->layout,
                                                &bench->pipelineLayout) == GPU_OK &&
         bench->pipelineLayout != NULL;
}

static bool
binding_createGroups(BenchRender *bench, BindingChurn *churn) {
  static const float colors[BINDING_GROUP_COUNT][4] = {
    {1.0f, 0.2f, 0.1f, 1.0f},
    {0.1f, 0.4f, 1.0f, 1.0f}
  };
  GPUBindGroupEntry      entry;
  GPUBindGroupCreateInfo groupInfo;
  GPUBufferCreateInfo    bufferInfo;

  memset(&entry, 0, sizeof(entry));
  memset(&groupInfo, 0, sizeof(groupInfo));
  memset(&bufferInfo, 0, sizeof(bufferInfo));
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(colors[0]);
  bufferInfo.usage = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t i = 0u; i < BINDING_GROUP_COUNT; i++) {
    if (GPUCreateBuffer(bench->device,
                        &bufferInfo,
                        &churn->buffers[i]) != GPU_OK ||
        !churn->buffers[i] ||
        GPUQueueWriteBuffer(bench->queue,
                            churn->buffers[i],
                            0u,
                            colors[i],
                            sizeof(colors[i])) != GPU_OK) {
      return false;
    }
  }

  entry.binding       = 0u;
  entry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entry.buffer.size   = sizeof(colors[0]);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout            = churn->layout;
  groupInfo.entryCount        = 1u;
  groupInfo.pEntries          = &entry;

  GPUResetStats(bench->device);
  for (uint32_t i = 0u; i < BINDING_GROUP_COUNT; i++) {
    entry.buffer.buffer = churn->buffers[i];
    if (GPUCreateBindGroup(bench->device,
                           &groupInfo,
                           &churn->groups[i]) != GPU_OK ||
        !churn->groups[i] ||
        GPUCreateBindGroup(bench->device,
                           &groupInfo,
                           &churn->aliases[i]) != GPU_OK ||
        churn->aliases[i] != churn->groups[i]) {
      return false;
    }
  }
  if (churn->groups[0] == churn->groups[1]) {
    return false;
  }

  return GPUGetCacheStats(bench->device, &churn->setupCacheStats) == GPU_OK &&
         churn->setupCacheStats.bindGroupHits == BINDING_GROUP_COUNT &&
         churn->setupCacheStats.bindGroupMisses == BINDING_GROUP_COUNT;
}

static bool
binding_encode(GPURenderPassEncoder *pass,
               uint32_t              drawCount,
               void                 *userData) {
  BindingChurn *churn;

  churn = userData;
  GPUBindRenderPipeline(pass, churn->pipeline);
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    uint32_t groupIndex;

    groupIndex = (draw >> 1u) & 1u;
    GPUBindRenderGroup(pass, 0u, churn->groups[groupIndex], 0u, NULL);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
  }
  return true;
}

static bool
binding_metricsMatch(const BenchRenderConfig *config,
                     const BenchSceneMetrics *metrics) {
  uint64_t frames;
  uint64_t runs;

  frames = metrics->sampleCount;
  runs   = ((uint64_t)config->drawCount + 1u) / 2u;
  return metrics->requestedBindCalls ==
           ((uint64_t)config->drawCount + 2u) * frames &&
         metrics->emittedBindCalls == (runs + 2u) * frames &&
         metrics->requestedStateCalls == 0u &&
         metrics->emittedStateCalls == 0u &&
         metrics->drawCalls == (uint64_t)config->drawCount * frames;
}

static void
binding_cleanup(BenchRender *bench, BindingChurn *churn) {
  GPUDestroyRenderPipeline(churn->pipeline);
  for (uint32_t i = 0u; i < BINDING_GROUP_COUNT; i++) {
    GPUDestroyBindGroup(churn->aliases[i]);
    GPUDestroyBindGroup(churn->groups[i]);
    GPUDestroyBuffer(churn->buffers[i]);
  }
  GPUDestroyPipelineLayout(bench->pipelineLayout);
  bench->pipelineLayout = NULL;
  GPUDestroyBindGroupLayout(churn->layout);
  bench_renderCleanup(bench);
}

int
main(int argc, char *argv[]) {
  BenchRenderConfig config;
  BenchRender       bench;
  BenchPipelineInfo pipelineInfo;
  BenchSceneMetrics metrics;
  BindingChurn      churn;
  bool              ok;

  memset(&bench, 0, sizeof(bench));
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  memset(&metrics, 0, sizeof(metrics));
  memset(&churn, 0, sizeof(churn));
  if (!bench_renderConfig(argc, argv, &config) ||
      !bench_renderInit(&bench, &config, 1u, 1u) ||
      !binding_createLayout(&bench, &churn) ||
      !binding_createGroups(&bench, &churn)) {
    binding_cleanup(&bench, &churn);
    return EXIT_FAILURE;
  }

  pipelineInfo.label         = "binding-churn-pipeline";
  pipelineInfo.vertexEntry   = "tri_vs";
  pipelineInfo.fragmentEntry = "tri_fs";
  pipelineInfo.frontFace     = GPU_FRONT_FACE_CCW;
  if (!bench_renderPipeline(&bench, &pipelineInfo, &churn.pipeline)) {
    fprintf(stderr, "failed to create binding churn pipeline\n");
    binding_cleanup(&bench, &churn);
    return EXIT_FAILURE;
  }

  ok = bench_renderRun(&bench,
                       &config,
                       binding_encode,
                       &churn,
                       &metrics);
  if (ok) {
    bench_renderPrint("binding churn", &bench, &config, &metrics);
    printf("bind group cache setup: %" PRIu64 " hits, %" PRIu64
           " misses, %" PRIu64 " collisions\n",
           churn.setupCacheStats.bindGroupHits,
           churn.setupCacheStats.bindGroupMisses,
           churn.setupCacheStats.bindGroupCollisions);
    ok = bench_renderMetricsPass(&metrics) &&
         binding_metricsMatch(&config, &metrics);
  }

  bench_renderFreeMetrics(&metrics);
  binding_cleanup(&bench, &churn);
  if (!ok) {
    fprintf(stderr, "binding churn benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
