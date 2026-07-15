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

#ifndef gpu_bench_render_h
#define gpu_bench_render_h

#include "bench.h"

#include <gpu/gpu.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct BenchRenderConfig {
  const char *artifactPath;
  GPUFeatureSet required;
  GPUBackend  backend;
  uint32_t    drawCount;
  uint32_t    warmupFrames;
  uint32_t    measuredFrames;
  uint32_t    repeats;
  bool        enableStats;
} BenchRenderConfig;

typedef struct BenchRender {
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUQueue             *queue;
  GPUShaderLibrary     *library;
  GPUPipelineLayout    *pipelineLayout;
  GPUBuffer            *vertexBuffer;
  GPUTexture           *target;
  GPUTextureView       *targetView;
  GPUFence             *fence;
  GPUAdapterProperties  adapterProperties;
  BenchProcessMemory    baselineMemory;
  bool                  requiredUnsupported;
} BenchRender;

typedef struct BenchPipelineInfo {
  const char   *label;
  const char   *vertexEntry;
  const char   *fragmentEntry;
  GPUFrontFace  frontFace;
  bool          vertexInput;
  bool          blendEnabled;
} BenchPipelineInfo;

typedef struct BenchSceneMetrics {
  double   *samples;
  double   *repeatMedians;
  double   *gpuSamples;
  double   *gpuRepeatMedians;
  uint64_t  requestedStateCalls;
  uint64_t  emittedStateCalls;
  uint64_t  requestedBindCalls;
  uint64_t  emittedBindCalls;
  uint64_t  drawCalls;
  uint64_t  maxAllocCount;
  uint64_t  maxAllocBytes;
  uint64_t  maxFreeCount;
  uint64_t  maxFreeBytes;
  size_t    sampleCount;
  size_t    gpuSampleCount;
  size_t    gpuRepeatCount;
} BenchSceneMetrics;

typedef bool (*BenchRenderEncodeFn)(GPURenderPassEncoder *pass,
                                    uint32_t              drawCount,
                                    void                 *userData);

bool
bench_renderConfig(int argc, char *argv[], BenchRenderConfig *config);

bool
bench_renderInit(BenchRender             *bench,
                 const BenchRenderConfig *config,
                 uint32_t                 width,
                 uint32_t                 height);

bool
bench_renderPipeline(BenchRender             *bench,
                     const BenchPipelineInfo *info,
                     GPURenderPipeline       **outPipeline);

bool
bench_renderRun(BenchRender             *bench,
                const BenchRenderConfig *config,
                BenchRenderEncodeFn      encode,
                void                    *userData,
                BenchSceneMetrics       *metrics);

void
bench_renderPrint(const char              *title,
                  const BenchRender       *bench,
                  const BenchRenderConfig *config,
                  BenchSceneMetrics       *metrics);

bool
bench_renderMetricsPass(const BenchSceneMetrics *metrics);

void
bench_renderFreeMetrics(BenchSceneMetrics *metrics);

void
bench_renderCleanup(BenchRender *bench);

#endif /* gpu_bench_render_h */
