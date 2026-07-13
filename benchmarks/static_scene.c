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

static bool
static_encode(GPURenderPassEncoder *pass,
              uint32_t              drawCount,
              void                 *userData) {
  GPURenderPipeline *pipeline;

  pipeline = userData;
  GPUBindRenderPipeline(pass, pipeline);
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    GPUDraw(pass, 3u, 1u, 0u, 0u);
  }
  return true;
}

int
main(int argc, char *argv[]) {
  BenchRenderConfig config;
  BenchRender       bench;
  BenchPipelineInfo pipelineInfo;
  BenchSceneMetrics metrics;
  GPURenderPipeline *pipeline;
  bool               ok;

  memset(&bench, 0, sizeof(bench));
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));
  memset(&metrics, 0, sizeof(metrics));
  pipeline = NULL;
  if (!bench_renderConfig(argc, argv, &config) ||
      !bench_renderInit(&bench, &config, 1u, 1u)) {
    bench_renderCleanup(&bench);
    return EXIT_FAILURE;
  }

  pipelineInfo.label       = "static-scene-pipeline";
  pipelineInfo.frontFace   = GPU_FRONT_FACE_CCW;
  pipelineInfo.vertexInput = true;
  if (!bench_renderPipeline(&bench, &pipelineInfo, &pipeline)) {
    fprintf(stderr, "failed to create static scene pipeline\n");
    bench_renderCleanup(&bench);
    return EXIT_FAILURE;
  }

  ok = bench_renderRun(&bench,
                       &config,
                       static_encode,
                       pipeline,
                       &metrics);
  if (ok) {
    bench_renderPrint("static scene", &bench, &config, &metrics);
    ok = bench_renderMetricsPass(&metrics);
  }

  bench_renderFreeMetrics(&metrics);
  GPUDestroyRenderPipeline(pipeline);
  bench_renderCleanup(&bench);
  if (!ok) {
    fprintf(stderr, "static scene benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
