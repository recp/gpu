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
  NATIVE_DEFAULT_DRAWS    = 1000,
  NATIVE_DEFAULT_WARMUP   = 300,
  NATIVE_DEFAULT_FRAMES   = 3000,
  NATIVE_DEFAULT_REPEATS  = 5,
  NATIVE_STATE_TARGET     = 64,
  NATIVE_BINDING_COUNT    = 2,
  NATIVE_UPLOAD_ALIGNMENT = 256,
  NATIVE_UPLOAD_FRAMES    = 3,
  NATIVE_VERTEX_SLOT      = 29
};

typedef enum NativeMetalMode {
  NativeMetalModeStatic,
  NativeMetalModeState,
  NativeMetalModeBinding,
  NativeMetalModeUpload
} NativeMetalMode;

typedef struct NativeMetalConfig {
  NativeMetalMode mode;
  uint32_t        drawCount;
  uint32_t        warmupFrames;
  uint32_t        measuredFrames;
  uint32_t        repeats;
} NativeMetalConfig;

typedef struct NativeMetalBench {
  id<MTLDevice>              device;
  id<MTLCommandQueue>        queue;
  id<MTLLibrary>             library;
  id<MTLRenderPipelineState> pipelines[NATIVE_BINDING_COUNT];
  id<MTLDepthStencilState>   depthStates[NATIVE_BINDING_COUNT];
  id<MTLBuffer>              bindingBuffers[NATIVE_BINDING_COUNT];
  id<MTLBuffer>              vertexBuffer;
  id<MTLBuffer>              uploadBuffer;
  id<MTLTexture>             target;
  MTLRenderPassDescriptor   *renderPass;
  BenchProcessMemory         baselineMemory;
  NativeMetalMode            mode;
  uint32_t                   targetSize;
  uint32_t                   frameIndex;
  uint64_t                   uploadBytesPerFrame;
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

static const float nativeBindingColors[NATIVE_BINDING_COUNT][4] = {
  {1.0f, 0.2f, 0.1f, 1.0f},
  {0.1f, 0.4f, 1.0f, 1.0f}
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
   "}\n"
   "fragment float4 binding_fs(const device float4 *colors [[buffer(0)]]) {\n"
   "  return colors[0];\n"
   "}\n"
   "fragment float4 upload_fs(constant float4& tint [[buffer(0)]]) {\n"
   "  return tint;\n"
   "}\n";

static const char *native_modeName(NativeMetalMode mode) {
  static const char *names[] = {
    [NativeMetalModeStatic]  = "static scene",
    [NativeMetalModeState]   = "state churn",
    [NativeMetalModeBinding] = "binding churn",
    [NativeMetalModeUpload]  = "upload heavy"
  };

  return (uint32_t)mode < sizeof(names) / sizeof(names[0])
           ? names[mode]
           : "unknown";
}

static bool
native_parseMode(const char *text, NativeMetalMode *outMode) {
  if (!text || !outMode) {
    return false;
  }
  if (strcmp(text, "static") == 0) {
    *outMode = NativeMetalModeStatic;
    return true;
  }
  if (strcmp(text, "state") == 0) {
    *outMode = NativeMetalModeState;
    return true;
  }
  if (strcmp(text, "binding") == 0) {
    *outMode = NativeMetalModeBinding;
    return true;
  }
  if (strcmp(text, "upload") == 0) {
    *outMode = NativeMetalModeUpload;
    return true;
  }
  return false;
}

static bool
native_parseConfig(int argc, char *argv[], NativeMetalConfig *config) {
  if (!config || argc < 2 || argc > 6) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s <static|state|binding|upload> "
              "[draws] [warmup] [frames] [repeats]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->drawCount      = NATIVE_DEFAULT_DRAWS;
  config->warmupFrames   = NATIVE_DEFAULT_WARMUP;
  config->measuredFrames = NATIVE_DEFAULT_FRAMES;
  config->repeats        = NATIVE_DEFAULT_REPEATS;
  return native_parseMode(argv[1], &config->mode) &&
         (argc <= 2 || bench_parseU32(argv[2], 1u, &config->drawCount)) &&
         (argc <= 3 || bench_parseU32(argv[3], 0u, &config->warmupFrames)) &&
         (argc <= 4 || bench_parseU32(argv[4], 1u,
                                     &config->measuredFrames)) &&
         (argc <= 5 || bench_parseU32(argv[5], 1u, &config->repeats));
}

static bool
native_createPipeline(NativeMetalBench *bench,
                      uint32_t          index,
                      NSString         *fragmentName,
                      bool              blendEnabled) {
  MTLRenderPipelineDescriptor *pipelineDesc;
  MTLDepthStencilDescriptor   *depthDesc;
  MTLVertexDescriptor         *vertexDesc;
  id<MTLFunction>              fragmentFunction;
  id<MTLFunction>              vertexFunction;
  NSError                     *error;

  error            = nil;
  vertexFunction   = [bench->library newFunctionWithName:@"api_vs"];
  fragmentFunction = [bench->library newFunctionWithName:fragmentName];
  if (!vertexFunction || !fragmentFunction) {
    [fragmentFunction release];
    [vertexFunction release];
    return false;
  }

  vertexDesc = [MTLVertexDescriptor new];
  vertexDesc.attributes[0].format      = MTLVertexFormatFloat2;
  vertexDesc.attributes[0].offset      = 0u;
  vertexDesc.attributes[0].bufferIndex = NATIVE_VERTEX_SLOT;
  vertexDesc.layouts[NATIVE_VERTEX_SLOT].stride = 2u * sizeof(float);
  vertexDesc.layouts[NATIVE_VERTEX_SLOT].stepFunction =
    MTLVertexStepFunctionPerVertex;

  pipelineDesc = [MTLRenderPipelineDescriptor new];
  pipelineDesc.vertexFunction                  = vertexFunction;
  pipelineDesc.fragmentFunction                = fragmentFunction;
  pipelineDesc.vertexDescriptor                = vertexDesc;
  pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  if (blendEnabled) {
    pipelineDesc.colorAttachments[0].blendingEnabled = YES;
    pipelineDesc.colorAttachments[0].sourceRGBBlendFactor =
      MTLBlendFactorSourceAlpha;
    pipelineDesc.colorAttachments[0].destinationRGBBlendFactor =
      MTLBlendFactorOneMinusSourceAlpha;
    pipelineDesc.colorAttachments[0].rgbBlendOperation =
      MTLBlendOperationAdd;
    pipelineDesc.colorAttachments[0].sourceAlphaBlendFactor =
      MTLBlendFactorOne;
    pipelineDesc.colorAttachments[0].destinationAlphaBlendFactor =
      MTLBlendFactorZero;
    pipelineDesc.colorAttachments[0].alphaBlendOperation =
      MTLBlendOperationAdd;
  }
  bench->pipelines[index] = [bench->device
    newRenderPipelineStateWithDescriptor:pipelineDesc
                                   error:&error];

  depthDesc = [MTLDepthStencilDescriptor new];
  bench->depthStates[index] = [bench->device
    newDepthStencilStateWithDescriptor:depthDesc];
  [depthDesc release];
  [pipelineDesc release];
  [vertexDesc release];
  [fragmentFunction release];
  [vertexFunction release];
  if (!bench->pipelines[index] || !bench->depthStates[index]) {
    fprintf(stderr, "native Metal pipeline error: %s\n",
            error ? error.localizedDescription.UTF8String : "unknown");
    return false;
  }
  return true;
}

static bool
native_createModeResources(NativeMetalBench        *bench,
                           const NativeMetalConfig *config) {
  NSString *fragmentName;

  fragmentName = @"api_fs";
  if (config->mode == NativeMetalModeBinding) {
    fragmentName = @"binding_fs";
  } else if (config->mode == NativeMetalModeUpload) {
    fragmentName = @"upload_fs";
  }
  if (!native_createPipeline(bench, 0u, fragmentName, false)) {
    return false;
  }
  if (config->mode == NativeMetalModeState &&
      !native_createPipeline(bench, 1u, fragmentName, true)) {
    return false;
  }

  if (config->mode == NativeMetalModeBinding) {
    for (uint32_t i = 0u; i < NATIVE_BINDING_COUNT; i++) {
      bench->bindingBuffers[i] = [bench->device
        newBufferWithBytes:nativeBindingColors[i]
                    length:sizeof(nativeBindingColors[i])
                   options:MTLResourceStorageModeShared];
      if (!bench->bindingBuffers[i]) {
        return false;
      }
    }
  }

  if (config->mode == NativeMetalModeUpload) {
    uint64_t totalBytes;

    bench->uploadBytesPerFrame =
      (uint64_t)config->drawCount * NATIVE_UPLOAD_ALIGNMENT;
    if (bench->uploadBytesPerFrame > UINT64_MAX / NATIVE_UPLOAD_FRAMES) {
      return false;
    }
    totalBytes = bench->uploadBytesPerFrame * NATIVE_UPLOAD_FRAMES;
    if (totalBytes > SIZE_MAX) {
      return false;
    }
    bench->uploadBuffer = [bench->device
      newBufferWithLength:(NSUInteger)totalBytes
                  options:MTLResourceStorageModeShared];
    if (!bench->uploadBuffer) {
      return false;
    }
  }
  return true;
}

static bool
native_init(NativeMetalBench        *bench,
            const NativeMetalConfig *config) {
  MTLTextureDescriptor *textureDesc;
  NSError              *error;

  memset(bench, 0, sizeof(*bench));
  (void)bench_processMemory(&bench->baselineMemory);
  bench->mode       = config->mode;
  bench->targetSize = config->mode == NativeMetalModeState
                        ? NATIVE_STATE_TARGET
                        : 1u;
  error             = nil;
  bench->device      = MTLCreateSystemDefaultDevice();
  bench->queue       = [bench->device newCommandQueue];
  bench->library     = [bench->device newLibraryWithSource:nativeShaderSource
                                                   options:nil
                                                     error:&error];
  if (!bench->device || !bench->queue || !bench->library) {
    if (error) {
      fprintf(stderr, "native Metal shader error: %s\n",
              error.localizedDescription.UTF8String);
    }
    return false;
  }
  if (!native_createModeResources(bench, config)) {
    return false;
  }

  bench->vertexBuffer = [bench->device
    newBufferWithBytes:nativeVertices
                length:sizeof(nativeVertices)
               options:MTLResourceStorageModeShared];
  textureDesc = [MTLTextureDescriptor
    texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                 width:bench->targetSize
                                height:bench->targetSize
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
  [bench->uploadBuffer release];
  [bench->vertexBuffer release];
  for (uint32_t i = 0u; i < NATIVE_BINDING_COUNT; i++) {
    [bench->bindingBuffers[i] release];
    [bench->depthStates[i] release];
    [bench->pipelines[i] release];
  }
  [bench->library release];
  [bench->queue release];
  [bench->device release];
}

static void
native_bindPipeline(NativeMetalBench             *bench,
                    id<MTLRenderCommandEncoder>   encoder,
                    uint32_t                      index) {
  [encoder setRenderPipelineState:bench->pipelines[index]];
  [encoder setDepthStencilState:bench->depthStates[index]];
  [encoder setCullMode:MTLCullModeNone];
  [encoder setFrontFacingWinding:index == 1u
                                  ? MTLWindingClockwise
                                  : MTLWindingCounterClockwise];
}

static void
native_draw(id<MTLRenderCommandEncoder> encoder) {
  [encoder drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0u
              vertexCount:3u
            instanceCount:1u
             baseInstance:0u];
}

static bool
native_encodeStatic(NativeMetalBench           *bench,
                    id<MTLRenderCommandEncoder> encoder,
                    uint32_t                    drawCount) {
  native_bindPipeline(bench, encoder, 0u);
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    native_draw(encoder);
  }
  return true;
}

static bool
native_encodeState(NativeMetalBench           *bench,
                   id<MTLRenderCommandEncoder> encoder,
                   uint32_t                    drawCount) {
  uint32_t previousState;

  previousState = UINT32_MAX;
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    uint32_t stateIndex;

    stateIndex = (draw >> 1u) & 1u;
    if (stateIndex != previousState) {
      double          inset;
      double          size;
      MTLScissorRect  scissor;
      MTLViewport     viewport;

      inset          = stateIndex == 0u ? 0.0 : 1.0;
      size           = stateIndex == 0u
                         ? NATIVE_STATE_TARGET
                         : NATIVE_STATE_TARGET - 2u;
      viewport.originX = inset;
      viewport.originY = inset;
      viewport.width   = size;
      viewport.height  = size;
      viewport.znear   = 0.0;
      viewport.zfar    = 1.0;
      scissor.x      = (NSUInteger)inset;
      scissor.y      = (NSUInteger)inset;
      scissor.width  = (NSUInteger)size;
      scissor.height = (NSUInteger)size;

      native_bindPipeline(bench, encoder, stateIndex);
      [encoder setViewport:viewport];
      [encoder setScissorRect:scissor];
      [encoder setBlendColorRed:stateIndex == 0u ? 0.0f : 1.0f
                          green:stateIndex == 0u ? 0.0f : 0.5f
                           blue:stateIndex == 0u ? 0.0f : 0.25f
                          alpha:stateIndex == 0u ? 0.0f : 1.0f];
      [encoder setStencilReferenceValue:stateIndex];
      previousState = stateIndex;
    }
    native_draw(encoder);
  }
  return true;
}

static bool
native_encodeBinding(NativeMetalBench           *bench,
                     id<MTLRenderCommandEncoder> encoder,
                     uint32_t                    drawCount) {
  uint32_t previousGroup;

  native_bindPipeline(bench, encoder, 0u);
  previousGroup = UINT32_MAX;
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    uint32_t groupIndex;

    groupIndex = (draw >> 1u) & 1u;
    if (groupIndex != previousGroup) {
      [encoder setFragmentBuffer:bench->bindingBuffers[groupIndex]
                          offset:0u
                         atIndex:0u];
      previousGroup = groupIndex;
    }
    native_draw(encoder);
  }
  return true;
}

