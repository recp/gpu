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

/*
 * GPU_FEATURE_THREADED_UPLOADS on any backend: worker threads, each with its
 * own uploader on the one queue, upload a buffer whole and a texture a band
 * of rows at a time, round after round through small rings that wrap, while
 * the main thread writes another buffer on the queue and submits command
 * buffers of its own. every byte checked after. skips (77) where the device
 * doesn't offer the feature.
 */

#include "test.h"

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
typedef HANDLE TUThread;
#else
#  include <pthread.h>
typedef pthread_t TUThread;
#endif

enum {
  TU_WORKERS     = 4u,
  TU_ROUNDS      = 8u,
  TU_BUFFER      = 48u * 1024u, /* bigger than a slot: wraps the ring */
  TU_WIDTH       = 64u,
  TU_HEIGHT      = 32u,         /* TU_ROUNDS bands of 4 rows */
  TU_BAND        = TU_HEIGHT / TU_ROUNDS,
  TU_ROW         = 256u,        /* readback rows */
  TU_MAIN_WRITES = 256u
};

typedef struct TUWorker {
  GPUQueue   *queue;
  GPUBuffer  *buffer;
  GPUTexture *texture;
  uint32_t    index;
  const char *failure;
} TUWorker;

static volatile int tu_running;

static uint8_t
tu_byte(uint32_t worker, uint32_t round, uint32_t at) {
  return (uint8_t)(worker * 61u + round * 17u + at * 7u + (at >> 8));
}

static void *
tu_work(void *arg) {
  GPUUploaderCreateInfo info   = {0};
  GPUTextureWriteRegion region = {0};
  TUWorker             *w      = arg;
  GPUUploader          *uploader;
  GPUFence             *fence;
  static uint8_t        buffers[TU_WORKERS][TU_BUFFER];
  uint8_t               band[TU_WIDTH * 4u * TU_BAND];
  uint8_t              *bytes;
  uint32_t              round, i;

  info.chain.sType        = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
  info.chain.structSize   = sizeof(info);
  info.label              = "threaded uploads";
  info.stagingSizeBytes   = 64u * 1024u;
  info.stagingBufferCount = 2u;
  uploader                = NULL;
  bytes                   = buffers[w->index];

  if (GPUCreateUploader(w->queue, &info, &uploader) != GPU_OK || !uploader) {
    w->failure = "create uploader";
    return NULL;
  }

  for (round = 0u; round < TU_ROUNDS && !w->failure; round++) {
    for (i = 0u; i < TU_BUFFER; i++)
      bytes[i] = tu_byte(w->index, round, i);

    for (i = 0u; i < sizeof(band); i++)
      band[i] = tu_byte(w->index + 8u, round, i);

    region.width       = TU_WIDTH;
    region.height      = TU_BAND;
    region.depth       = 1u;
    region.layerCount  = 1u;
    region.y           = round * TU_BAND;
    region.bytesPerRow = TU_WIDTH * 4u;

    fence = NULL;

    if (GPUUploadBuffer(uploader, w->buffer, 0u, bytes, TU_BUFFER) != GPU_OK)
      w->failure = "upload buffer";
    else if (GPUUploadTexture(uploader, w->texture, &region, band, sizeof(band)) != GPU_OK)
      w->failure = "upload texture";
    else if (GPUFlushUploads(uploader, &fence) != GPU_OK)
      w->failure = "flush";

    GPUDestroyFence(fence);
  }

  GPUDestroyUploader(uploader); /* waits for its slots */

  return NULL;
}

static bool
tu_start(TUThread *thread, TUWorker *worker) {
#if defined(_WIN32) || defined(WIN32)
  *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)tu_work, worker, 0, NULL);
  return *thread != NULL;
#else
  return pthread_create(thread, NULL, tu_work, worker) == 0;
#endif
}

static void
tu_join(TUThread thread) {
#if defined(_WIN32) || defined(WIN32)
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
#else
  pthread_join(thread, NULL);
#endif
}

static GPUResult
tu_submit(GPUQueue *queue, GPUCommandBuffer *command, GPUFence *fence) {
  GPUQueueSubmitInfo submit = {0};

  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = &command;
  submit.fence              = fence;

  return GPUQueueSubmit(queue, &submit);
}

