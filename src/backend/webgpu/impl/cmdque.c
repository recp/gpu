/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static void
webgpu_recycleCommand(GPUCommandBuffer *cmdb) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command) {
    return;
  }
  memset(&command->command, 0, sizeof(command->command));
  command->command._priv = command;
  command->present       = NULL;
  atomic_store_explicit(&command->inUse, false, memory_order_release);
}

static void
webgpu_commandDone(WGPUQueueWorkDoneStatus status,
                   WGPUStringView          message,
                   void                   *userData,
                   void                   *unused) {
  GPUCommandWebGPU *command;

  GPU__UNUSED(status);
  GPU__UNUSED(message);
  GPU__UNUSED(unused);
  command = userData;
  if (command->submitted) {
    wgpuCommandBufferRelease(command->submitted);
    command->submitted = NULL;
  }
  gpuFinishCommandBuffer(&command->command, webgpu_recycleCommand);
}

static GPUQueue *
webgpu_getCommandQueue(GPUDevice *device,
                       GPUQueueFlagBits bits,
                       uint32_t index) {
  GPUDeviceWebGPU *native;

  native = gpu_webgpuDevice(device);
  if (!native || index != 0u || bits == 0u ||
      (bits & ~native->queueHandle.bits) != 0u) {
    return NULL;
  }
  return &native->queueHandle;
}

static GPUCommandBuffer *
webgpu_newCommandBuffer(GPUQueue                    *queue,
                        const char                  *label,
                        void                        *sender,
                        GPUCommandBufferCompletionFn onComplete) {
  WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  GPUDeviceWebGPU             *device;
  GPUCommandWebGPU            *command;

  device = gpu_webgpuDevice(queue ? queue->_device : NULL);
  if (!device) {
    return NULL;
  }

  command = NULL;
  for (uint32_t i = 0u; i < GPU_WEBGPU_COMMAND_SLOT_COUNT; i++) {
    bool expected;

    expected = false;
    if (atomic_compare_exchange_strong_explicit(&device->commands[i].inUse,
                                                &expected,
                                                true,
                                                memory_order_acq_rel,
                                                memory_order_acquire)) {
      command = &device->commands[i];
      break;
    }
  }
  if (!command) {
    return NULL;
  }

  memset(&command->command, 0, sizeof(command->command));
  command->command._priv             = command;
  command->command._queue            = queue;
  command->command._onCompleteSender = sender;
  command->command._onComplete       = onComplete;
  command->present                   = NULL;
  descriptor.label                   = gpu_webgpuString(label);
  command->encoder = wgpuDeviceCreateCommandEncoder(device->device,
                                                     &descriptor);
  if (!command->encoder) {
    webgpu_recycleCommand(&command->command);
    return NULL;
  }
  return &command->command;
}

static void
webgpu_commandBufferOnComplete(GPUCommandBuffer            *cmdb,
                               void                        *sender,
                               GPUCommandBufferCompletionFn onComplete) {
  cmdb->_onCompleteSender = sender;
  cmdb->_onComplete       = onComplete;
}

static GPUResult
webgpu_discard(GPUCommandBuffer *cmdb) {
  GPUCommandWebGPU *command;

  command = gpu_webgpuCommand(cmdb);
  if (!command) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  if (command->encoder) {
    wgpuCommandEncoderRelease(command->encoder);
    command->encoder = NULL;
  }
  gpuDiscardCommandBufferState(cmdb, webgpu_recycleCommand);
  return GPU_OK;
}

static GPUResult
webgpu_commit(GPUCommandBuffer *cmdb) {
  WGPUCommandBufferDescriptor finishInfo = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  WGPUQueueWorkDoneCallbackInfo callbackInfo =
    WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
  GPUCommandWebGPU *command;
  GPUDeviceWebGPU  *device;

  command = gpu_webgpuCommand(cmdb);
  device  = gpu_webgpuDevice(gpuCommandBufferDevice(cmdb));
  if (!command || !device || !command->encoder) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  command->submitted = wgpuCommandEncoderFinish(command->encoder, &finishInfo);
  wgpuCommandEncoderRelease(command->encoder);
  command->encoder = NULL;
  if (!command->submitted) {
    gpuFinishCommandBuffer(cmdb, webgpu_recycleCommand);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  wgpuQueueSubmit(device->queue, 1u, &command->submitted);
  if (command->present) {
#if !defined(__EMSCRIPTEN__)
    wgpuSurfacePresent(command->present->surface);
#endif
    command->present = NULL;
  }

  callbackInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
  callbackInfo.callback  = webgpu_commandDone;
  callbackInfo.userdata1 = command;
  wgpuQueueOnSubmittedWorkDone(device->queue, callbackInfo);
  return GPU_OK;
}

static bool
webgpu_presentDrawable(GPUCommandBuffer *cmdb, GPUFrame *frame) {
  GPUCommandWebGPU   *command;
  GPUSwapchainWebGPU *swapchain;

  command   = gpu_webgpuCommand(cmdb);
  swapchain = frame ? frame->_priv : NULL;
  if (!command || !swapchain || !swapchain->acquired || command->present) {
    return false;
  }
  command->present = swapchain;
  return true;
}

void
webgpu_initCommandQueue(GPUApiCommandQueue *api) {
  api->getCommandQueue         = webgpu_getCommandQueue;
  api->newCommandBuffer        = webgpu_newCommandBuffer;
  api->commandBufferOnComplete = webgpu_commandBufferOnComplete;
  api->discard                 = webgpu_discard;
  api->commit                  = webgpu_commit;
}

void
webgpu_initCommandBuffer(GPUApiCommandBuffer *api) {
  api->presentDrawable = webgpu_presentDrawable;
}