static bool
native_encodeUpload(NativeMetalBench           *bench,
                    id<MTLRenderCommandEncoder> encoder,
                    uint32_t                    drawCount) {
  uint64_t frameBase;
  uint8_t *bytes;

  frameBase = (uint64_t)(bench->frameIndex % NATIVE_UPLOAD_FRAMES) *
              bench->uploadBytesPerFrame;
  bytes = bench->uploadBuffer.contents;
  if (!bytes) {
    return false;
  }

  native_bindPipeline(bench, encoder, 0u);
  for (uint32_t draw = 0u; draw < drawCount; draw++) {
    float    tint[4];
    uint64_t uniformOffset;
    uint64_t vertexOffset;

    tint[0]       = (draw & 1u) ? 0.2f : 1.0f;
    tint[1]       = (draw & 2u) ? 1.0f : 0.3f;
    tint[2]       = (draw & 4u) ? 0.4f : 1.0f;
    tint[3]       = 1.0f;
    uniformOffset = frameBase + (uint64_t)draw * NATIVE_UPLOAD_ALIGNMENT;
    vertexOffset  = uniformOffset + sizeof(tint);
    memcpy(bytes + uniformOffset, tint, sizeof(tint));
    memcpy(bytes + vertexOffset, nativeVertices, sizeof(nativeVertices));
    [encoder setVertexBuffer:bench->uploadBuffer
                      offset:(NSUInteger)vertexOffset
                     atIndex:NATIVE_VERTEX_SLOT];
    [encoder setFragmentBuffer:bench->uploadBuffer
                        offset:(NSUInteger)uniformOffset
                       atIndex:0u];
    native_draw(encoder);
  }
  bench->frameIndex++;
  return true;
}

