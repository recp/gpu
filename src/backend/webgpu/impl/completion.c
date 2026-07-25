/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

#if GPU_WEBGPU_PROVIDER_WGPU_NATIVE
static void
webgpu_completionLock(GPUDeviceWebGPU *device) {
#  if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&device->completionLock);
#  else
  pthread_mutex_lock(&device->completionLock);
#  endif
}

static void
webgpu_completionUnlock(GPUDeviceWebGPU *device) {
#  if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&device->completionLock);
#  else
  pthread_mutex_unlock(&device->completionLock);
#  endif
}

static void
webgpu_completionSignal(GPUDeviceWebGPU *device) {
#  if defined(_WIN32) || defined(WIN32)
  WakeConditionVariable(&device->completionCondition);
#  else
  pthread_cond_signal(&device->completionCondition);
#  endif
}

static void
webgpu_completionWait(GPUDeviceWebGPU *device) {
#  if defined(_WIN32) || defined(WIN32)
  SleepConditionVariableCS(&device->completionCondition,
                           &device->completionLock,
                           INFINITE);
#  else
  pthread_cond_wait(&device->completionCondition, &device->completionLock);
#  endif
}

static void
webgpu_completionLoop(GPUDeviceWebGPU *device) {
  for (;;) {
    WGPUSubmissionIndex submission;

    webgpu_completionLock(device);
    while (!device->stoppingCompletionWorker &&
           device->completionCount == 0u) {
      webgpu_completionWait(device);
    }
    if (device->stoppingCompletionWorker &&
        device->completionCount == 0u) {
      webgpu_completionUnlock(device);
      break;
    }
    submission = device->completionSubmissions[device->completionHead];
    device->completionHead =
      (device->completionHead + 1u) % GPU_WEBGPU_COMMAND_SLOT_COUNT;
    device->completionCount--;
    webgpu_completionUnlock(device);

    wgpuDevicePoll(device->device, true, &submission);
  }
}

#  if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
webgpu_completionMain(LPVOID context) {
  webgpu_completionLoop(context);
  return 0;
}
#  else
static void *
webgpu_completionMain(void *context) {
  webgpu_completionLoop(context);
  return NULL;
}
#  endif

bool
gpu_webgpuStartCompletionWorker(GPUDeviceWebGPU *device) {
  if (!device || !device->device) {
    return false;
  }

#  if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&device->completionLock);
  InitializeConditionVariable(&device->completionCondition);
  device->completionWorker = CreateThread(NULL,
                                          0,
                                          webgpu_completionMain,
                                          device,
                                          0,
                                          NULL);
  device->completionWorkerStarted = device->completionWorker != NULL;
  if (!device->completionWorkerStarted) {
    DeleteCriticalSection(&device->completionLock);
  }
#  else
  if (pthread_mutex_init(&device->completionLock, NULL) != 0) {
    return false;
  }
  if (pthread_cond_init(&device->completionCondition, NULL) != 0) {
    pthread_mutex_destroy(&device->completionLock);
    return false;
  }
  device->completionWorkerStarted = pthread_create(&device->completionWorker,
                                                   NULL,
                                                   webgpu_completionMain,
                                                   device) == 0;
  if (!device->completionWorkerStarted) {
    pthread_cond_destroy(&device->completionCondition);
    pthread_mutex_destroy(&device->completionLock);
  }
#  endif
  return device->completionWorkerStarted;
}

void
gpu_webgpuQueueCompletion(GPUDeviceWebGPU    *device,
                          WGPUSubmissionIndex submission) {
  if (!device || !device->completionWorkerStarted) {
    return;
  }

  webgpu_completionLock(device);
  if (device->completionCount < GPU_WEBGPU_COMMAND_SLOT_COUNT) {
    device->completionSubmissions[device->completionTail] = submission;
    device->completionTail =
      (device->completionTail + 1u) % GPU_WEBGPU_COMMAND_SLOT_COUNT;
    device->completionCount++;
  }
  webgpu_completionSignal(device);
  webgpu_completionUnlock(device);
}

void
gpu_webgpuStopCompletionWorker(GPUDeviceWebGPU *device) {
  if (!device || !device->completionWorkerStarted) {
    return;
  }

  webgpu_completionLock(device);
  device->stoppingCompletionWorker = true;
  webgpu_completionSignal(device);
  webgpu_completionUnlock(device);

#  if defined(_WIN32) || defined(WIN32)
  WaitForSingleObject(device->completionWorker, INFINITE);
  CloseHandle(device->completionWorker);
  DeleteCriticalSection(&device->completionLock);
  device->completionWorker = NULL;
#  else
  pthread_join(device->completionWorker, NULL);
  pthread_cond_destroy(&device->completionCondition);
  pthread_mutex_destroy(&device->completionLock);
#  endif
  device->completionWorkerStarted = false;
}
#endif