int
main(void) {
  GPUBufferCreateInfo  bufferInfo  = {0};
  GPUTextureCreateInfo textureInfo = {0};
  GPUDeviceCreateInfo  deviceInfo  = {0};
  const GPUFeature     feature     = GPU_FEATURE_THREADED_UPLOADS;
  TUWorker             workers[TU_WORKERS];
  TUThread             threads[TU_WORKERS];
  GPUInstance         *instance = NULL;
  GPUAdapter          *adapter  = NULL;
  GPUDevice           *device   = NULL;
  GPUQueue            *queue;
  GPUBuffer           *shared = NULL, *readback = NULL;
  GPUCommandBuffer    *command;
  GPUTransferPassEncoder *pass;
  GPUFence            *fence;
  static uint8_t       got[TU_BUFFER];
  static uint8_t       rows[TU_ROW * TU_HEIGHT];
  uint32_t             value, i, w, started, y, x;
  bool                 ok;

  if (GPUCreateInstance(NULL, &instance) != GPU_OK || gpu_test_request_adapter(instance, &adapter) != GPU_OK
      || !adapter) {
    GPUDestroyInstance(instance);
    puts("threaded uploads: no adapter");
    return 77;
  }

  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;

  if (gpu_test_create_device(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    GPUDestroyInstance(instance);
    puts("threaded uploads: not offered");
    return 77;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  ok    = queue != NULL;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = TU_WIDTH;
  textureInfo.height           = TU_HEIGHT;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST | GPU_TEXTURE_USAGE_SAMPLED;

  memset(workers, 0, sizeof(workers));

  for (w = 0u; ok && w < TU_WORKERS; w++) {
    workers[w].queue = queue;
    workers[w].index = w;
    bufferInfo.sizeBytes = TU_BUFFER;
    bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
    ok = GPUCreateBuffer(device, &bufferInfo, &workers[w].buffer) == GPU_OK
         && GPUCreateTexture(device, &textureInfo, &workers[w].texture) == GPU_OK;
  }

  bufferInfo.sizeBytes = TU_MAIN_WRITES * sizeof(uint32_t);
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &shared) == GPU_OK;
  bufferInfo.sizeBytes = sizeof(rows);
  ok = ok && GPUCreateBuffer(device, &bufferInfo, &readback) == GPU_OK;

  if (!ok) {
    fprintf(stderr, "threaded uploads: setup failed\n");
    goto done;
  }

  /* the workers, and the main thread on the same queue meanwhile */

  tu_running = 1;

  for (started = 0u; started < TU_WORKERS && tu_start(&threads[started], &workers[started]); started++) {}

  for (i = 0u; i < TU_MAIN_WRITES; i++) {
    value = i * 2654435761u;

    if (GPUQueueWriteBuffer(queue, shared, (uint64_t)i * sizeof(value), &value, sizeof(value)) != GPU_OK) {
      fprintf(stderr, "threaded uploads: main queue write %u failed\n", i);
      ok = false;
      break;
    }

    if ((i & 15u) == 0u) {
      command = NULL;

      if (GPUAcquireCommandBuffer(queue, "threaded uploads main", &command) != GPU_OK || !command
          || tu_submit(queue, command, NULL) != GPU_OK) {
        fprintf(stderr, "threaded uploads: main submit %u failed\n", i);
        ok = false;
        break;
      }
    }
  }

  for (w = 0u; w < started; w++)
    tu_join(threads[w]);

  tu_running = 0;

  if (started != TU_WORKERS) {
    fprintf(stderr, "threaded uploads: %u of %u threads started\n", started, TU_WORKERS);
    ok = false;
  }

  for (w = 0u; w < TU_WORKERS; w++) {
    if (workers[w].failure) {
      fprintf(stderr, "threaded uploads: worker %u: %s\n", w, workers[w].failure);
      ok = false;
    }
  }

  /* every byte: each buffer as its last round left it, each band as its round wrote it */

  for (i = 0u; ok && i < TU_MAIN_WRITES; i++) {
    if (GPUQueueReadBuffer(queue, shared, (uint64_t)i * sizeof(value), &value, sizeof(value)) != GPU_OK
        || value != i * 2654435761u) {
      fprintf(stderr, "threaded uploads: main write %u reads %u\n", i, value);
      ok = false;
    }
  }

  for (w = 0u; ok && w < TU_WORKERS; w++) {
    GPUBufferTextureCopyRegion copy = {0};

    ok = GPUQueueReadBuffer(queue, workers[w].buffer, 0u, got, sizeof(got)) == GPU_OK;

    for (i = 0u; ok && i < TU_BUFFER; i++) {
      if (got[i] != tu_byte(w, TU_ROUNDS - 1u, i)) {
        fprintf(stderr, "threaded uploads: worker %u buffer byte %u is %u, expected %u\n", w, i, got[i],
                tu_byte(w, TU_ROUNDS - 1u, i));
        ok = false;
      }
    }

    command = NULL;
    fence   = NULL;
    ok      = ok && GPUAcquireCommandBuffer(queue, "threaded uploads readback", &command) == GPU_OK && command
              && (pass = GPUBeginTransferPass(command, "threaded uploads readback")) != NULL;

    if (!ok)
      break;

    copy.texture.width      = TU_WIDTH;
    copy.texture.height     = TU_HEIGHT;
    copy.texture.depth      = 1u;
    copy.texture.layerCount = 1u;
    copy.bytesPerRow        = TU_ROW;
    copy.rowsPerImage       = TU_HEIGHT;
    GPUCopyTextureToBuffer(pass, workers[w].texture, readback, &copy);
    GPUEndTransferPass(pass);

    ok = GPUCreateFence(device, NULL, &fence) == GPU_OK && tu_submit(queue, command, fence) == GPU_OK
         && GPUWaitFence(fence, UINT64_MAX) == GPU_OK
         && GPUQueueReadBuffer(queue, readback, 0u, rows, sizeof(rows)) == GPU_OK;
    GPUDestroyFence(fence);

    for (y = 0u; ok && y < TU_HEIGHT; y++) {
      for (x = 0u; ok && x < TU_WIDTH * 4u; x++) {
        uint8_t expected = tu_byte(w + 8u, y / TU_BAND, (y % TU_BAND) * TU_WIDTH * 4u + x);

        if (rows[y * TU_ROW + x] != expected) {
          fprintf(stderr, "threaded uploads: worker %u texture row %u byte %u is %u, expected %u\n", w, y, x,
                  rows[y * TU_ROW + x], expected);
          ok = false;
        }
      }
    }
  }

done:
  printf("threaded uploads (%u threads x %u rounds): %s\n", TU_WORKERS, TU_ROUNDS, ok ? "ok" : "FAILED");

  for (w = 0u; w < TU_WORKERS; w++) {
    GPUDestroyTexture(workers[w].texture);
    GPUDestroyBuffer(workers[w].buffer);
  }

  GPUDestroyBuffer(readback);
  GPUDestroyBuffer(shared);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);

  return ok ? 0 : 1;
}
