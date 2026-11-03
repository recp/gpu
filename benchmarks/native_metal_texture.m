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

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* compile texture_layout.usl to Metal first; both layouts use that same pipeline.
 * this isolates native sampling, not GPU-C encode, upload or app frame costs. */

typedef struct Quad {
  float rect[4], uv[4];
} Quad;

static const float corners[] = {0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1};
static const float scales[] = {0.5f, 1.0f, 2.0f};

static double
now_ns(void) {
  struct timespec value;

  clock_gettime(CLOCK_MONOTONIC, &value);
  return (double)value.tv_sec * 1e9 + (double)value.tv_nsec;
}

static bool
complete(id<MTLCommandBuffer> command) {
  [command commit];
  [command waitUntilCompleted];
  if (command.status != MTLCommandBufferStatusCompleted) {
    fprintf(stderr, "command failed: %s\n", command.error.description.UTF8String);
    return false;
  }
  return true;
}

static id<MTLRenderPipelineState>
pipeline(id<MTLDevice> device, id<MTLLibrary> library, MTLPixelFormat format) {
  MTLRenderPipelineDescriptor *desc;
  MTLVertexDescriptor         *vertex;
  NSError                     *error = nil;
  id<MTLRenderPipelineState>    result;
  uint32_t                     i;

  vertex = [MTLVertexDescriptor vertexDescriptor];
  vertex.attributes[0].format = MTLVertexFormatFloat2;
  vertex.attributes[0].bufferIndex = 0;
  vertex.layouts[0].stride = 2u * sizeof(float);
  for (i = 1u; i <= 2u; i++) {
    vertex.attributes[i].format = MTLVertexFormatFloat4;
    vertex.attributes[i].offset = (i - 1u) * 4u * sizeof(float);
    vertex.attributes[i].bufferIndex = 1;
  }
  vertex.layouts[1].stride = sizeof(Quad);
  vertex.layouts[1].stepFunction = MTLVertexStepFunctionPerInstance;

  desc = [MTLRenderPipelineDescriptor new];
  desc.vertexFunction = [library newFunctionWithName:@"layout_vs"];
  desc.fragmentFunction = [library newFunctionWithName:@"layout_fs"];
  desc.vertexDescriptor = vertex;
  desc.colorAttachments[0].pixelFormat = format;
  desc.colorAttachments[0].blendingEnabled = YES;
  desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorOne;
  desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
  desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  result = [device newRenderPipelineStateWithDescriptor:desc error:&error];
  if (!result)
    fprintf(stderr, "pipeline failed: %s\n", error.description.UTF8String);

  return result;
}

static uint32_t
scene(Quad *quads, uint32_t width, uint32_t height, uint32_t atlas,
      uint32_t zoom, uint32_t layers, bool constant) {
  float    step, x, y, u, v;
  uint32_t count = 0u, layer, tile, columns;

  if (constant) {
    quads[0] = (Quad){{0, 0, (float)width, (float)height},
                     {0, 0, 1.0f / atlas, 1.0f / atlas}};
    return 1u;
  }

  step = 256.0f * scales[zoom];
  columns = atlas / 256u;
  for (layer = 0u; layer < layers; layer++) {
    for (y = -0.25f; y < (float)height; y += step) {
      for (x = -0.5f; x < (float)width; x += step) {
        if (count == 512u)
          return 0u;

        tile = (count * 37u + layer * 17u) % (columns * columns);
        u = (float)(tile % columns) / columns;
        v = (float)(tile / columns) / columns;
        quads[count++] = (Quad){{x, y, x + step, y + step},
                               {u, v, u + 1.0f / columns, v + 1.0f / columns}};
      }
    }
  }
  return count;
}

static bool
render(id<MTLCommandQueue> queue, id<MTLRenderPipelineState> state,
       id<MTLTexture> target, id<MTLTexture> texture, id<MTLSamplerState> sampler,
       id<MTLBuffer> vertices, id<MTLBuffer> instances, uint32_t count,
       uint32_t repeats, double *gpu, double *cpu) {
  float                       viewport[4];
  MTLRenderPassDescriptor     *pass;
  id<MTLCommandBuffer>         command;
  id<MTLRenderCommandEncoder>  encoder;
  double                      begin;
  uint32_t                    i;

  viewport[0] = (float)target.width;
  viewport[1] = (float)target.height;
  viewport[2] = 0.5f / (float)texture.width;
  viewport[3] = 0.0f;
  pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = target;
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
  begin = now_ns();
  command = [queue commandBuffer];
  for (i = 0u; i < repeats; i++) {
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
      return false;

    [encoder setRenderPipelineState:state];
    [encoder setVertexBuffer:vertices offset:0 atIndex:0];
    [encoder setVertexBuffer:instances offset:0 atIndex:1];
    [encoder setVertexBytes:viewport length:sizeof(viewport) atIndex:30];
    [encoder setFragmentTexture:texture atIndex:0];
    [encoder setFragmentSamplerState:sampler atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6 instanceCount:count];
    [encoder endEncoding];
  }
  [command commit];
  *cpu = now_ns() - begin;
  [command waitUntilCompleted];
  *gpu = (command.GPUEndTime - command.GPUStartTime) * 1e9;
  return command.status == MTLCommandBufferStatusCompleted;
}

