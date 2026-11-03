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
#import <objc/runtime.h>

enum {
  RESIDENCY_COMMANDS = 4,
  RESIDENCY_WIDTH    = 64,
  RESIDENCY_HEIGHT   = 4,
  RESIDENCY_LAYERS   = 4,
  RESIDENCY_ROW      = RESIDENCY_WIDTH * 4,
  RESIDENCY_BYTES    = RESIDENCY_ROW * RESIDENCY_HEIGHT * RESIDENCY_LAYERS
};

typedef struct ReleaseState {
  dispatch_semaphore_t signal;
  atomic_uint          count;
} ReleaseState;

typedef struct Resources {
  GPUTexture       *texture;
  GPUBuffer        *source;
  GPUBuffer        *readback;
  GPUCommandBuffer *command;
  MTCommandBuffer  *native;
} Resources;

@interface ReleaseObserver : NSObject {
@public
  ReleaseState *state;
}
@end

static ReleaseState kCompleted;
static ReleaseState kDiscarded;
static char         kReleaseKey;

@implementation ReleaseObserver
- (void)dealloc {
  atomic_fetch_add_explicit(&state->count, 1u, memory_order_release);
  dispatch_semaphore_signal(state->signal);
  [super dealloc];
}
@end

static void
observe_release(id allocation, ReleaseState *state) {
  ReleaseObserver *observer;

  observer = [ReleaseObserver new];
  observer->state = state;
  objc_setAssociatedObject(allocation, &kReleaseKey, observer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  [observer release];
}

static bool
wait_releases(ReleaseState *state, uint32_t expected) {
  dispatch_time_t deadline;

  deadline = dispatch_time(DISPATCH_TIME_NOW, INT64_C(1000000000));
  while (atomic_load_explicit(&state->count, memory_order_acquire) < expected) {
    if (dispatch_semaphore_wait(state->signal, deadline) != 0)
      return false;
  }
  return atomic_load_explicit(&state->count, memory_order_acquire) == expected;
}

static uint8_t
payload(uint32_t index, uint32_t epoch) {
  return (uint8_t)(index * 29u + (index >> 7u) * 13u + epoch * 37u);
}

static id<MTLTexture>
owned_texture(GPUTexture *texture) {
  GPUTextureMT *native;

  native = texture->_priv;
  return native->texture;
}

static bool
create_resources(GPUDevice *device, Resources *resources, ReleaseState *state, uint32_t epoch) {
  GPUTextureCreateInfo texture = {0};
  GPUBufferCreateInfo  buffer  = {0};
  uint8_t             *source;
  void                *readback;
  uint32_t             i;

  texture.dimension     = GPU_TEXTURE_DIMENSION_2D;
  texture.format        = GPU_FORMAT_RGBA8_UNORM;
  texture.width         = RESIDENCY_WIDTH;
  texture.height        = RESIDENCY_HEIGHT;
  texture.depthOrLayers = RESIDENCY_LAYERS;
  texture.mipLevelCount = 1u;
  texture.sampleCount   = 1u;
  texture.usage         = GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;
  buffer.sizeBytes      = RESIDENCY_BYTES;
  buffer.usage          = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &texture, &resources->texture) != GPU_OK
      || GPUCreateBuffer(device, &buffer, &resources->source) != GPU_OK
      || GPUCreateBuffer(device, &buffer, &resources->readback) != GPU_OK)
    return false;

  observe_release(owned_texture(resources->texture), state);
  observe_release((id<MTLBuffer>)resources->source->_priv, state);
  observe_release((id<MTLBuffer>)resources->readback->_priv, state);
  source   = GPUGetBufferHostAddress(resources->source);
  readback = GPUGetBufferHostAddress(resources->readback);
  if (!source || !readback)
    return false;

  for (i = 0u; i < RESIDENCY_BYTES; i++) {
    source[i] = payload(i, epoch);
  }
  memset(readback, 0xa5, RESIDENCY_BYTES);
  return true;
}

static bool
encode_roundtrip(GPUQueue *queue, Resources *resources) {
  GPUBufferTextureCopyRegion copy    = {0};
  GPUTextureBarrier          texture = {0};
  GPUBarrierBatch            barrier = {0};
  GPUTransferPassEncoder    *pass;

  if (GPUAcquireCommandBuffer(queue, "residency roundtrip", &resources->command) != GPU_OK)
    return false;

  resources->native = mt_commandBuffer(resources->command);
  if (!(pass = GPUBeginTransferPass(resources->command, "array write")))
    return false;

  copy.bytesPerRow        = RESIDENCY_ROW;
  copy.rowsPerImage       = RESIDENCY_HEIGHT;
  copy.texture.width      = RESIDENCY_WIDTH;
  copy.texture.height     = RESIDENCY_HEIGHT;
  copy.texture.depth      = 1u;
  copy.texture.layerCount = RESIDENCY_LAYERS;
  GPUCopyBufferToTexture(pass, resources->source, resources->texture, &copy);
  GPUEndTransferPass(pass);

  texture.texture    = resources->texture;
  texture.srcAccess  = GPU_ACCESS_TRANSFER_WRITE;
  texture.dstAccess  = GPU_ACCESS_TRANSFER_READ;
  texture.mipCount   = 1u;
  texture.layerCount = RESIDENCY_LAYERS;
  barrier.pTextureBarriers     = &texture;
  barrier.textureBarrierCount = 1u;
  barrier.srcStages            = GPU_STAGE_TRANSFER;
  barrier.dstStages            = GPU_STAGE_TRANSFER;
  GPUEncodeBarriers(resources->command, &barrier);
  if (!(pass = GPUBeginTransferPass(resources->command, "array readback")))
    return false;

  GPUCopyTextureToBuffer(pass, resources->texture, resources->readback, &copy);
  GPUEndTransferPass(pass);
  return true;
}

