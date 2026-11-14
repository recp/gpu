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

#include "../../src/backend/mt/common.h"
#include <math.h>
#include <pthread.h>

static bool
check_buffer_texture(GPUDevice *device) {
  const GPUFormat formats[] = {GPU_FORMAT_RGBA8_UNORM, GPU_FORMAT_BGRA8_UNORM};
  GPUBufferCreateInfo        bufferInfo = {0};
  GPUBufferTextureCreateInfo info       = {0};
  GPUBufferTextureCreateInfo invalid;
  GPUBuffer                *buffer = NULL;
  GPUTexture               *texture = NULL;
  GPUTexture               *bad;
  GPUDeviceMT              *native;
  id<MTLCommandQueue>        queue;
  id<MTLLibrary>             library;
  id<MTLFunction>            function;
  id<MTLComputePipelineState> pipeline;
  id<MTLTexture>             reference;
  id<MTLBuffer>              result;
  id<MTLCommandBuffer>       command;
  id<MTLComputeCommandEncoder> encoder;
  MTLTextureDescriptor     *desc;
  NSError                  *error = nil;
  uint8_t                  *host;
  const float              *values;
  uint32_t                  alignment, pitch, format, epoch, x, y, c, index;
  bool                      ok = false;

  native = device->_priv;
  queue  = [native->device newCommandQueue];
  library = [native->device newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
      "kernel void sample_image(texture2d<float> a [[texture(0)]], texture2d<float> b [[texture(1)]],"
      "device float4 *out [[buffer(0)]], uint2 p [[thread_position_in_grid]]) {"
      "constexpr sampler s(coord::normalized, address::clamp_to_edge, filter::nearest);"
      "uint i=p.y*8+p.x; float2 uv=(float2(p)+0.5)/float2(8,4);"
      "out[i]=a.sample(s,uv,level(0)); out[32+i]=b.sample(s,uv,level(0));}"
                                            options:nil error:&error];
  function = [library newFunctionWithName:@"sample_image"];
  pipeline = function ? [native->device newComputePipelineStateWithFunction:function error:&error] : nil;
  [function release];
  result = [native->device newBufferWithLength:64u * 16u options:MTLResourceStorageModeShared];
  reference = nil;
  if (!queue || !pipeline || !result)
    goto cleanup;

  for (format = 0u; format < 2u; format++) {
    alignment = GPUGetLinearTextureAlignment(device, formats[format]);
    if (!alignment)
      goto cleanup;

    pitch = ((32u + alignment - 1u) / alignment) * alignment;
    bufferInfo.sizeBytes = alignment + pitch * 4u + alignment;
    bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
    if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK
        || !(host = GPUGetBufferHostAddress(buffer)))
      goto cleanup;

    info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_TEXTURE_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.buffer           = buffer;
    info.offset           = alignment;
    info.bytesPerRow      = pitch;
    info.format           = formats[format];
    info.width            = 8u;
    info.height           = 4u;
    info.usage            = GPU_TEXTURE_USAGE_SAMPLED;
    invalid = info;
    invalid.offset++;
    bad = (GPUTexture *)(uintptr_t)1u;
    if (GPUCreateTextureFromBuffer(device, &invalid, &bad) != GPU_ERROR_INVALID_ARGUMENT || bad)
      goto cleanup;

    invalid = info;
    invalid.bytesPerRow++;
    if (GPUCreateTextureFromBuffer(device, &invalid, &bad) != GPU_ERROR_INVALID_ARGUMENT || bad)
      goto cleanup;

    invalid = info;
    invalid.height = UINT32_MAX;
    if (GPUCreateTextureFromBuffer(device, &invalid, &bad) != GPU_ERROR_INVALID_ARGUMENT || bad)
      goto cleanup;

    invalid = info;
    invalid.usage |= GPU_TEXTURE_USAGE_STORAGE;
    if (GPUCreateTextureFromBuffer(device, &invalid, &bad) != GPU_ERROR_INVALID_ARGUMENT || bad)
      goto cleanup;

    if (GPUCreateTextureFromBuffer(device, &info, &texture) != GPU_OK)
      goto cleanup;

    desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:((GPUTextureMT *)texture->_priv)->texture.pixelFormat
                                                            width:8u height:4u mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    reference = [native->device newTextureWithDescriptor:desc];
    for (epoch = 0u; epoch < 2u; epoch++) {
      memset(host, 0xa5, (size_t)bufferInfo.sizeBytes);
      for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 32u; x++) {
          host[alignment + y * pitch + x] = (uint8_t)(epoch * 71u + y * 37u + x * 11u);
        }
      }

      [reference replaceRegion:MTLRegionMake2D(0, 0, 8, 4) mipmapLevel:0
                      withBytes:host + alignment bytesPerRow:pitch];
      command = [queue commandBuffer];
      encoder = [command computeCommandEncoder];
      [encoder setComputePipelineState:pipeline];
      [encoder setTexture:((GPUTextureMT *)texture->_priv)->texture atIndex:0];
      [encoder setTexture:reference atIndex:1];
      [encoder setBuffer:result offset:0 atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(8, 4, 1) threadsPerThreadgroup:MTLSizeMake(4, 4, 1)];
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      values = result.contents;
      if (command.status != MTLCommandBufferStatusCompleted
          || memcmp(values, values + 128u, 128u * sizeof(float)))
        goto cleanup;

      for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 8u; x++) {
          for (c = 0u; c < 4u; c++) {
            index = format == 1u && c < 3u ? 2u - c : c;
            if (lroundf(values[(y * 8u + x) * 4u + c] * 255.0f) != host[alignment + y * pitch + x * 4u + index])
              goto cleanup;
          }
        }

        for (x = 32u; x < pitch; x++) {
          if (host[alignment + y * pitch + x] != 0xa5u)
            goto cleanup;
        }
      }

      for (x = 0u; x < alignment; x++) {
        if (host[x] != 0xa5u || host[alignment + 4u * pitch + x] != 0xa5u)
          goto cleanup;
      }
    }

    [reference release];
    reference = nil;
    GPUDestroyTexture(texture);
    GPUDestroyBuffer(buffer);
    texture = NULL;
    buffer  = NULL;
  }
  ok = true;