static bool
readback(id<MTLCommandQueue> queue, id<MTLTexture> texture,
         id<MTLBuffer> output, NSUInteger pitch) {
  id<MTLCommandBuffer>       command;
  id<MTLBlitCommandEncoder>  encoder;

  command = [queue commandBuffer];
  encoder = [command blitCommandEncoder];
  if (!encoder)
    return false;

  [encoder copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(texture.width, texture.height, 1)
                 toBuffer:output destinationOffset:0 destinationBytesPerRow:pitch
      destinationBytesPerImage:pitch * texture.height];
  [encoder endEncoding];
  return complete(command);
}

int
main(int argc, char **argv) {
  @autoreleasepool {
    Quad                       quads[512];
    const uint8_t              constant[4] = {32, 96, 192, 255};
    double                     gpu[2], cpu[2];
    MTLTextureDescriptor      *desc;
    MTLSamplerDescriptor      *sampling;
    NSError                   *error = nil;
    NSString                  *source;
    id<MTLDevice>              device;
    id<MTLCommandQueue>        queue;
    id<MTLCommandBuffer>       command;
    id<MTLBlitCommandEncoder>  blit;
    id<MTLLibrary>             library;
    id<MTLRenderPipelineState> state;
    id<MTLSamplerState>        sampler;
    id<MTLTexture>             textures[2], target;
    id<MTLBuffer>              buffer, vertices, instances, output[2], copied;
    uint8_t                   *host, *bytes;
    const float               *values;
    char                      *end;
    unsigned long              parsed;
    NSUInteger                 alignment, offset, pitch, total, row;
    NSUInteger                 outputPitch, outputBytes, pixelBytes, at;
    uint64_t                   checks = 0u;
    uint32_t                   size, width, height, x, y, alpha, format, zoom, layers;
    uint32_t                   count, pair, slot, variant, i;
    bool                       check, control, padded;

    if ((argc != 4 && argc != 5)
        || (strcmp(argv[2], "check") && strcmp(argv[2], "bench") && strcmp(argv[2], "control"))
        || (argc == 5 && strcmp(argv[4], "padded"))) {
      fprintf(stderr, "usage: %s <texture_layout.metal> <check|bench|control> <1024|2048|4096> [padded]\n", argv[0]);
      return 2;
    }

    parsed = strtoul(argv[3], &end, 10);
    if (*end || (parsed != 1024u && parsed != 2048u && parsed != 4096u))
      return 2;

    size = (uint32_t)parsed;
    check = strcmp(argv[2], "check") == 0;
    control = strcmp(argv[2], "control") == 0;
    padded = argc == 5;
    width = check ? 257u : 1024u;
    height = check ? 193u : 768u;
    device = MTLCreateSystemDefaultDevice();
    if (!device)
      return 77;

    alignment = [device minimumLinearTextureAlignmentForPixelFormat:MTLPixelFormatBGRA8Unorm];
    if (!alignment)
      return 77;

    offset = padded ? alignment : 0u;
    pitch = ((size * 4u + alignment - 1u) / alignment) * alignment + offset;
    total = offset + pitch * size + offset;
    buffer = [device newBufferWithLength:total options:MTLResourceStorageModeShared];
    copied = [device newBufferWithLength:(NSUInteger)size * size * 4u options:MTLResourceStorageModeShared];
    queue = [device newCommandQueue];
    vertices = [device newBufferWithBytes:corners length:sizeof(corners) options:MTLResourceStorageModeShared];
    instances = [device newBufferWithLength:sizeof(quads) options:MTLResourceStorageModeShared];
    if (!buffer || !copied || !queue || !vertices || !instances)
      return 3;

    host = buffer.contents;
    memset(host, 0xa5, total);
    for (y = 0u; y < size; y++) {
      for (x = 0u; x < size; x++) {
        at = offset + y * pitch + x * 4u;
        alpha = ((x / 256u + y / 256u) & 1u) ? 128u : 255u;
        host[at] = (uint8_t)((x ^ (y * 13u)) % (alpha + 1u));
        host[at + 1u] = (uint8_t)((y ^ (x * 7u)) % (alpha + 1u));
        host[at + 2u] = (uint8_t)(((x + y) & 3u) ? alpha : 0u);
        host[at + 3u] = (uint8_t)alpha;
      }
    }
    memcpy(host + offset, constant, sizeof(constant));

    desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                            width:size height:size mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    textures[1] = [buffer newTextureWithDescriptor:desc offset:offset bytesPerRow:pitch];
    desc.storageMode = MTLStorageModePrivate;
    textures[0] = [device newTextureWithDescriptor:desc];
    if (!textures[0] || !textures[1])
      return 3;

    command = [queue commandBuffer];
    blit = [command blitCommandEncoder];
    if (!blit)
      return 3;

    [blit copyFromBuffer:buffer sourceOffset:offset sourceBytesPerRow:pitch sourceBytesPerImage:pitch * size
             sourceSize:MTLSizeMake(size, size, 1) toTexture:textures[0] destinationSlice:0 destinationLevel:0
      destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    if (!complete(command) || !readback(queue, textures[0], copied, size * 4u))
      return 4;

    for (y = 0u; y < size; y++) {
      if (memcmp((uint8_t *)copied.contents + y * size * 4u, host + offset + y * pitch, size * 4u))
        return 5;
    }
    copied = nil;

    fprintf(stderr, "device=%s atlas=%u offset=%lu pitch=%lu buffer=%lu private=%lu control=%u\n",
            device.name.UTF8String, size, (unsigned long)offset, (unsigned long)pitch, (unsigned long)buffer.allocatedSize,
            (unsigned long)textures[0].allocatedSize, control);
    if (control)
      textures[1] = textures[0];

    source = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:argv[1]]
                                      encoding:NSUTF8StringEncoding error:&error];
    if (!source)
      return 3;

    library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
      fprintf(stderr, "library failed: %s\n", error.description.UTF8String);
      return 3;
    }
    sampling = [MTLSamplerDescriptor new];
    sampling.minFilter = sampling.magFilter = MTLSamplerMinMagFilterLinear;
    sampling.sAddressMode = sampling.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler = [device newSamplerStateWithDescriptor:sampling];
    if (!sampler)
      return 3;

    for (format = 0u; format < (check ? 2u : 1u); format++) {
      @autoreleasepool {
        pixelBytes = format ? 16u : 4u;
        outputPitch = ((width * pixelBytes + 255u) / 256u) * 256u;
        outputBytes = outputPitch * height;
        desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format ? MTLPixelFormatRGBA32Float : MTLPixelFormatBGRA8Unorm
                                                                width:width height:height mipmapped:NO];
        desc.storageMode = MTLStorageModePrivate;
        desc.usage = MTLTextureUsageRenderTarget;
        target = [device newTextureWithDescriptor:desc];
        state = pipeline(device, library, desc.pixelFormat);
        if (!target || !state)
          return 3;

        if (check) {
          output[0] = [device newBufferWithLength:outputBytes options:MTLResourceStorageModeShared];
          output[1] = [device newBufferWithLength:outputBytes options:MTLResourceStorageModeShared];
          if (!output[0] || !output[1])
            return 3;
        }

        for (zoom = 0u; zoom < (check ? 4u : 3u); zoom++) {
          for (layers = 1u; layers <= 4u; layers *= 4u) {
            count = scene(quads, width, height, size, zoom, layers, zoom == 3u);
            if (!count)
              return 3;

            memcpy(instances.contents, quads, count * sizeof(Quad));
            for (pair = 0u; pair < (check ? 1u : 28u); pair++) {
              for (slot = 0u; slot < 2u; slot++) {
                @autoreleasepool {
                  variant = (pair & 1u) ^ slot;
                  if (!render(queue, state, target, textures[variant], sampler, vertices,
                              instances, count, check ? 1u : 16u, &gpu[variant], &cpu[variant]))
                    return 4;

                  if (check && !readback(queue, target, output[variant], outputPitch))
                    return 4;
                }
              }
              if (!check && pair >= 4u) {
                if (!(gpu[0] > 0.0) || !(gpu[1] > 0.0))
                  return 4;

                printf("%u %u %u %u %.0f %.0f %.0f %.0f\n", size, zoom, layers, pair - 4u,
                       gpu[0], gpu[1], cpu[0], cpu[1]);
              }
            }
            if (check) {
              for (y = 0u; y < height; y++) {
                row = y * outputPitch;
                bytes = (uint8_t *)output[0].contents + row;
                if (memcmp(bytes, (uint8_t *)output[1].contents + row, width * pixelBytes)) {
                  fprintf(stderr, "image mismatch format=%u zoom=%u layers=%u row=%u\n", format, zoom, layers, y);
                  return 5;
                }
                for (x = 0u; x < width; x++) {
                  if (!format && zoom == 3u && memcmp(bytes + x * 4u, constant, 4u))
                    return 5;

                  if (format) {
                    values = (const float *)(bytes + x * 16u);
                    for (i = 0u; i < 4u; i++) {
                      if (!isfinite(values[i]))
                        return 5;
                    }
                  }
                }
                checks += width * 4u;
              }
            }
          }
        }
      }
    }
    for (at = 0u; at < total; at++) {
      if ((at < offset || at >= offset + pitch * size
           || (at - offset) % pitch >= size * 4u) && host[at] != 0xa5u)
        return 5;
    }
    if (check)
      printf("components=%llu mismatches=0 guards=pass\n", (unsigned long long)checks);

    return 0;
  }
}
