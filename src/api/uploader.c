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

#include "../common.h"
#include "buffer_internal.h"
#include "cmdqueue_internal.h"
#include "format_internal.h"
#include "texture_internal.h"

#include <assert.h>

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

enum {
  UPLOAD_MAX_SLOTS = 8u,
  UPLOAD_ALIGNMENT = 256u
};

struct GPUUploaderReady {
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION   lock;
  CONDITION_VARIABLE condition;
  DWORD              callbackThread;
#else
  pthread_mutex_t mutex;
  pthread_cond_t  condition;
  pthread_t       callbackThread;
#endif
  GPUUploaderReadyFn callback;
  void              *user;
  GPUFence          *retired;
  uint32_t           references;
  bool               active;
};

typedef struct UploadSlot {
  GPUBuffer *buffer;
  GPUFence  *fence;
  GPUFence  *retired;
  uint8_t   *host;
  uint64_t   used;
  uint64_t   progress;
  bool       readyObserved;
  bool       waitForFence;
} UploadSlot;

struct GPUUploader {
  GPUQueue                 *queue;
  GPUCommandBuffer         *command;
  GPUTransferPassEncoder   *pass;
  const GPUCommandQueueApi *api;
  uint64_t                  capacity;
  uint32_t                  slotCount;
  uint32_t                  current;
  bool                      deferWhenBusy;
  UploadSlot                slots[UPLOAD_MAX_SLOTS];
};

static GPUResult
reuseSlot(const GPUUploader *uploader, UploadSlot *slot) {
  GPUResult result;

  if (slot->retired) {
    if (!GPUIsFenceSignaled(slot->retired))
      return GPU_ERROR_TIMEOUT;

    GPUDestroyFence(slot->retired);
    slot->retired = NULL;
  }

  if (uploader->deferWhenBusy) {
    if (!GPUIsFenceSignaled(slot->fence)) {
      if (slot->waitForFence || !slot->progress
          || uploader->api->uploadProgress(uploader->queue, true) < slot->progress)
        return GPU_ERROR_TIMEOUT;

      /* the native reads are done. keep this command's completion fence. */
      slot->retired = slot->fence;
      slot->fence   = NULL;
    }
  } else {
    result = GPUWaitFence(slot->fence, UINT64_MAX);
    if (result != GPU_OK)
      return result;
  }

  GPUDestroyFence(slot->fence);
  slot->fence         = NULL;
  slot->progress      = 0u;
  slot->readyObserved = false;
  slot->waitForFence  = false;
  return GPU_OK;
}

static GPUResult
beginUpload(GPUUploader *uploader, uint64_t sizeBytes, uint64_t *offset) {
  UploadSlot *slot;
  GPUResult   result;

  if (!sizeBytes || sizeBytes > uploader->capacity)
    return GPU_ERROR_INSUFFICIENT_CAPACITY;

  slot = &uploader->slots[uploader->current];
  *offset = (slot->used + UPLOAD_ALIGNMENT - 1u) & ~(uint64_t)(UPLOAD_ALIGNMENT - 1u);
  if (*offset > uploader->capacity || sizeBytes > uploader->capacity - *offset) {
    result = GPUFlushUploads(uploader, NULL);
    if (result != GPU_OK)
      return result;

    slot = &uploader->slots[uploader->current];
    *offset = 0u;
  }

  if (slot->fence) {
    result = reuseSlot(uploader, slot);
    if (result != GPU_OK)
      return result;

    slot->used = 0u;
    *offset    = 0u;
  }

  if (!uploader->command) {
    if (GPUAcquireCommandBuffer(uploader->queue, "uploader batch", &uploader->command) != GPU_OK)
      return GPU_ERROR_BACKEND_FAILURE;

    uploader->command->_orderedUploads = true;
  }

  if (!uploader->pass
      && !(uploader->pass = GPUBeginTransferPass(uploader->command, "uploader copies")))
    return GPU_ERROR_BACKEND_FAILURE;

  slot->used = *offset + sizeBytes;
  return GPU_OK;
}