cleanup:
  [reference release];
  [result release];
  [pipeline release];
  [library release];
  [queue release];
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(buffer);
  printf("buffer textures: RGBA/BGRA, padded rows, offset, CPU rewrites and bit-exact native sampling=%s\n", ok ? "pass" : "fail");
  return ok;
}

static bool
check_write_order(GPUDevice *device) {
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureWriteRegion         write       = {0};
  GPUBufferCreateInfo           bufferInfo  = {0};
  GPUBufferTextureCopyRegion    copy        = {0};
  GPUQueueSubmitExInfo          submit      = {0};
  GPUQueueSemaphoreWait         wait        = {0};
  uint8_t                       oldPixels[64], newPixels[64], actual[2048];
  GPUQueue                     *queue;
  GPUTexture                   *texture = NULL;
  GPUBuffer                    *buffer = NULL;
  GPUSemaphore                 *gate = NULL;
  GPUFence                     *fence = NULL;
  GPUCommandBuffer             *command = NULL;
  GPUTransferPassEncoder       *pass;
  uint32_t                      phase, row;
  bool                          ok = false;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  memset(oldPixels, 0x37, sizeof(oldPixels));
  memset(newPixels, 0xb9, sizeof(newPixels));
  textureInfo.dimension     = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format        = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width         = 4u;
  textureInfo.height        = 4u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount   = 1u;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;
  bufferInfo.sizeBytes = sizeof(actual);
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  write.width = write.height = 4u;
  write.depth = write.layerCount = 1u;
  write.bytesPerRow = 16u;
  copy.texture.width = copy.texture.height = 4u;
  copy.texture.depth = copy.texture.layerCount = 1u;
  copy.bytesPerRow = 256u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK
      || GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK
      || GPUCreateSemaphore(device, NULL, &gate) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK
      || GPUQueueWriteTexture(queue, texture, &write, oldPixels, sizeof(oldPixels)) != GPU_OK)
    goto cleanup;

  for (phase = 0u; phase < 2u; phase++) {
    if (GPUAcquireCommandBuffer(queue, "ordered snapshot", &command) != GPU_OK
        || !(pass = GPUBeginTransferPass(command, "snapshot before/after write")))
      goto cleanup;

    copy.bufferOffset = (uint64_t)phase * 1024u;
    GPUCopyTextureToBuffer(pass, texture, buffer, &copy);
    GPUEndTransferPass(pass);
    submit.ppCommandBuffers   = &command;
    submit.commandBufferCount = 1u;
    submit.waitCount           = phase == 0u ? 1u : 0u;
    submit.pWaits              = &wait;
    submit.fence               = phase == 1u ? fence : NULL;
    wait.semaphore = gate;
    wait.value     = 1u;
    wait.waitStages = GPU_STAGE_TRANSFER;
    if (GPUQueueSubmitEx(queue, &submit) != GPU_OK)
      goto cleanup;

    command = NULL;
    if (phase == 0u && GPUQueueWriteTexture(queue, texture, &write, newPixels, sizeof(newPixels)) != GPU_OK)
      goto cleanup;
  }

  [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];
  if (GPUWaitFence(fence, 2000000000ull) != GPU_OK
      || GPUQueueReadBuffer(queue, buffer, 0u, actual, sizeof(actual)) != GPU_OK)
    goto cleanup;

  ok = true;
  for (row = 0u; row < 4u; row++) {
    if (memcmp(actual + row * 256u, oldPixels + row * 16u, 16u)
        || memcmp(actual + 1024u + row * 256u, newPixels + row * 16u, 16u))
      ok = false;
  }