static void
finish_pending(GPUQueue *queue) {
  MTCommandQueue *native;

  native = mt_commandQueue(queue);
  if (dispatch_group_wait(native->inFlightGroup,
                          dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))) != 0) {
    fprintf(stderr, "residency: GPU completion timed out\n");
    abort();
  }
}

static void
destroy_resources(Resources *resources) {
  if (resources->command)
    GPUDiscardCommandBuffer(resources->command);

  GPUDestroyTexture(resources->texture);
  GPUDestroyBuffer(resources->source);
  GPUDestroyBuffer(resources->readback);
  memset(resources, 0, sizeof(*resources));
}

static bool
check_empty(GPUDevice *device) {
  GPUQueueSubmitInfo submit = {0};
  GPUCommandBuffer  *command = NULL;
  MTCommandBuffer   *native;
  GPUQueue          *queue;
  GPUFence          *fence = NULL;
  uint32_t           i;
  bool               correct = false;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK)
    goto cleanup;

  for (i = 0u; i < 2u; i++) {
    if (GPUAcquireCommandBuffer(queue, "empty residency", &command) != GPU_OK)
      goto cleanup;

    native = mt_commandBuffer(command);
    if (native->residencyAllocationCount != 0u)
      goto cleanup;

    if (i == 0u) {
      if (GPUDiscardCommandBuffer(command) != GPU_OK)
        goto cleanup;
    } else {
      submit.ppCommandBuffers   = &command;
      submit.commandBufferCount = 1u;
      submit.fence               = fence;
      if (GPUQueueSubmit(queue, &submit) != GPU_OK)
        goto cleanup;

      command = NULL;
      if (GPUWaitFence(fence, UINT64_C(5000000000)) != GPU_OK)
        goto cleanup;
      finish_pending(queue);
    }
    command = NULL;
    if (native->residencyAllocationCount != 0u)
      goto cleanup;
  }
  correct = true;

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  finish_pending(queue);
  GPUDestroyFence(fence);
  printf("residency: resource-free discard/submit=%s\n", correct ? "pass" : "fail");
  return correct;
}

static bool
check_completed(GPUDevice *device) {
  Resources                resources[RESIDENCY_COMMANDS] = {{0}};
  GPUCommandBuffer        *commands[RESIDENCY_COMMANDS];
  GPUQueueSubmitExInfo     submit = {0};
  GPUQueueSemaphoreWait    wait   = {0};
  GPUQueue                *queue;
  MTCommandQueue          *nativeQueue;
  MTCommandBuffer         *pooled;
  GPUFence                *fence = NULL;
  GPUSemaphore            *gate  = NULL;
  const uint8_t           *bytes;
  uint32_t                 i, j, pooledMask = 0u;
  bool                     correct = false, released;

  queue       = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  nativeQueue = mt_commandQueue(queue);
  @autoreleasepool {
    if (GPUCreateFence(device, NULL, &fence) != GPU_OK
        || GPUCreateSemaphore(device, NULL, &gate) != GPU_OK)
      goto cleanup;

    /* acquiring every command first prevents the pool from reusing one state. */
    for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
      if (!create_resources(device, &resources[i], &kCompleted, i)
          || !encode_roundtrip(queue, &resources[i]))
        goto cleanup;
      commands[i] = resources[i].command;
    }

    wait.semaphore  = gate;
    wait.value      = 1u;
    wait.waitStages = GPU_STAGE_TRANSFER;
    submit.ppCommandBuffers   = commands;
    submit.commandBufferCount = RESIDENCY_COMMANDS;
    submit.pWaits              = &wait;
    submit.waitCount           = 1u;
    submit.fence               = fence;
    if (GPUQueueSubmitEx(queue, &submit) != GPU_OK)
      goto cleanup;

    for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
      resources[i].command = NULL;
    }

    if (GPUWaitFence(fence, 0u) != GPU_ERROR_TIMEOUT
        || atomic_load_explicit(&kCompleted.count, memory_order_acquire) != 0u)
      goto cleanup;

#if MT_HAS_METAL4
    if (@available(macOS 26.0, iOS 26.0, *)) {
      if (nativeQueue->mode == MTCommandMode4) {
        for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
          if (![(id<MTLResidencySet>)resources[i].native->residency
                  containsAllocation:owned_texture(resources[i].texture)])
            goto cleanup;
        }
      }
    }
