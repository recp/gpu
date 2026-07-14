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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  BINDLESS_RENDER_ARRAY_SIZE  = 16,
  BINDLESS_RENDER_GROUP_COUNT = 2,
  BINDLESS_RENDER_TARGET_SIZE = 16
};

typedef enum BindlessRenderPath {
  BINDLESS_RENDER_STABLE = 0,
  BINDLESS_RENDER_CHURN,
  BINDLESS_RENDER_PATH_COUNT
} BindlessRenderPath;

typedef struct BindlessRender {
  GPURenderPipeline  *pipeline;
  GPUShaderLayout    *shaderLayout;
  GPUBindGroupLayout *bindlessLayout;
  GPUBindGroup       *groups[BINDLESS_RENDER_GROUP_COUNT];
  GPUTexture         *textures[BINDLESS_RENDER_GROUP_COUNT];
  GPUTextureView     *views[BINDLESS_RENDER_GROUP_COUNT];
  GPUSampler         *samplers[BINDLESS_RENDER_GROUP_COUNT];
  GPUBuffer          *selectionBuffer;
  BenchRender         render;
  BindlessRenderPath  path;
} BindlessRender;

static bool
bindless_hasEntry(const GPUBindGroupLayoutEntry *entries,
                  uint32_t                       count,
                  GPUBindingType                 type,
                  GPUShaderStageFlags            visibility,
                  uint32_t                       binding,
                  uint32_t                       arrayCount) {
  for (uint32_t i = 0u; entries && i < count; i++) {
    if (entries[i].binding == binding &&
        entries[i].bindingType == type &&
        entries[i].visibility == visibility &&
        entries[i].arrayCount == arrayCount) {
      return true;
    }
  }
  return false;
}

static bool
bindless_createTexture(BindlessRender *state,
                       uint32_t        index,
                       const uint8_t   color[4]) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTextureWriteRegion    writeRegion = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "bindless-render-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 1u;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->render.device,
                       &textureInfo,
                       &state->textures[index]) != GPU_OK ||
      !state->textures[index]) {
    return false;
  }

  writeRegion.width        = 1u;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = 4u;
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(state->render.queue,
                           state->textures[index],
                           &writeRegion,
                           color,
                           4u) != GPU_OK) {
    return false;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "bindless-render-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(state->textures[index],
                              &viewInfo,
                              &state->views[index]) == GPU_OK &&
         state->views[index];
}

static bool
bindless_createResources(BindlessRender *state) {
  static const uint8_t colors[BINDLESS_RENDER_GROUP_COUNT][4] = {
    {255u, 0u, 0u, 255u},
    {0u, 255u, 0u, 255u}
  };
  uint32_t             selection[64] = {0u};
  GPUSamplerCreateInfo samplerInfo   = {0};
  GPUBufferCreateInfo  bufferInfo    = {0};

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "bindless-render-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter   = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  for (uint32_t i = 0u; i < BINDLESS_RENDER_GROUP_COUNT; i++) {
    if (!bindless_createTexture(state, i, colors[i]) ||
        GPUCreateSampler(state->render.device,
                         &samplerInfo,
                         false,
                         &state->samplers[i]) != GPU_OK ||
        !state->samplers[i]) {
      return false;
    }
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "bindless-render-selection";
  bufferInfo.sizeBytes        = sizeof(selection);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  return GPUCreateBuffer(state->render.device,
                         &bufferInfo,
                         &state->selectionBuffer) == GPU_OK &&
         state->selectionBuffer &&
         GPUQueueWriteBuffer(state->render.queue,
                             state->selectionBuffer,
                             0u,
                             selection,
                             sizeof(selection)) == GPU_OK;
}