cleanup:
  if (gate)
    [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];

  if (command)
    GPUDiscardCommandBuffer(command);

  if (fence)
    GPUWaitFence(fence, 2000000000ull);

  GPUDestroyFence(fence);
  GPUDestroySemaphore(gate);
  GPUDestroyBuffer(buffer);
  GPUDestroyTexture(texture);
  printf("texture write queue order: blocked prior read retains old pixels=%s\n", ok ? "pass" : "fail");
  return ok;
}

typedef struct UploadWorker {
  GPUQueue   *queue;
  GPUTexture *texture;
  GPUBuffer  *buffer;
  uint32_t    index;
  uint32_t    epochs;
  bool        ok;
} UploadWorker;

static void*
upload_worker(void *user) {
  @autoreleasepool {
    GPUUploaderCreateInfo info = {0};
    GPUTextureWriteRegion write = {0};
    uint8_t pixels[72], payload[17];
    UploadWorker *worker;
    GPUUploader *uploader = NULL;
    GPUFence *first = NULL, *last = NULL, *empty = NULL;
    uint32_t epoch, i;

    worker = user;
    info.chain.sType      = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
    info.chain.structSize = sizeof(info);
    info.stagingSizeBytes = 2048u;
    info.stagingBufferCount = 2u;
    write.x           = worker->index * 8u + 1u;
    write.y           = 1u;
    write.width       = 4u;
    write.height      = 3u;
    write.depth       = 1u;
    write.layerCount  = 1u;
    write.bytesPerRow = 24u;
    worker->ok = GPUCreateUploader(worker->queue, &info, &uploader) == GPU_OK;
    worker->ok = worker->ok && GPUFlushUploads(uploader, NULL) == GPU_OK
                 && GPUFlushUploads(uploader, &empty) == GPU_OK
                 && GPUWaitFence(empty, 0u) == GPU_OK;
    GPUDestroyFence(empty);
    empty = NULL;

    for (epoch = 0u; worker->ok && epoch < worker->epochs; epoch++) {
      for (i = 0u; i < sizeof(pixels); i++) {
        pixels[i] = (uint8_t)(worker->index * 43u + epoch * 7u + i);
      }
      memset(payload, (int)(worker->index * 43u + epoch), sizeof(payload));
      worker->ok = GPUUploadTexture(uploader, worker->texture, &write, pixels, sizeof(pixels)) == GPU_OK
                   && GPUUploadBuffer(uploader, worker->buffer, worker->index * 64u + 7u,
                                       payload, sizeof(payload)) == GPU_OK;
      memset(pixels, 0xee, sizeof(pixels));
      memset(payload, 0xee, sizeof(payload));
      if (epoch == 0u)
        worker->ok = worker->ok && GPUFlushUploads(uploader, &first) == GPU_OK;
    }

    worker->ok = worker->ok && GPUFlushUploads(uploader, &last) == GPU_OK
                 && GPUFlushUploads(uploader, &empty) == GPU_OK
                 && GPUWaitFence(empty, 2000000000ull) == GPU_OK
                 && GPUWaitFence(last, 0u) == GPU_OK
                 && GPUWaitFence(first, 0u) == GPU_OK;
    /* caller references survive every ring reuse and uploader destruction. */
    GPUDestroyUploader(uploader);
    worker->ok = worker->ok && GPUIsFenceSignaled(first) && GPUIsFenceSignaled(last)
                 && GPUIsFenceSignaled(empty);
    GPUDestroyFence(first);
    GPUDestroyFence(last);
    GPUDestroyFence(empty);
    return NULL;
  }
}

