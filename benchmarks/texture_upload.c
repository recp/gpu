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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  TEXTURE_UPLOAD_DEFAULT_WRITES = 1024,
  TEXTURE_UPLOAD_DEFAULT_WIDTH  = 64,
  TEXTURE_UPLOAD_DEFAULT_HEIGHT = 64,
  TEXTURE_UPLOAD_ROW_ALIGNMENT  = 256
};

typedef struct TextureUploadConfig {
  GPUBackend backend;
  uint32_t   writeCount;
  uint32_t   width;
  uint32_t   height;
} TextureUploadConfig;

typedef struct TextureUpload {
  GPUInstance     *instance;
  GPUAdapter      *adapter;
  GPUDevice       *device;
  GPUCommandQueue *queue;
  GPUTexture      *texture;
  GPUFence        *fence;
  void            *bytes;
  uint64_t         bytesPerWrite;
  uint32_t         bytesPerRow;
} TextureUpload;

static bool
texture_uploadConfig(int argc, char *argv[], TextureUploadConfig *config) {
  if (!config || argc > 5) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s [default|metal|vulkan|dx12] [writes] "
              "[width] [height]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->backend    = GPU_BACKEND_DEFAULT;
  config->writeCount = TEXTURE_UPLOAD_DEFAULT_WRITES;
  config->width      = TEXTURE_UPLOAD_DEFAULT_WIDTH;
  config->height     = TEXTURE_UPLOAD_DEFAULT_HEIGHT;
  if ((argc > 1 && !bench_parseBackend(argv[1], &config->backend)) ||
      (argc > 2 && !bench_parseU32(argv[2], 1u, &config->writeCount)) ||
      (argc > 3 && !bench_parseU32(argv[3], 1u, &config->width)) ||
      (argc > 4 && !bench_parseU32(argv[4], 1u, &config->height))) {
    fprintf(stderr, "invalid texture-upload benchmark arguments\n");
    return false;
  }
  return true;
}

static GPUAdapter *
texture_selectAdapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  GPUResult   result;
  uint32_t    count;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    return NULL;
  }
  return adapter;
}

