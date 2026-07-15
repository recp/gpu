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

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "bench.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  NATIVE_DEFAULT_DRAWS   = 1000,
  NATIVE_DEFAULT_WARMUP  = 300,
  NATIVE_DEFAULT_FRAMES  = 3000,
  NATIVE_DEFAULT_REPEATS = 5
};

typedef struct NativeMetalConfig {
  uint32_t drawCount;
  uint32_t warmupFrames;
  uint32_t measuredFrames;
  uint32_t repeats;
} NativeMetalConfig;

typedef struct NativeMetalBench {
  id<MTLDevice>              device;
  id<MTLCommandQueue>        queue;
  id<MTLLibrary>             library;
  id<MTLFunction>            vertexFunction;
  id<MTLFunction>            fragmentFunction;
  id<MTLRenderPipelineState> pipeline;
  id<MTLBuffer>              vertexBuffer;
  id<MTLTexture>             target;
  MTLRenderPassDescriptor   *renderPass;
  BenchProcessMemory         baselineMemory;
} NativeMetalBench;

typedef struct NativeMetalMetrics {
  double *encodeSamples;
  double *encodeRepeatMedians;
  double *gpuSamples;
  double *gpuRepeatMedians;
  size_t  sampleCount;
} NativeMetalMetrics;

static const float nativeVertices[] = {
  -1.0f, -1.0f,
   3.0f, -1.0f,
  -1.0f,  3.0f
};

static NSString *nativeShaderSource =
  @"#include <metal_stdlib>\n"
   "using namespace metal;\n"
   "struct VertexIn { float2 position [[attribute(0)]]; };\n"
   "vertex float4 api_vs(VertexIn input [[stage_in]]) {\n"
   "  return float4(input.position, 0.0, 1.0);\n"
   "}\n"
   "fragment float4 api_fs() {\n"
   "  return float4(1.0, 0.0, 0.0, 1.0);\n"
   "}\n";

static bool
native_parseConfig(int argc, char *argv[], NativeMetalConfig *config) {
  if (!config || argc > 5) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s [draws] [warmup] [frames] [repeats]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->drawCount      = NATIVE_DEFAULT_DRAWS;
  config->warmupFrames   = NATIVE_DEFAULT_WARMUP;
  config->measuredFrames = NATIVE_DEFAULT_FRAMES;
  config->repeats        = NATIVE_DEFAULT_REPEATS;
  return (argc <= 1 || bench_parseU32(argv[1], 1u, &config->drawCount)) &&
         (argc <= 2 || bench_parseU32(argv[2], 0u, &config->warmupFrames)) &&
         (argc <= 3 || bench_parseU32(argv[3], 1u,
                                     &config->measuredFrames)) &&
         (argc <= 4 || bench_parseU32(argv[4], 1u, &config->repeats));
}

static bool
native_init(NativeMetalBench *bench) {
  MTLRenderPipelineDescriptor *pipelineDesc;
  MTLTextureDescriptor        *textureDesc;
  MTLVertexDescriptor         *vertexDesc;
  NSError                     *error;

  memset(bench, 0, sizeof(*bench));
  (void)bench_processMemory(&bench->baselineMemory);
  error         = nil;
  bench->device = MTLCreateSystemDefaultDevice();
  bench->queue  = [bench->device newCommandQueue];
  bench->library = [bench->device newLibraryWithSource:nativeShaderSource
                                               options:nil
                                                 error:&error];
  bench->vertexFunction = [bench->library newFunctionWithName:@"api_vs"];
  bench->fragmentFunction = [bench->library newFunctionWithName:@"api_fs"];
  if (!bench->device || !bench->queue || !bench->library ||
      !bench->vertexFunction || !bench->fragmentFunction) {
    if (error) {
      fprintf(stderr, "native Metal shader error: %s\n",
              error.localizedDescription.UTF8String);
    }
    return false;
  }

  vertexDesc = [MTLVertexDescriptor new];
  vertexDesc.attributes[0].format      = MTLVertexFormatFloat2;
  vertexDesc.attributes[0].offset      = 0u;
  vertexDesc.attributes[0].bufferIndex = 0u;
  vertexDesc.layouts[0].stride         = 2u * sizeof(float);
  vertexDesc.layouts[0].stepFunction   = MTLVertexStepFunctionPerVertex;

  pipelineDesc = [MTLRenderPipelineDescriptor new];
  pipelineDesc.vertexFunction                  = bench->vertexFunction;
  pipelineDesc.fragmentFunction                = bench->fragmentFunction;
  pipelineDesc.vertexDescriptor                = vertexDesc;
  pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  bench->pipeline = [bench->device
    newRenderPipelineStateWithDescriptor:pipelineDesc
                                   error:&error];
  [pipelineDesc release];
  [vertexDesc release];
  if (!bench->pipeline) {
    fprintf(stderr, "native Metal pipeline error: %s\n",
            error ? error.localizedDescription.UTF8String : "unknown");
    return false;
  }

  bench->vertexBuffer = [bench->device
    newBufferWithBytes:nativeVertices
                length:sizeof(nativeVertices)
               options:MTLResourceStorageModeShared];
  textureDesc = [MTLTextureDescriptor
    texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                 width:1u
                                height:1u
                             mipmapped:NO];
  textureDesc.usage = MTLTextureUsageRenderTarget;
  bench->target     = [bench->device newTextureWithDescriptor:textureDesc];
  bench->renderPass = [MTLRenderPassDescriptor new];
  bench->renderPass.colorAttachments[0].texture     = bench->target;
  bench->renderPass.colorAttachments[0].loadAction  = MTLLoadActionClear;
  bench->renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;
  bench->renderPass.colorAttachments[0].clearColor  =
    MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
  return bench->vertexBuffer && bench->target && bench->renderPass;
}