static bool
check_uploaders(GPUDevice *device) {
  GPUTextureCreateInfo       textureInfo = {0};
  GPUBufferCreateInfo        bufferInfo  = {0};
  GPUTextureWriteRegion      write       = {0};
  GPUBufferTextureCopyRegion copy        = {0};
  GPUBufferCopyRegion        bufferCopy  = {0};
  GPUQueueSubmitInfo         submit      = {0};
  UploadWorker               workers[4] = {0};
  pthread_t                  threads[4];
  uint8_t                    initial[1024], pixels[2048], contents[256], expected;
  GPUQueue                  *queue;
  GPUTexture                *texture = NULL;
  GPUBuffer                 *buffer = NULL, *readback = NULL;
  GPUCommandBuffer          *command;
  GPUTransferPassEncoder    *pass;
  GPUFence                  *fence = NULL;
  uint32_t                   i, created = 0u, x, y, c, index;
  uint32_t                   epochs;
  bool                       ok = false;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  epochs = getenv("GPU_PDF_UPLOAD_ODD") ? 49u : 48u;
  textureInfo.dimension     = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format        = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width         = 32u;
  textureInfo.height        = 8u;
  textureInfo.depthOrLayers = 1u;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount   = 1u;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;
  bufferInfo.sizeBytes = 256u;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK
      || GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK)
    goto cleanup;

  bufferInfo.sizeBytes = sizeof(pixels);
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK)
    goto cleanup;

  memset(initial, 0x33, sizeof(initial));
  memset(contents, 0x91, sizeof(contents));
  write.width = 32u;
  write.height = 8u;
  write.depth = write.layerCount = 1u;
  write.bytesPerRow = 128u;
  if (GPUQueueWriteTexture(queue, texture, &write, initial, sizeof(initial)) != GPU_OK
      || GPUQueueWriteBuffer(queue, buffer, 0u, contents, sizeof(contents)) != GPU_OK)
    goto cleanup;

  for (i = 0u; i < 4u; i++) {
    workers[i].queue   = queue;
    workers[i].texture = texture;
    workers[i].buffer  = buffer;
    workers[i].index   = i;
    workers[i].epochs  = epochs;
    if (pthread_create(&threads[i], NULL, upload_worker, &workers[i]) != 0)
      goto cleanup;

    created++;
  }

  for (i = 0u; i < 64u; i++) {
    GPUResetStats(device);
    if (GPUAcquireCommandBuffer(queue, "concurrent consumer", &command) != GPU_OK
        || !(pass = GPUBeginTransferPass(command, "consumer copy")))
      goto cleanup;

    bufferCopy.sizeBytes = sizeof(contents);
    GPUCopyBufferToBuffer(pass, buffer, readback, &bufferCopy);
    GPUEndTransferPass(pass);
    submit.ppCommandBuffers   = &command;
    submit.commandBufferCount = 1u;
    submit.fence               = fence;
    if (GPUQueueSubmit(queue, &submit) != GPU_OK || GPUWaitFence(fence, 2000000000ull) != GPU_OK)
      goto cleanup;
  }

  for (i = 0u; i < created; i++) {
    pthread_join(threads[i], NULL);
  }
  created = 0u;
  for (i = 0u; i < 4u; i++) {
    if (!workers[i].ok) {
      fprintf(stderr, "uploader worker %u failed\n", i);
      goto cleanup;
    }
  }

  if (GPUQueueReadBuffer(queue, buffer, 0u, contents, sizeof(contents)) != GPU_OK
      || GPUAcquireCommandBuffer(queue, "upload readback", &command) != GPU_OK
      || !(pass = GPUBeginTransferPass(command, "atlas readback")))
    goto cleanup;

  copy.texture.width  = 32u;
  copy.texture.height = 8u;
  copy.texture.depth = copy.texture.layerCount = 1u;
  copy.bytesPerRow = 256u;
  GPUCopyTextureToBuffer(pass, texture, readback, &copy);
  GPUEndTransferPass(pass);
  submit.ppCommandBuffers = &command;
  if (GPUQueueSubmit(queue, &submit) != GPU_OK || GPUWaitFence(fence, 2000000000ull) != GPU_OK
      || GPUQueueReadBuffer(queue, readback, 0u, pixels, sizeof(pixels)) != GPU_OK)
    goto cleanup;

  ok = true;
  for (i = 0u; i < sizeof(contents); i++) {
    index = i / 64u;
    expected = i % 64u >= 7u && i % 64u < 24u ? (uint8_t)(index * 43u + epochs - 1u) : 0x91u;
    if (contents[i] != expected) {
      fprintf(stderr, "uploaded buffer byte %u: got %u expected %u\n", i, contents[i], expected);
      ok = false;
    }
  }

  for (y = 0u; y < 8u; y++) {
    for (x = 0u; x < 32u; x++) {
      for (c = 0u; c < 4u; c++) {
        index = x / 8u;
        expected = y >= 1u && y < 4u && x % 8u >= 1u && x % 8u < 5u
                     ? (uint8_t)(index * 43u + (epochs - 1u) * 7u + (y - 1u) * 24u + (x % 8u - 1u) * 4u + c) : 0x33u;
        if (pixels[y * 256u + x * 4u + c] != expected) {
          fprintf(stderr, "uploaded pixel %u,%u:%u: got %u expected %u\n", x, y, c,
                  pixels[y * 256u + x * 4u + c], expected);
          ok = false;
        }
      }
    }
  }

