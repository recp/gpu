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

#ifndef gpu_uploader_h
#define gpu_uploader_h
#ifdef __cplusplus
extern "C" {
#endif

#include "texture.h"
#include "buffer.h"

typedef struct GPUUploader GPUUploader;

typedef struct GPUUploaderCreateInfo {
  GPUChainedStruct chain;
  const char      *label;
  uint64_t         stagingSizeBytes;   /* per slot; zero selects 4 MiB */
  uint32_t         stagingBufferCount; /* zero selects three; at most eight */
} GPUUploaderCreateInfo;

/* one calling thread per uploader; different uploaders may share a queue.
 * enable GPU_FEATURE_THREADED_UPLOADS. the device/queue and destinations outlive
 * pending work. source bytes are copied before upload returns. full rings flush
 * and wait for their oldest slot; a single oversized upload returns insufficient
 * capacity. host writes and destination destruction still require completion.
 */
GPU_EXPORT
GPUResult
GPUCreateUploader(GPUQueue *queue, const GPUUploaderCreateInfo *info, GPUUploader **outUploader);

GPU_EXPORT
GPUResult
GPUUploadTexture(GPUUploader *uploader, GPUTexture *texture, const GPUTextureWriteRegion *region,
                  const void *data, uint64_t sizeBytes);

GPU_EXPORT
GPUResult
GPUUploadBuffer(GPUUploader *uploader, GPUBuffer *buffer, uint64_t offset, const void *data, uint64_t sizeBytes);

/* publishes preceding uploads before later submits on this queue. concurrent
 * callers must coordinate which flush precedes a consuming submit. the returned
 * fence is caller-owned, remains valid across ring reuse, and must not be reset
 * until signaled. NULL outFence is allowed. an empty flush submits no work;
 * its fence covers this uploader's last submitted batch, or is already signaled
 * when the uploader has never submitted work. each returned reference is owned.
 */
GPU_EXPORT
GPUResult
GPUFlushUploads(GPUUploader *uploader, GPUFence **outFence);

/* flushes pending uploads and waits for owned slots; may block. */
GPU_EXPORT
void
GPUDestroyUploader(GPUUploader *uploader);

#ifdef __cplusplus
}
#endif
#endif /* gpu_uploader_h */