static GPUResult
newUploadReady(GPUUploaderReadyFn callback, void *user, GPUUploaderReady **outReady) {
  GPUUploaderReady *ready;

  if (!(ready = calloc(1u, sizeof(*ready))))
    return GPU_ERROR_OUT_OF_MEMORY;

#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&ready->lock);
  InitializeConditionVariable(&ready->condition);
#else
  if (pthread_mutex_init(&ready->mutex, NULL) != 0) {
    free(ready);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (pthread_cond_init(&ready->condition, NULL) != 0) {
    pthread_mutex_destroy(&ready->mutex);
    free(ready);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif

  ready->callback   = callback;
  ready->user       = user;
  ready->references = 2u; /* caller and completion */
  *outReady         = ready;
  return GPU_OK;
}

static void
uploadReadyLock(GPUUploaderReady *ready) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&ready->lock);
#else
  pthread_mutex_lock(&ready->mutex);
#endif
}

static void
uploadReadyUnlock(GPUUploaderReady *ready) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&ready->lock);
#else
  pthread_mutex_unlock(&ready->mutex);
#endif
}

static void
uploadReadyRelease(GPUUploaderReady *ready) {
#if defined(_WIN32) || defined(WIN32)
  if (InterlockedDecrement((volatile LONG *)&ready->references) != 0)
    return;

  DeleteCriticalSection(&ready->lock);
#else
  if (__atomic_sub_fetch(&ready->references, 1u, __ATOMIC_ACQ_REL) != 0u)
    return;

  pthread_cond_destroy(&ready->condition);
  pthread_mutex_destroy(&ready->mutex);
#endif

  free(ready);
}

static void
deliverUploadReady(void *context) {
  GPUUploaderReadyFn callback;
  GPUUploaderReady  *ready = context;
  void              *user;

  uploadReadyLock(ready);
  callback = ready->callback;
  user     = ready->user;
  ready->callback = NULL;
  ready->user     = NULL;
  ready->active   = callback != NULL;
  if (callback) {
#if defined(_WIN32) || defined(WIN32)
    ready->callbackThread = GetCurrentThreadId();
#else
    ready->callbackThread = pthread_self();
#endif
  }
  uploadReadyUnlock(ready);

  if (callback) {
    callback(user);

    uploadReadyLock(ready);
    ready->active = false;
#if defined(_WIN32) || defined(WIN32)
    WakeAllConditionVariable(&ready->condition);
#else
    pthread_cond_broadcast(&ready->condition);
#endif
    uploadReadyUnlock(ready);
  }

  uploadReadyRelease(ready);
}

static void
observeRetiredSlot(void *context) {
  GPUUploaderReady *ready = context;
  GPUFence         *retired;
  GPUResult         result;

  retired        = ready->retired;
  ready->retired = NULL;
  result         = observeFenceOnce(retired, deliverUploadReady, ready);

  /* a slot with a full-fence observer cannot retire before that fence signals */
  assert(result == GPU_OK);
  if (result != GPU_OK)
    uploadReadyRelease(ready);

  GPUDestroyFence(retired);
}

static GPUResult
observeUploadSlot(GPUUploader *uploader, UploadSlot *slot, GPUUploaderReadyFn callback, void *user) {
  GPUResult result;

  if (slot->progress && uploader->api->observeUploadProgress && !GPUIsFenceSignaled(slot->fence)) {
    result = uploader->api->observeUploadProgress(uploader->queue, slot->progress, slot->fence, callback, user);
    if (result == GPU_OK)
      return GPU_OK;
  }

  /* a full-fence observer must drain before this fence can be retired */
  result = observeFenceOnce(slot->fence, callback, user);
  if (result == GPU_OK)
    slot->waitForFence = true;

  return result;
}