cleanup:
  for (i = 0u; i < created; i++) {
    pthread_join(threads[i], NULL);
  }
  GPUDestroyFence(fence);
  GPUDestroyBuffer(readback);
  GPUDestroyBuffer(buffer);
  GPUDestroyTexture(texture);
  printf("uploaders: four workers, %u texture/buffer pairs, bounded ring, retained fences and concurrent submits=%s\n",
         epochs * 4u, ok ? "pass" : "fail");
  return ok;
}

int
main(void) {
  @autoreleasepool {
    GPUDeviceCreateInfo info = {0};
    const GPUFeature features[] = {GPU_FEATURE_BUFFER_TEXTURES, GPU_FEATURE_FRAME_PACER, GPU_FEATURE_THREADED_UPLOADS};
    GPUInstance *instance = NULL;
    GPUAdapter  *adapter;
    GPUDevice   *device = NULL;
    bool         ok;

    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance)))
      return 1;

    info.required.pFeatures    = features;
    info.required.featureCount = getenv("GPU_PDF_ORDER_ONLY") ? 2u : 3u;
    if (GPUCreateDevice(adapter, &info, &device) != GPU_OK)
      return 77;

    ok = getenv("GPU_PDF_UPLOAD_ONLY") ? check_uploaders(device) : check_write_order(device);
    if (!getenv("GPU_PDF_ORDER_ONLY") && !getenv("GPU_PDF_UPLOAD_ONLY"))
      ok = check_uploaders(device) && check_buffer_texture(device) && ok;
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