static bool
native_encode(NativeMetalBench           *bench,
              id<MTLRenderCommandEncoder> encoder,
              uint32_t                    drawCount) {
  [encoder setVertexBuffer:bench->vertexBuffer
                    offset:0u
                   atIndex:NATIVE_VERTEX_SLOT];
  switch (bench->mode) {
    case NativeMetalModeStatic:
      return native_encodeStatic(bench, encoder, drawCount);
    case NativeMetalModeState:
      return native_encodeState(bench, encoder, drawCount);
    case NativeMetalModeBinding:
      return native_encodeBinding(bench, encoder, drawCount);
    case NativeMetalModeUpload:
      return native_encodeUpload(bench, encoder, drawCount);
  }
  return false;
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
    if (!commandBuffer || !encoder ||
        !native_encode(bench, encoder, drawCount)) {
      return false;
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
  printf("Native Metal %s benchmark\n", native_modeName(config->mode));
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
        !native_init(&bench, &config) ||
        !native_run(&bench, &config, &metrics)) {
      fprintf(stderr, "native Metal render benchmark failed\n");
    } else {
      native_print(&bench, &config, &metrics);
      result = EXIT_SUCCESS;
    }
    native_freeMetrics(&metrics);
    native_destroy(&bench);
  }
  return result;
}