static bool
texture_wait(TextureUpload *upload) {
  GPUCommandBuffer  *cmdb;
  GPUCommandBuffer  *buffers[1];
  GPUQueueSubmitInfo submitInfo;

  memset(&submitInfo, 0, sizeof(submitInfo));
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(upload->queue,
                              "texture-upload-wait",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    return false;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = upload->fence;
  return GPUQueueSubmit(upload->queue, &submitInfo) == GPU_OK &&
         GPUWaitFence(upload->fence, UINT64_MAX) == GPU_OK;
}

static bool
texture_uploadInit(TextureUpload             *upload,
                   const TextureUploadConfig *config,
                   GPUAdapterProperties      *properties) {
  GPUInstanceCreateInfo instanceInfo;
  GPUTextureCreateInfo  textureInfo;
  GPURuntimeConfig      runtimeInfo;
  uint64_t              rowBytes;

  memset(upload, 0, sizeof(*upload));
  memset(properties, 0, sizeof(*properties));
  memset(&instanceInfo, 0, sizeof(instanceInfo));
  memset(&textureInfo, 0, sizeof(textureInfo));
  memset(&runtimeInfo, 0, sizeof(runtimeInfo));

  rowBytes = (uint64_t)config->width * sizeof(float);
  if (rowBytes > UINT32_MAX - (TEXTURE_UPLOAD_ROW_ALIGNMENT - 1u)) {
    return false;
  }
  upload->bytesPerRow = (uint32_t)(
    (rowBytes + TEXTURE_UPLOAD_ROW_ALIGNMENT - 1u) &
    ~(uint64_t)(TEXTURE_UPLOAD_ROW_ALIGNMENT - 1u)
  );
  if (config->height > UINT64_MAX / upload->bytesPerRow) {
    return false;
  }
  upload->bytesPerWrite = (uint64_t)upload->bytesPerRow * config->height;
  if (upload->bytesPerWrite > SIZE_MAX) {
    return false;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = config->backend;
  if (GPUCreateInstance(&instanceInfo, &upload->instance) != GPU_OK ||
      !upload->instance) {
    return false;
  }

  upload->adapter = texture_selectAdapter(upload->instance);
  if (!upload->adapter) {
    return false;
  }
  upload->device = GPUCreateDeviceWithDefaultQueues(upload->adapter);
  if (!upload->device) {
    return false;
  }
  upload->queue = GPUGetQueue(upload->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!upload->queue) {
    return false;
  }

  runtimeInfo.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeInfo.chain.structSize = sizeof(runtimeInfo);
  runtimeInfo.validationMode   = GPU_VALIDATION_OFF;
  runtimeInfo.enableStats      = true;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "texture-upload-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT_STENCIL8;
  textureInfo.width            = config->width;
  textureInfo.height           = config->height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COPY_DST;
  upload->bytes                = malloc((size_t)upload->bytesPerWrite);
  if (!upload->bytes ||
      GPUConfigureRuntime(upload->device, &runtimeInfo) != GPU_OK ||
      GPUGetAdapterProperties(upload->adapter, properties) != GPU_OK ||
      GPUCreateTexture(upload->device,
                       &textureInfo,
                       &upload->texture) != GPU_OK ||
      !upload->texture ||
      GPUCreateFence(upload->device, NULL, &upload->fence) != GPU_OK ||
      !upload->fence) {
    return false;
  }

  memset(upload->bytes, 0x3f, (size_t)upload->bytesPerWrite);
  return true;
}

static void
texture_uploadCleanup(TextureUpload *upload) {
  if (!upload) {
    return;
  }

  free(upload->bytes);
  GPUDestroyFence(upload->fence);
  GPUDestroyTexture(upload->texture);
  GPUDestroyDevice(upload->device);
  GPUDestroyInstance(upload->instance);
}

static GPUResult
texture_write(TextureUpload *upload, const TextureUploadConfig *config) {
  GPUTextureWriteRegion region;

  memset(&region, 0, sizeof(region));
  region.width        = config->width;
  region.height       = config->height;
  region.depth        = 1u;
  region.layerCount   = 1u;
  region.bytesPerRow  = upload->bytesPerRow;
  region.rowsPerImage = config->height;
  region.aspect       = GPU_TEXTURE_ASPECT_DEPTH_ONLY;
  return GPUQueueWriteTexture(upload->queue,
                              upload->texture,
                              &region,
                              upload->bytes,
                              upload->bytesPerWrite);
}

int
main(int argc, char *argv[]) {
  TextureUploadConfig config;
  TextureUpload       upload;
  GPUAdapterProperties properties;
  GPUAllocatorStats   stats;
  uint64_t            totalBytes;
  double              elapsed;
  double              begin;
  bool                drained;
  bool                ok;

  memset(&upload, 0, sizeof(upload));
  memset(&properties, 0, sizeof(properties));
  memset(&stats, 0, sizeof(stats));
  if (!texture_uploadConfig(argc, argv, &config) ||
      !texture_uploadInit(&upload, &config, &properties)) {
    fprintf(stderr, "failed to initialize texture-upload benchmark\n");
    texture_uploadCleanup(&upload);
    return EXIT_FAILURE;
  }

  ok = texture_write(&upload, &config) == GPU_OK && texture_wait(&upload);
  GPUResetStats(upload.device);
  begin = bench_now();
  for (uint32_t i = 0u; ok && i < config.writeCount; i++) {
    ok = texture_write(&upload, &config) == GPU_OK;
  }
  drained = texture_wait(&upload);
  ok      = ok && drained;
  elapsed = bench_now() - begin;
  ok      = ok && GPUGetAllocatorStats(upload.device, &stats) == GPU_OK;

  if (ok) {
    totalBytes = (uint64_t)config.writeCount * upload.bytesPerWrite;
    printf("GPU texture-upload benchmark\n");
    printf("adapter: %s, backend: %s\n",
           properties.name ? properties.name : "unknown",
           bench_backendName(properties.backend));
    printf("writes: %u, extent: %ux%u, bytes/write: %.2f KiB, "
           "total: %.2f MiB\n",
           config.writeCount,
           config.width,
           config.height,
           (double)upload.bytesPerWrite / 1024.0,
           (double)totalBytes / (1024.0 * 1024.0));
    printf("elapsed: %.3f ms, throughput: %.2f MiB/s\n",
           elapsed * 1e3,
           elapsed > 0.0
             ? (double)totalBytes / (1024.0 * 1024.0) / elapsed
             : 0.0);
    printf("upload stalls: %" PRIu64 " (%.2f%% of writes)\n",
           stats.uploadStallCount,
           (double)stats.uploadStallCount * 100.0 / config.writeCount);
  }

  texture_uploadCleanup(&upload);
  if (!ok) {
    fprintf(stderr, "texture-upload benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