GPU_EXPORT
GPUResult
GPUCreateUploader(GPUQueue *queue, const GPUUploaderCreateInfo *info, GPUUploader **outUploader) {
  GPUBufferCreateInfo       bufferInfo = {0};
  const GPUUploaderOptions *options;
  GPUUploader        *uploader;
  GPUApi             *api;
  uint64_t            capacity;
  uint32_t            count, i;
  GPUResult           result;

  if (outUploader)
    *outUploader = NULL;

  if (!queue || !info || !outUploader
      || info->chain.sType != GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO
      || info->chain.structSize < sizeof(*info)
      || info->stagingBufferCount > UPLOAD_MAX_SLOTS)
    return GPU_ERROR_INVALID_ARGUMENT;

  options = info->chain.pNext;
  if (options) {
    if (options->chain.sType != GPU_STRUCTURE_TYPE_UPLOADER_OPTIONS)
      return GPU_ERROR_UNSUPPORTED;

    if (options->chain.structSize < sizeof(*options) || options->chain.pNext)
      return GPU_ERROR_INVALID_ARGUMENT;
  }

  api = commandQueueApi(queue);
  if (!GPUIsFeatureEnabled(queue->_device, GPU_FEATURE_THREADED_UPLOADS)
      || !api || !api->cmdque.lock || !api->cmdque.unlock
      || !api->cmdque.commandBufferOnComplete || !api->cmdque.commit
      || !api->renderPass.copyBufferToBuffer || !api->renderPass.copyBufferToTexture)
    return GPU_ERROR_UNSUPPORTED;

  capacity = info->stagingSizeBytes ? info->stagingSizeBytes : 4u * 1024u * 1024u;
  count    = info->stagingBufferCount ? info->stagingBufferCount : 3u;
  if (capacity > SIZE_MAX - UPLOAD_ALIGNMENT)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!(uploader = calloc(1u, sizeof(*uploader))))
    return GPU_ERROR_OUT_OF_MEMORY;

  uploader->queue         = queue;
  uploader->api           = &api->cmdque;
  uploader->capacity      = capacity;
  uploader->slotCount     = count;
  uploader->deferWhenBusy  = options && options->deferWhenBusy;

  bufferInfo.label     = info->label;
  bufferInfo.sizeBytes = capacity;
  bufferInfo.usage     = GPU_BUFFER_USAGE_COPY_SRC;
  for (i = 0u; i < count; i++) {
    result = GPUCreateBuffer(queue->_device, &bufferInfo, &uploader->slots[i].buffer);
    if (result != GPU_OK)
      goto fail;

    if (!(uploader->slots[i].host = GPUGetBufferHostAddress(uploader->slots[i].buffer))) {
      result = GPU_ERROR_UNSUPPORTED;
      goto fail;
    }
  }

  *outUploader = uploader;
  return GPU_OK;

fail:
  GPUDestroyUploader(uploader);
  return result;
}