#endif

    [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];
    if (GPUWaitFence(fence, UINT64_C(5000000000)) != GPU_OK)
      goto cleanup;
    finish_pending(queue);

    correct = true;
    for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
      bytes = GPUGetBufferHostAddress(resources[i].readback);
      for (j = 0u; j < RESIDENCY_BYTES; j++) {
        if (bytes[j] != payload(j, i)) {
          fprintf(stderr, "residency readback %u:%u got=%u expected=%u\n",
                  i, j, bytes[j], payload(j, i));
          correct = false;
          goto cleanup;
        }
      }
    }

    os_unfair_lock_lock(&nativeQueue->poolLock);
    for (pooled = nativeQueue->freeCommands; pooled; pooled = pooled->poolNext) {
      for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
        if (pooled == resources[i].native)
          pooledMask |= 1u << i;
      }
    }
    os_unfair_lock_unlock(&nativeQueue->poolLock);
    correct = pooledMask == (1u << RESIDENCY_COMMANDS) - 1u;

cleanup:
    if (gate)
      [(id<MTLSharedEvent>)gate->_priv setSignaledValue:1u];
    finish_pending(queue);
    for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
      destroy_resources(&resources[i]);
    }
    GPUDestroySemaphore(gate);
    GPUDestroyFence(fence);
  }

  /* no command is acquired after completion, including for the CPU readback. */
  released = wait_releases(&kCompleted, RESIDENCY_COMMANDS * 3u);
  printf("residency: completed idle pool=%u exact bytes=%u native releases=%u/%u result=%s\n",
         RESIDENCY_COMMANDS, RESIDENCY_COMMANDS * RESIDENCY_BYTES,
         atomic_load_explicit(&kCompleted.count, memory_order_acquire), RESIDENCY_COMMANDS * 3u,
         correct && released ? "pass" : "fail");
  return correct && released;
}

static bool
check_discarded(GPUDevice *device) {
  Resources resources[RESIDENCY_COMMANDS] = {{0}};
  GPUQueue *queue;
  uint32_t  i;
  bool      correct = false, released;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  @autoreleasepool {
    for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
      if (!create_resources(device, &resources[i], &kDiscarded, i + 17u)
          || !encode_roundtrip(queue, &resources[i]))
        goto cleanup;
    }

    correct = true;
    for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
      if (GPUDiscardCommandBuffer(resources[i].command) != GPU_OK)
        correct = false;
      resources[i].command = NULL;
    }

cleanup:
    for (i = 0u; i < RESIDENCY_COMMANDS; i++) {
      destroy_resources(&resources[i]);
    }
  }

  released = wait_releases(&kDiscarded, RESIDENCY_COMMANDS * 3u);
  printf("residency: discarded unused pool=%u native releases=%u/%u result=%s\n",
         RESIDENCY_COMMANDS, atomic_load_explicit(&kDiscarded.count, memory_order_acquire),
         RESIDENCY_COMMANDS * 3u, correct && released ? "pass" : "fail");
  return correct && released;
}

int
main(void) {
  GPUAdapterProperties properties = {0};
  GPUInstance         *instance = NULL;
  GPUAdapter          *adapter;
  GPUDevice           *device = NULL;
  MTCommandQueue      *native;
  const char          *requested;
  GPUResult            result;
  int                  status = 1;
  bool                 empty, completed, discarded;

  requested = getenv("GPU_METAL_MODE");
  atomic_init(&kCompleted.count, 0u);
  atomic_init(&kDiscarded.count, 0u);
  kCompleted.signal = dispatch_semaphore_create(0);
  kDiscarded.signal = dispatch_semaphore_create(0);
  @autoreleasepool {
    if (GPUCreateInstance(NULL, &instance) != GPU_OK)
      goto cleanup;

    if (!(adapter = GPUGetAutoSelectedAdapter(instance))
        || GPUGetAdapterProperties(adapter, &properties) != GPU_OK
        || properties.backend != GPU_BACKEND_METAL)
      goto cleanup;

    result = GPUCreateDevice(adapter, NULL, &device);
    if (result != GPU_OK) {
      if (result == GPU_ERROR_UNSUPPORTED && requested && !strcmp(requested, "metal4"))
        status = 77;
      goto cleanup;
    }

    native = mt_commandQueue(GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u));
    if (!native)
      goto cleanup;

    printf("residency: requested=%s native=%s\n", requested ? requested : "auto",
           native->mode == MTCommandMode4 ? "metal4" : "classic");
    if (requested && !strcmp(requested, "metal4") && native->mode != MTCommandMode4) {
      status = 77;
      goto cleanup;
    }

    if (requested && !strcmp(requested, "classic") && native->mode != MTCommandModeClassic)
      goto cleanup;

    empty     = check_empty(device);
    completed = check_completed(device);
    discarded = check_discarded(device);
    status = empty && completed && discarded ? 0 : 1;

cleanup:
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
  }
  dispatch_release(kCompleted.signal);
  dispatch_release(kDiscarded.signal);
  return status;
}