static bool
bindless_createLayout(BindlessRender *state) {
  const GPUBindGroupLayoutEntry *entries;
  GPUBindlessLayoutEXT           bindlessInfo       = {0};
  GPUBindGroupLayoutCreateInfo   layoutInfo         = {0};
  GPUPipelineLayoutCreateInfo    pipelineLayoutInfo = {0};
  GPUShaderStageFlags            sampledVisibility;
  uint32_t                       entryCount;

  if (GPUCreateShaderLayout(state->render.device,
                            state->render.library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    return false;
  }

  entries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &entryCount
  );
  sampledVisibility = GPU_SHADER_STAGE_FRAGMENT_BIT |
                      GPU_SHADER_STAGE_COMPUTE_BIT;
  if (!entries || entryCount != 4u ||
      !bindless_hasEntry(entries,
                         entryCount,
                         GPU_BINDING_SAMPLED_TEXTURE,
                         sampledVisibility,
                         0u,
                         BINDLESS_RENDER_ARRAY_SIZE) ||
      !bindless_hasEntry(entries,
                         entryCount,
                         GPU_BINDING_SAMPLER,
                         sampledVisibility,
                         1u,
                         BINDLESS_RENDER_ARRAY_SIZE) ||
      !bindless_hasEntry(entries,
                         entryCount,
                         GPU_BINDING_UNIFORM_BUFFER,
                         sampledVisibility,
                         2u,
                         1u) ||
      !bindless_hasEntry(entries,
                         entryCount,
                         GPU_BINDING_STORAGE_BUFFER,
                         GPU_SHADER_STAGE_COMPUTE_BIT,
                         3u,
                         1u)) {
    return false;
  }

  bindlessInfo.chain.sType      = GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT;
  bindlessInfo.chain.structSize = sizeof(bindlessInfo);
  bindlessInfo.sourceLayout     = state->shaderLayout->bindGroupLayouts[0];
  layoutInfo.chain.sType        = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize   = sizeof(layoutInfo);
  layoutInfo.chain.pNext        = &bindlessInfo;
  layoutInfo.label              = "bindless-render-layout";
  if (GPUCreateBindGroupLayout(state->render.device,
                               &layoutInfo,
                               &state->bindlessLayout) != GPU_OK ||
      !state->bindlessLayout) {
    return false;
  }

  GPUDestroyPipelineLayout(state->render.pipelineLayout);
  state->render.pipelineLayout = NULL;
  pipelineLayoutInfo.chain.sType =
    GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.chain.structSize     = sizeof(pipelineLayoutInfo);
  pipelineLayoutInfo.label                = "bindless-render-layout";
  pipelineLayoutInfo.bindGroupLayoutCount = 1u;
  pipelineLayoutInfo.ppBindGroupLayouts   = &state->bindlessLayout;
  return GPUCreatePipelineLayout(state->render.device,
                                 &pipelineLayoutInfo,
                                 &state->render.pipelineLayout) == GPU_OK &&
         state->render.pipelineLayout;
}

static bool
bindless_createGroups(BindlessRender *state) {
  GPUBindGroupCreateInfo groupInfo = {0};

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "bindless-render-group";
  groupInfo.layout           = state->bindlessLayout;
  for (uint32_t i = 0u; i < BINDLESS_RENDER_GROUP_COUNT; i++) {
    GPUBindGroupEntry entries[3] = {{0}};

    if (GPUCreateBindGroup(state->render.device,
                           &groupInfo,
                           &state->groups[i]) != GPU_OK ||
        !state->groups[i]) {
      return false;
    }

    entries[0].binding     = 0u;
    entries[0].arrayIndex  = 0u;
    entries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    entries[0].textureView = state->views[i];
    entries[1].binding     = 1u;
    entries[1].arrayIndex  = 0u;
    entries[1].bindingType = GPU_BINDING_SAMPLER;
    entries[1].sampler     = state->samplers[i];
    entries[2].binding       = 2u;
    entries[2].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
    entries[2].buffer.buffer = state->selectionBuffer;
    entries[2].buffer.size   = 256u;
    if (GPUUpdateBindGroupEXT(state->groups[i],
                              (uint32_t)GPU_ARRAY_LEN(entries),
                              entries) != GPU_OK) {
      return false;
    }
  }
  return true;
}

static bool
bindless_createPipeline(BindlessRender *state) {
  BenchPipelineInfo info = {0};

  info.label         = "bindless-render-pipeline";
  info.vertexEntry   = "bindless_render_vs";
  info.fragmentEntry = "bindless_render_fs";
  info.frontFace     = GPU_FRONT_FACE_CCW;
  return bench_renderPipeline(&state->render, &info, &state->pipeline);
}

static bool
bindless_encode(GPURenderPassEncoder *pass,
                uint32_t              drawCount,
                void                 *userData) {
  BindlessRender *state;

  state = userData;
  GPUBindRenderPipeline(pass, state->pipeline);
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    uint32_t groupIndex;

    groupIndex = state->path == BINDLESS_RENDER_STABLE ? 0u : (draw & 1u);
    GPUBindRenderGroup(pass, 0u, state->groups[groupIndex], 0u, NULL);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
  }
  return true;
}