static void
native_destroy(NativeMetalBench *bench) {
  [bench->renderPass release];
  [bench->target release];
  [bench->vertexBuffer release];
  [bench->pipeline release];
  [bench->fragmentFunction release];
  [bench->vertexFunction release];
  [bench->library release];
  [bench->queue release];
  [bench->device release];
}

static bool
native_frame(NativeMetalBench *bench,
             uint32_t          drawCount,
             double           *outEncodeNs,
             double           *outGpuNs) {
  id<MTLCommandBuffer>        commandBuffer;
  id<MTLRenderCommandEncoder> encoder;
  CFTimeInterval              gpuStart;
  CFTimeInterval              gpuEnd;
  double                      begin;
  double                      end;

  @autoreleasepool {
    begin         = bench_now();
    commandBuffer = [bench->queue commandBuffer];
    encoder = [commandBuffer renderCommandEncoderWithDescriptor:
      bench->renderPass];
    if (!commandBuffer || !encoder) {
      return false;
    }
    [encoder setRenderPipelineState:bench->pipeline];
    [encoder setVertexBuffer:bench->vertexBuffer offset:0u atIndex:0u];
    for (uint32_t draw = 0u; draw < drawCount; draw++) {
      [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                  vertexStart:0u
                  vertexCount:3u
                instanceCount:1u
                 baseInstance:0u];
    }
    [encoder endEncoding];
    [commandBuffer commit];
    end = bench_now();
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status != MTLCommandBufferStatusCompleted) {
      return false;
    }
    gpuStart = commandBuffer.GPUStartTime;
    gpuEnd   = commandBuffer.GPUEndTime;
  }

  if (!(gpuEnd > gpuStart)) {
    return false;
  }
  *outEncodeNs = (end - begin) * 1e9;
  *outGpuNs    = (gpuEnd - gpuStart) * 1e9;
  return true;
}

static bool
native_run(NativeMetalBench        *bench,
           const NativeMetalConfig *config,
           NativeMetalMetrics      *metrics) {
  size_t sampleCount;

  if ((size_t)config->measuredFrames > SIZE_MAX / config->repeats) {
    return false;
  }
  sampleCount = (size_t)config->measuredFrames * config->repeats;
  memset(metrics, 0, sizeof(*metrics));
  metrics->encodeSamples = calloc(sampleCount,
                                  sizeof(*metrics->encodeSamples));
  metrics->encodeRepeatMedians = calloc(
    config->repeats,
    sizeof(*metrics->encodeRepeatMedians)
  );
  metrics->gpuSamples = calloc(sampleCount, sizeof(*metrics->gpuSamples));
  metrics->gpuRepeatMedians = calloc(
    config->repeats,
    sizeof(*metrics->gpuRepeatMedians)
  );
  metrics->sampleCount = sampleCount;
  if (!metrics->encodeSamples || !metrics->encodeRepeatMedians ||
      !metrics->gpuSamples || !metrics->gpuRepeatMedians) {
    return false;
  }

  for (uint32_t repeat = 0u; repeat < config->repeats; repeat++) {
    size_t base;

    for (uint32_t frame = 0u; frame < config->warmupFrames; frame++) {
      double ignoredEncode;
      double ignoredGpu;

      if (!native_frame(bench,
                        config->drawCount,
                        &ignoredEncode,
                        &ignoredGpu)) {
        return false;
      }
    }
    base = (size_t)repeat * config->measuredFrames;
    for (uint32_t frame = 0u; frame < config->measuredFrames; frame++) {
      if (!native_frame(bench,
                        config->drawCount,
                        &metrics->encodeSamples[base + frame],
                        &metrics->gpuSamples[base + frame])) {
        return false;
      }
    }
    metrics->encodeRepeatMedians[repeat] = bench_percentile(
      &metrics->encodeSamples[base],
      config->measuredFrames,
      0.5
    );
    metrics->gpuRepeatMedians[repeat] = bench_percentile(
      &metrics->gpuSamples[base],
      config->measuredFrames,
      0.5
    );
  }
  return true;
}

