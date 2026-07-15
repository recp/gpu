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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  STATE_TARGET_SIZE = 64,
  STATE_COUNT       = 2
};

typedef struct StateChurn {
  GPURenderPipeline        *pipelines[STATE_COUNT];
  GPUDynamicStateApplyInfo  states[STATE_COUNT];
} StateChurn;

static void
state_init(StateChurn *churn) {
  GPUDynamicStateMask mask;

  memset(churn, 0, sizeof(*churn));
  mask = GPU_DYNAMIC_STATE_VIEWPORT_BIT |
         GPU_DYNAMIC_STATE_SCISSOR_BIT |
         GPU_DYNAMIC_STATE_BLEND_CONSTANT_BIT |
         GPU_DYNAMIC_STATE_STENCIL_REFERENCE_BIT;
  for (uint32_t i = 0u; i < STATE_COUNT; i++) {
    churn->states[i].chain.sType = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
    churn->states[i].chain.structSize = sizeof(churn->states[i]);
    churn->states[i].mask              = mask;
    churn->states[i].viewport.minDepth = 0.0f;
    churn->states[i].viewport.maxDepth = 1.0f;
  }

  churn->states[0].viewport.width  = STATE_TARGET_SIZE;
  churn->states[0].viewport.height = STATE_TARGET_SIZE;
  churn->states[0].scissor.width   = STATE_TARGET_SIZE;
  churn->states[0].scissor.height  = STATE_TARGET_SIZE;

  churn->states[1].viewport.x       = 1.0f;
  churn->states[1].viewport.y       = 1.0f;
  churn->states[1].viewport.width   = STATE_TARGET_SIZE - 2u;
  churn->states[1].viewport.height  = STATE_TARGET_SIZE - 2u;
  churn->states[1].scissor.x        = 1;
  churn->states[1].scissor.y        = 1;
  churn->states[1].scissor.width    = STATE_TARGET_SIZE - 2u;
  churn->states[1].scissor.height   = STATE_TARGET_SIZE - 2u;
  churn->states[1].blendConstant[0] = 1.0f;
  churn->states[1].blendConstant[1] = 0.5f;
  churn->states[1].blendConstant[2] = 0.25f;
  churn->states[1].blendConstant[3] = 1.0f;
  churn->states[1].stencilReference = 1u;
}

static bool
state_encode(GPURenderPassEncoder *pass,
             uint32_t              drawCount,
             void                 *userData) {
  StateChurn *churn;

  churn = userData;
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    uint32_t stateIndex;

    stateIndex = (draw >> 1u) & 1u;
    GPUBindRenderPipeline(pass, churn->pipelines[stateIndex]);
    GPUApplyDynamicState(pass, &churn->states[stateIndex]);
    GPUDraw(pass, 3u, 1u, 0u, 0u);
  }
  return true;
}

static bool
state_metricsMatch(const BenchRenderConfig *config,
                   const BenchSceneMetrics *metrics) {
  uint64_t frames;
  uint64_t runs;

  frames = metrics->sampleCount;
  runs   = ((uint64_t)config->drawCount + 1u) / 2u;
  return metrics->requestedBindCalls ==
           ((uint64_t)config->drawCount + 1u) * frames &&
         metrics->emittedBindCalls == (runs + 1u) * frames &&
         metrics->requestedStateCalls ==
           (uint64_t)config->drawCount * 4u * frames &&
         metrics->emittedStateCalls == runs * 4u * frames &&
         metrics->drawCalls == (uint64_t)config->drawCount * frames;
}

int
main(int argc, char *argv[]) {
  BenchRenderConfig config;
  BenchRender       bench;
  BenchPipelineInfo pipelineInfo;
  BenchSceneMetrics metrics;
  StateChurn        churn;
  bool              ok;

  memset(&bench, 0, sizeof(bench));
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  memset(&metrics, 0, sizeof(metrics));
  state_init(&churn);
  if (!bench_renderConfig(argc, argv, &config) ||
      config.drawCount > UINT32_MAX / 4u ||
      !bench_renderInit(&bench,
                        &config,
                        STATE_TARGET_SIZE,
                        STATE_TARGET_SIZE)) {
    bench_renderCleanup(&bench);
    return EXIT_FAILURE;
  }

  pipelineInfo.label       = "state-churn-pipeline-a";
  pipelineInfo.frontFace   = GPU_FRONT_FACE_CCW;
  pipelineInfo.vertexInput = true;
  if (!bench_renderPipeline(&bench,
                            &pipelineInfo,
                            &churn.pipelines[0])) {
    fprintf(stderr, "failed to create state churn pipeline A\n");
    bench_renderCleanup(&bench);
    return EXIT_FAILURE;
  }

  pipelineInfo.label        = "state-churn-pipeline-b";
  pipelineInfo.frontFace    = GPU_FRONT_FACE_CW;
  pipelineInfo.blendEnabled = true;
  if (!bench_renderPipeline(&bench,
                            &pipelineInfo,
                            &churn.pipelines[1])) {
    fprintf(stderr, "failed to create state churn pipeline B\n");
    GPUDestroyRenderPipeline(churn.pipelines[0]);
    bench_renderCleanup(&bench);
    return EXIT_FAILURE;
  }

  ok = bench_renderRun(&bench,
                       &config,
                       state_encode,
                       &churn,
                       &metrics);
  if (ok) {
    bench_renderPrint("state churn", &bench, &config, &metrics);
    ok = bench_renderMetricsPass(&metrics) &&
         (!config.enableStats || state_metricsMatch(&config, &metrics));
  }

  bench_renderFreeMetrics(&metrics);
  GPUDestroyRenderPipeline(churn.pipelines[1]);
  GPUDestroyRenderPipeline(churn.pipelines[0]);
  bench_renderCleanup(&bench);
  if (!ok) {
    fprintf(stderr, "state churn benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