static bool
bindless_metricsMatch(const BenchRenderConfig *config,
                      const BenchSceneMetrics *metrics,
                      BindlessRenderPath       path) {
  uint64_t frames;
  uint64_t emittedPerFrame;

  frames = metrics->sampleCount;
  emittedPerFrame = path == BINDLESS_RENDER_STABLE
                      ? 3u
                      : (uint64_t)config->drawCount + 2u;
  return metrics->requestedBindCalls ==
           ((uint64_t)config->drawCount + 2u) * frames &&
         metrics->emittedBindCalls == emittedPerFrame * frames &&
         metrics->requestedStateCalls == 0u &&
         metrics->emittedStateCalls == 0u &&
         metrics->drawCalls == (uint64_t)config->drawCount * frames;
}

static void
bindless_cleanup(BindlessRender *state) {
  GPUDestroyRenderPipeline(state->pipeline);
  GPUDestroyBindGroup(state->groups[1]);
  GPUDestroyBindGroup(state->groups[0]);
  GPUDestroyBuffer(state->selectionBuffer);
  GPUDestroySampler(state->samplers[1]);
  GPUDestroySampler(state->samplers[0]);
  GPUDestroyTextureView(state->views[1]);
  GPUDestroyTextureView(state->views[0]);
  GPUDestroyTexture(state->textures[1]);
  GPUDestroyTexture(state->textures[0]);
  GPUDestroyPipelineLayout(state->render.pipelineLayout);
  state->render.pipelineLayout = NULL;
  GPUDestroyBindGroupLayout(state->bindlessLayout);
  GPUDestroyShaderLayout(state->shaderLayout);
  bench_renderCleanup(&state->render);
}

int
main(int argc, char *argv[]) {
  GPUFeature         feature = GPU_FEATURE_BINDLESS;
  BenchRenderConfig  config;
  BenchSceneMetrics  metrics[BINDLESS_RENDER_PATH_COUNT];
  BindlessRender     state;
  double             median[BINDLESS_RENDER_PATH_COUNT];
  bool               ok;

  memset(&state, 0, sizeof(state));
  memset(metrics, 0, sizeof(metrics));
  if (!bench_renderConfig(argc, argv, &config)) {
    return EXIT_FAILURE;
  }
  config.required.featureCount = 1u;
  config.required.pFeatures    = &feature;
  if (!bench_renderInit(&state.render,
                        &config,
                        BINDLESS_RENDER_TARGET_SIZE,
                        BINDLESS_RENDER_TARGET_SIZE)) {
    if (state.render.requiredUnsupported) {
      printf("bindless-render benchmark skipped: feature unsupported\n");
      bindless_cleanup(&state);
      return EXIT_SUCCESS;
    }
    fprintf(stderr, "failed to initialize bindless-render benchmark\n");
    bindless_cleanup(&state);
    return EXIT_FAILURE;
  }
  if (!GPUIsFeatureEnabled(state.render.device, GPU_FEATURE_BINDLESS) ||
      !bindless_createLayout(&state) ||
      !bindless_createResources(&state) ||
      !bindless_createGroups(&state) ||
      !bindless_createPipeline(&state)) {
    fprintf(stderr, "failed to initialize bindless-render benchmark\n");
    bindless_cleanup(&state);
    return EXIT_FAILURE;
  }

  ok = true;
  for (uint32_t path = 0u; ok && path < BINDLESS_RENDER_PATH_COUNT; path++) {
    state.path = (BindlessRenderPath)path;
    ok = bench_renderRun(&state.render,
                         &config,
                         bindless_encode,
                         &state,
                         &metrics[path]) &&
         bench_renderMetricsPass(&metrics[path]) &&
         bindless_metricsMatch(&config, &metrics[path], state.path);
    if (ok) {
      bench_renderPrint(path == BINDLESS_RENDER_STABLE
                          ? "bindless render stable"
                          : "bindless render churn",
                        &state.render,
                        &config,
                        &metrics[path]);
      median[path] = bench_percentile(metrics[path].repeatMedians,
                                      config.repeats,
                                      0.5);
    }
  }
  if (ok) {
    printf("median bind churn cost: %.3f ns/draw\n",
           (median[BINDLESS_RENDER_CHURN] -
            median[BINDLESS_RENDER_STABLE]) / config.drawCount);
  }

  for (uint32_t path = 0u; path < BINDLESS_RENDER_PATH_COUNT; path++) {
    bench_renderFreeMetrics(&metrics[path]);
  }
  bindless_cleanup(&state);
  if (!ok) {
    fprintf(stderr, "bindless-render benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