static void
native_print(const NativeMetalBench   *bench,
             const NativeMetalConfig  *config,
             NativeMetalMetrics       *metrics) {
  BenchProcessMemory memory;
  double             encodeMedian;
  double             encodeP95;
  double             encodeP99;
  double             gpuMedian;
  double             gpuP95;
  double             gpuP99;

  encodeMedian = bench_percentile(metrics->encodeRepeatMedians,
                                  config->repeats,
                                  0.5);
  encodeP95 = bench_percentile(metrics->encodeSamples,
                               metrics->sampleCount,
                               0.95);
  encodeP99 = bench_percentile(metrics->encodeSamples,
                               metrics->sampleCount,
                               0.99);
  gpuMedian = bench_percentile(metrics->gpuRepeatMedians,
                               config->repeats,
                               0.5);
  gpuP95 = bench_percentile(metrics->gpuSamples,
                            metrics->sampleCount,
                            0.95);
  gpuP99 = bench_percentile(metrics->gpuSamples,
                            metrics->sampleCount,
                            0.99);
  printf("Native Metal static scene benchmark\n");
  printf("adapter: %s\n", bench->device.name.UTF8String);
  printf("draws/frame: %u, warmup: %u, frames: %u, repeats: %u\n",
         config->drawCount,
         config->warmupFrames,
         config->measuredFrames,
         config->repeats);
  printf("encode+submit: median %.3f us, p95 %.3f us, p99 %.3f us\n",
         encodeMedian / 1e3,
         encodeP95 / 1e3,
         encodeP99 / 1e3);
  printf("median per draw: %.3f ns\n", encodeMedian / config->drawCount);
  printf("GPU frame: median %.3f us, p95 %.3f us, p99 %.3f us\n",
         gpuMedian / 1e3,
         gpuP95 / 1e3,
         gpuP99 / 1e3);
  if (bench_processMemory(&memory) && bench->baselineMemory.residentBytes > 0u) {
    double residentDelta;
    double peakDelta;

    residentDelta = memory.residentBytes > bench->baselineMemory.residentBytes
                      ? (double)(memory.residentBytes -
                                 bench->baselineMemory.residentBytes)
                      : 0.0;
    peakDelta = memory.peakResidentBytes > bench->baselineMemory.peakResidentBytes
                  ? (double)(memory.peakResidentBytes -
                             bench->baselineMemory.peakResidentBytes)
                  : 0.0;
    printf("process memory: resident %.2f MiB (+%.2f), peak %.2f MiB "
           "(+%.2f)\n",
           (double)memory.residentBytes / (1024.0 * 1024.0),
           residentDelta / (1024.0 * 1024.0),
           (double)memory.peakResidentBytes / (1024.0 * 1024.0),
           peakDelta / (1024.0 * 1024.0));
  }
}

static void
native_freeMetrics(NativeMetalMetrics *metrics) {
  free(metrics->gpuRepeatMedians);
  free(metrics->gpuSamples);
  free(metrics->encodeRepeatMedians);
  free(metrics->encodeSamples);
}

int
main(int argc, char *argv[]) {
  NativeMetalConfig  config;
  NativeMetalBench   bench;
  NativeMetalMetrics metrics;
  int                result;

  memset(&bench, 0, sizeof(bench));
  memset(&metrics, 0, sizeof(metrics));
  result = EXIT_FAILURE;
  @autoreleasepool {
    if (!native_parseConfig(argc, argv, &config) ||
        !native_init(&bench) ||
        !native_run(&bench, &config, &metrics)) {
      fprintf(stderr, "native Metal static benchmark failed\n");
    } else {
      native_print(&bench, &config, &metrics);
      result = EXIT_SUCCESS;
    }
    native_freeMetrics(&metrics);
    native_destroy(&bench);
  }
  return result;
}