GPU_EXPORT
GPUResult
GPUUploadBuffer(GPUUploader *uploader, GPUBuffer *buffer, uint64_t offset, const void *data, uint64_t sizeBytes) {
  GPUBufferCopyRegion copy;
  UploadSlot         *slot;
  uint64_t            stagingOffset;
  GPUResult           result;

  if (!uploader || !data || !buffer || buffer->device != uploader->queue->_device
      || !bufferHasUsage(buffer, GPU_BUFFER_USAGE_COPY_DST) || !bufferRangeValid(buffer, offset, sizeBytes))
    return GPU_ERROR_INVALID_ARGUMENT;

  result = beginUpload(uploader, sizeBytes, &stagingOffset);
  if (result != GPU_OK)
    return result;

  slot = &uploader->slots[uploader->current];
  memcpy(slot->host + stagingOffset, data, (size_t)sizeBytes);
  copy.srcOffset = stagingOffset;
  copy.dstOffset = offset;
  copy.sizeBytes = sizeBytes;
  GPUCopyBufferToBuffer(uploader->pass, slot->buffer, buffer, &copy);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUUploadTexture(GPUUploader *uploader, GPUTexture *texture, const GPUTextureWriteRegion *region,
                  const void *data, uint64_t sizeBytes) {
  GPUBufferTextureCopyRegion copy = {0};
  GPUFormatDataLayout        source, target;
  GPUFormatLayout            format;
  UploadSlot                *slot;
  const uint8_t             *bytes;
  uint8_t                   *destination;
  uint64_t                   offset, image, images;
  uint32_t                   pitch, row;
  GPUResult                  result;

  if (!uploader || !data || !textureWriteRegionValid(texture, region, sizeBytes, &source)
      || texture->device != uploader->queue->_device
      || !formatAspectLayout(texture->format, region->aspect, &format))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (source.bytesInLastRow > UINT32_MAX - (UPLOAD_ALIGNMENT - 1u))
    return GPU_ERROR_INSUFFICIENT_CAPACITY;

  pitch = (source.bytesInLastRow + UPLOAD_ALIGNMENT - 1u) & ~(uint32_t)(UPLOAD_ALIGNMENT - 1u);
  if (!formatAspectDataLayout(texture->format, region->aspect, region->width, region->height,
                              region->depth, region->layerCount, pitch, source.blockRows * format.blockHeight, &target))
    return GPU_ERROR_INVALID_ARGUMENT;

  result = beginUpload(uploader, target.requiredBytes, &offset);
  if (result != GPU_OK)
    return result;

  slot        = &uploader->slots[uploader->current];
  bytes       = data;
  destination = slot->host + offset;
  images      = (uint64_t)region->depth * region->layerCount;
  for (image = 0u; image < images; image++) {
    for (row = 0u; row < source.blockRows; row++) {
      memcpy(destination + image * target.bytesPerImage + (uint64_t)row * pitch,
             bytes + image * source.bytesPerImage + (uint64_t)row * region->bytesPerRow,
             source.bytesInLastRow);
    }
  }

  copy.texture.texture.aspect         = region->aspect;
  copy.texture.texture.x              = region->x;
  copy.texture.texture.y              = region->y;
  copy.texture.texture.z              = region->z;
  copy.texture.texture.mipLevel       = region->mipLevel;
  copy.texture.texture.baseArrayLayer = region->baseArrayLayer;
  copy.texture.width                  = region->width;
  copy.texture.height                 = region->height;
  copy.texture.depth                  = region->depth;
  copy.texture.layerCount             = region->layerCount;
  copy.bufferOffset = offset;
  copy.bytesPerRow   = pitch;
  copy.rowsPerImage  = source.blockRows * format.blockHeight;
  GPUCopyBufferToTexture(uploader->pass, slot->buffer, texture, &copy);
  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUFlushUploads(GPUUploader *uploader, GPUFence **outFence) {
  GPUFenceCreateInfo emptyInfo = {0};
  GPUCommandBuffer *command;
  UploadSlot       *slot;
  GPUFence         *fence;
  GPUResult        result;

  if (outFence)
    *outFence = NULL;

  if (!uploader)
    return GPU_ERROR_INVALID_ARGUMENT;

  if (!uploader->command) {
    slot = &uploader->slots[(uploader->current + uploader->slotCount - 1u) % uploader->slotCount];
    fence = slot->fence ? slot->fence : slot->retired;
    if (fence) {
      if (outFence) {
        retainFence(fence);
        *outFence = fence;
      }
      return GPU_OK;
    }

    if (!outFence)
      return GPU_OK;

    emptyInfo.signaled = true;
    return GPUCreateFence(uploader->queue->_device, &emptyInfo, outFence);
  }

  slot = &uploader->slots[uploader->current];
  if (slot->fence) {
    result = reuseSlot(uploader, slot);
    if (result != GPU_OK)
      return result;
  }

  if (uploader->pass) {
    GPUEndTransferPass(uploader->pass);
    uploader->pass = NULL;
  }

  result = GPUCreateFence(uploader->queue->_device, NULL, &fence);
  if (result != GPU_OK)
    return result;

  command           = uploader->command;
  uploader->command = NULL;
  slot->fence        = fence;
  result = submitUploadCommandBuffer(command, fence, uploader->deferWhenBusy ? &slot->progress : NULL);
  if (result == GPU_OK && outFence) {
    retainFence(fence);
    *outFence = fence;
  }

  uploader->current = (uploader->current + 1u) % uploader->slotCount;
  uploader->slots[uploader->current].used = 0u;
  return result;
}

GPU_EXPORT
GPUResult
GPUUploaderNotifyReady(GPUUploader        *uploader,
                       GPUUploaderReadyFn  callback,
                       void               *user,
                       GPUUploaderReady  **outReady) {
  GPUUploaderReadyFn notify;
  GPUUploaderReady  *ready;
  UploadSlot        *slot;
  GPUResult          result;

  if (outReady)
    *outReady = NULL;

  if (!uploader || !callback || !outReady || !uploader->deferWhenBusy)
    return GPU_ERROR_INVALID_ARGUMENT;

  slot = &uploader->slots[uploader->current];
  if (slot->fence && slot->readyObserved)
    return GPU_ERROR_INVALID_ARGUMENT;

  result = newUploadReady(callback, user, &ready);
  if (result != GPU_OK)
    return result;

  if (!slot->fence) {
    deliverUploadReady(ready);
    *outReady = ready;
    return GPU_OK;
  }

  notify = deliverUploadReady;
  if (slot->retired && !GPUIsFenceSignaled(slot->retired)) {
    /* observe both reuse conditions without borrowing uploader state in a callback */
    retainFence(slot->retired);
    ready->retired = slot->retired;
    notify         = observeRetiredSlot;
  }

  result = observeUploadSlot(uploader, slot, notify, ready);
  if (result != GPU_OK) {
    GPUDestroyFence(ready->retired);
    uploadReadyRelease(ready);
    uploadReadyRelease(ready);
    return result;
  }

  slot->readyObserved = true;
  *outReady           = ready;
  return GPU_OK;
}

GPU_EXPORT
void
GPUDestroyUploaderReady(GPUUploaderReady *ready) {
  if (!ready)
    return;

  uploadReadyLock(ready);
  ready->callback = NULL;
  ready->user     = NULL;

#if defined(_WIN32) || defined(WIN32)
  while (ready->active && ready->callbackThread != GetCurrentThreadId())
    SleepConditionVariableCS(&ready->condition, &ready->lock, INFINITE);
#else
  while (ready->active && !pthread_equal(ready->callbackThread, pthread_self()))
    pthread_cond_wait(&ready->condition, &ready->mutex);
#endif

  uploadReadyUnlock(ready);
  uploadReadyRelease(ready);
}

GPU_EXPORT
void
GPUDestroyUploader(GPUUploader *uploader) {
  uint32_t i;

  if (!uploader)
    return;

  if (uploader->command && GPUFlushUploads(uploader, NULL) != GPU_OK && uploader->command) {
    if (uploader->pass)
      GPUEndTransferPass(uploader->pass);

    GPUDiscardCommandBuffer(uploader->command);
  }

  for (i = 0u; i < uploader->slotCount; i++) {
    if (uploader->slots[i].fence) {
      GPUWaitFence(uploader->slots[i].fence, UINT64_MAX);
      GPUDestroyFence(uploader->slots[i].fence);
    }

    if (uploader->slots[i].retired) {
      GPUWaitFence(uploader->slots[i].retired, UINT64_MAX);
      GPUDestroyFence(uploader->slots[i].retired);
    }

    GPUDestroyBuffer(uploader->slots[i].buffer);
  }

  free(uploader);
}
