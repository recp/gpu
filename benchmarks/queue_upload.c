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
  QUEUE_UPLOAD_DEFAULT_WRITES = 1024,
  QUEUE_UPLOAD_DEFAULT_BYTES  = 4096
};

typedef struct QueueUploadConfig {
  GPUBackend backend;
  uint32_t   writeCount;
  uint32_t   bytesPerWrite;
} QueueUploadConfig;

typedef struct QueueUpload {
  GPUInstance     *instance;
  GPUAdapter      *adapter;
  GPUDevice       *device;
  GPUQueue        *queue;
  GPUBuffer       *buffer;
  GPUFence        *fence;
  void            *bytes;
} QueueUpload;

static bool
queue_uploadConfig(int argc, char *argv[], QueueUploadConfig *config) {
  if (!config || argc > 4) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s [default|metal|vulkan|dx12] [writes] [bytes]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->backend       = GPU_BACKEND_DEFAULT;
  config->writeCount    = QUEUE_UPLOAD_DEFAULT_WRITES;
  config->bytesPerWrite = QUEUE_UPLOAD_DEFAULT_BYTES;
  if ((argc > 1 && !bench_parseBackend(argv[1], &config->backend)) ||
      (argc > 2 && !bench_parseU32(argv[2], 1u, &config->writeCount)) ||
      (argc > 3 && !bench_parseU32(argv[3], 1u, &config->bytesPerWrite))) {
    fprintf(stderr, "invalid queue-upload benchmark arguments\n");
    return false;
  }
  return true;
}

static GPUAdapter *
queue_selectAdapter(GPUInstance *instance) {
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
queue_wait(QueueUpload *upload) {
  GPUCommandBuffer  *cmdb;
  GPUCommandBuffer  *buffers[1];
  GPUQueueSubmitInfo submitInfo;

  memset(&submitInfo, 0, sizeof(submitInfo));
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(upload->queue,
                              "queue-upload-wait",
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
queue_uploadInit(QueueUpload             *upload,
                 const QueueUploadConfig *config,
                 GPUAdapterProperties    *properties) {
  GPUInstanceCreateInfo instanceInfo;
  GPUBufferCreateInfo   bufferInfo;
  GPURuntimeConfig      runtimeInfo;

  memset(upload, 0, sizeof(*upload));
  memset(properties, 0, sizeof(*properties));
  memset(&instanceInfo, 0, sizeof(instanceInfo));
  memset(&bufferInfo, 0, sizeof(bufferInfo));
  memset(&runtimeInfo, 0, sizeof(runtimeInfo));

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = config->backend;
  if (GPUCreateInstance(&instanceInfo, &upload->instance) != GPU_OK ||
      !upload->instance) {
    return false;
  }

  upload->adapter = queue_selectAdapter(upload->instance);
  if (!upload->adapter) {
    return false;
  }
  upload->device  = GPUCreateDeviceWithDefaultQueues(upload->adapter);
  if (!upload->device) {
    return false;
  }
  upload->queue   = GPUGetQueue(upload->device, GPU_QUEUE_GRAPHICS, 0u);
  if (!upload->queue) {
    return false;
  }

  runtimeInfo.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeInfo.chain.structSize = sizeof(runtimeInfo);
  runtimeInfo.validationMode   = GPU_VALIDATION_OFF;
  runtimeInfo.enableStats      = true;
  bufferInfo.chain.sType       = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize  = sizeof(bufferInfo);
  bufferInfo.label             = "queue-upload-target";
  bufferInfo.sizeBytes         = config->bytesPerWrite;
  bufferInfo.usage             = GPU_BUFFER_USAGE_COPY_DST;
  upload->bytes                = malloc(config->bytesPerWrite);
  if (!upload->bytes ||
      GPUConfigureRuntime(upload->device, &runtimeInfo) != GPU_OK ||
      GPUGetAdapterProperties(upload->adapter, properties) != GPU_OK ||
      GPUCreateBuffer(upload->device, &bufferInfo, &upload->buffer) != GPU_OK ||
      !upload->buffer ||
      GPUCreateFence(upload->device, NULL, &upload->fence) != GPU_OK ||
      !upload->fence) {
    return false;
  }

  memset(upload->bytes, 0x5a, config->bytesPerWrite);
  return true;
}

static void
queue_uploadCleanup(QueueUpload *upload) {
  if (!upload) {
    return;
  }

  free(upload->bytes);
  GPUDestroyFence(upload->fence);
  GPUDestroyBuffer(upload->buffer);
  GPUDestroyDevice(upload->device);
  GPUDestroyInstance(upload->instance);
}

int
main(int argc, char *argv[]) {
  QueueUploadConfig    config;
  QueueUpload          upload;
  GPUAdapterProperties properties;
  GPUAllocatorStats    stats;
  uint64_t             totalBytes;
  double               elapsed;
  double               begin;
  bool                 drained;
  bool                 ok;

  memset(&upload, 0, sizeof(upload));
  memset(&properties, 0, sizeof(properties));
  memset(&stats, 0, sizeof(stats));
  if (!queue_uploadConfig(argc, argv, &config) ||
      !queue_uploadInit(&upload, &config, &properties)) {
    fprintf(stderr, "failed to initialize queue-upload benchmark\n");
    queue_uploadCleanup(&upload);
    return EXIT_FAILURE;
  }

  ok = GPUQueueWriteBuffer(upload.queue,
                           upload.buffer,
                           0u,
                           upload.bytes,
                           config.bytesPerWrite) == GPU_OK &&
       queue_wait(&upload);
  GPUResetStats(upload.device);
  begin = bench_now();
  for (uint32_t i = 0u; ok && i < config.writeCount; i++) {
    ok = GPUQueueWriteBuffer(upload.queue,
                             upload.buffer,
                             0u,
                             upload.bytes,
                             config.bytesPerWrite) == GPU_OK;
  }
  drained = queue_wait(&upload);
  ok      = ok && drained;
  elapsed = bench_now() - begin;
  ok      = ok && GPUGetAllocatorStats(upload.device, &stats) == GPU_OK;

  if (ok) {
    totalBytes = (uint64_t)config.writeCount * config.bytesPerWrite;
    printf("GPU queue-upload benchmark\n");
    printf("adapter: %s, backend: %s\n",
           properties.name ? properties.name : "unknown",
           bench_backendName(properties.backend));
    printf("writes: %u, bytes/write: %u, total: %.2f MiB\n",
           config.writeCount,
           config.bytesPerWrite,
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

  queue_uploadCleanup(&upload);
  if (!ok) {
    fprintf(stderr, "queue-upload benchmark failed\n");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
