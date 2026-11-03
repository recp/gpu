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

#include "frame_feedback_internal.h"
#include "frame_internal.h"
#include "device_internal.h"
#include <math.h>

#if !defined(_WIN32) && !defined(WIN32)
#  include <pthread.h>
#endif

struct GPUFrameFeedback {
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION   lock;
  CONDITION_VARIABLE condition;
  DWORD              callbackThread;
#else
  pthread_mutex_t mutex;
  pthread_cond_t  condition;
  pthread_t       callbackThread;
#endif
  GPUFrameFeedbackFn callback;
  void              *user;
  uint64_t           frameId;
  double             targetPresentTime;
  uint32_t           references;
  uint32_t           delivered;
  bool               active;
  bool               canceled;
};

static void
feedbackLock(GPUFrameFeedback *feedback) {
#if defined(_WIN32) || defined(WIN32)
  EnterCriticalSection(&feedback->lock);
#else
  pthread_mutex_lock(&feedback->mutex);
#endif
}

static void
feedbackUnlock(GPUFrameFeedback *feedback) {
#if defined(_WIN32) || defined(WIN32)
  LeaveCriticalSection(&feedback->lock);
#else
  pthread_mutex_unlock(&feedback->mutex);
#endif
}

static void
feedbackWait(GPUFrameFeedback *feedback) {
#if defined(_WIN32) || defined(WIN32)
  SleepConditionVariableCS(&feedback->condition, &feedback->lock, INFINITE);
#else
  pthread_cond_wait(&feedback->condition, &feedback->mutex);
#endif
}

static bool
feedbackCallbackThread(GPUFrameFeedback *feedback) {
#if defined(_WIN32) || defined(WIN32)
  return feedback->callbackThread == GetCurrentThreadId();
#else
  return pthread_equal(feedback->callbackThread, pthread_self()) != 0;
#endif
}

static void
feedbackEmit(GPUFrameFeedback *feedback, GPUFrameEvent *event) {
  void     *user;
  uint32_t  excludes;

  GPUFrameFeedbackFn callback;

  if (!feedback)
    return;

  excludes = (1u << GPU_FRAME_EVENT_GPU_COMPLETE) | (1u << GPU_FRAME_EVENT_DISCARDED);
  if (event->kind == GPU_FRAME_EVENT_PRESENTED || event->kind == GPU_FRAME_EVENT_PRESENTATION_UNAVAILABLE)
    excludes = (1u << GPU_FRAME_EVENT_PRESENTED) | (1u << GPU_FRAME_EVENT_PRESENTATION_UNAVAILABLE)
               | (1u << GPU_FRAME_EVENT_DISCARDED);

  feedbackLock(feedback);
  while (feedback->active && !feedback->canceled && !(feedback->delivered & excludes))
    feedbackWait(feedback);

  if (feedback->canceled || (feedback->delivered & excludes)) {
    feedbackUnlock(feedback);
    return;
  }

  feedback->delivered |= 1u << event->kind;
  feedback->active     = true;
#if defined(_WIN32) || defined(WIN32)
  feedback->callbackThread = GetCurrentThreadId();
#else
  feedback->callbackThread = pthread_self();
#endif
  callback = feedback->callback;
  user     = feedback->user;
  event->frameId = feedback->frameId;
  if (feedback->targetPresentTime > 0.0) {
    event->targetPresentTime = feedback->targetPresentTime;
    event->flags |= GPU_FRAME_EVENT_TARGET_TIME_VALID;
  }
  feedbackUnlock(feedback);

  callback(event, user);

  feedbackLock(feedback);
  feedback->active = false;
#if defined(_WIN32) || defined(WIN32)
  WakeAllConditionVariable(&feedback->condition);
#else
  pthread_cond_broadcast(&feedback->condition);
#endif
  feedbackUnlock(feedback);
}

GPU_EXPORT
GPUResult
GPUCreateFrameFeedback(GPUFrame                         *frame,
                        const GPUFrameFeedbackCreateInfo *info,
                        GPUFrameFeedback                **outFeedback) {
  GPUFrameFeedback *feedback;
  GPUApi           *api;
  GPUResult         result;

  if (outFeedback)
    *outFeedback = NULL;

  if (!frame || !info || !outFeedback
      || info->chain.sType != GPU_STRUCTURE_TYPE_FRAME_FEEDBACK_CREATE_INFO
      || info->chain.structSize < sizeof(*info))
    return GPU_ERROR_INVALID_ARGUMENT;

  if (info->chain.pNext)
    return GPU_ERROR_UNSUPPORTED;

  if (!info->callback || !frame->device || !frame->drawable
      || frame->presentationScheduled || frame->_feedback)
    return GPU_ERROR_INVALID_ARGUMENT;

  api = deviceApi(frame->device);
  if (!GPUIsFeatureEnabled(frame->device, GPU_FEATURE_FRAME_FEEDBACK) || !api || !api->frame.observe)
    return GPU_ERROR_UNSUPPORTED;

  if (!(feedback = calloc(1u, sizeof(*feedback))))
    return GPU_ERROR_OUT_OF_MEMORY;

#if defined(_WIN32) || defined(WIN32)
  InitializeCriticalSection(&feedback->lock);
  InitializeConditionVariable(&feedback->condition);
#else
  if (pthread_mutex_init(&feedback->mutex, NULL) != 0) {
    free(feedback);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  if (pthread_cond_init(&feedback->condition, NULL) != 0) {
    pthread_mutex_destroy(&feedback->mutex);
    free(feedback);
    return GPU_ERROR_BACKEND_FAILURE;
  }
#endif
  feedback->references = 1u;
  feedback->callback   = info->callback;
  feedback->user       = info->user;
  feedback->frameId    = info->frameId;
  if (isfinite(frame->targetPresentTime) && frame->targetPresentTime > 0.0)
    feedback->targetPresentTime = frame->targetPresentTime;

  result = api->frame.observe(frame, feedback);
  if (result != GPU_OK) {
    GPUDestroyFrameFeedback(feedback);
    return result;
  }

  frameFeedbackRetain(feedback);
  frame->_feedback = feedback;
  *outFeedback     = feedback;
  return GPU_OK;
}

GPU_HIDE
void
frameFeedbackRetain(GPUFrameFeedback *feedback) {
  if (!feedback)
    return;

#if defined(_WIN32) || defined(WIN32)
  InterlockedIncrement((volatile LONG *)&feedback->references);
#else
  __atomic_add_fetch(&feedback->references, 1u, __ATOMIC_RELAXED);
#endif
}

GPU_HIDE
void
frameFeedbackRelease(GPUFrameFeedback *feedback) {
  if (!feedback)
    return;

#if defined(_WIN32) || defined(WIN32)
  if (InterlockedDecrement((volatile LONG *)&feedback->references) != 0)
    return;

  DeleteCriticalSection(&feedback->lock);
#else
  if (__atomic_sub_fetch(&feedback->references, 1u, __ATOMIC_ACQ_REL) != 0u)
    return;

  pthread_cond_destroy(&feedback->condition);
  pthread_mutex_destroy(&feedback->mutex);
#endif
  free(feedback);
}

GPU_EXPORT
void
GPUDestroyFrameFeedback(GPUFrameFeedback *feedback) {
  if (!feedback)
    return;

  feedbackLock(feedback);
  feedback->canceled = true;
  feedback->callback = NULL;
  feedback->user     = NULL;
  while (feedback->active && !feedbackCallbackThread(feedback))
    feedbackWait(feedback);

  feedbackUnlock(feedback);
  frameFeedbackRelease(feedback);
}

GPU_HIDE
void
frameFeedbackComplete(GPUFrameFeedback *feedback, GPUResult result, double start, double end) {
  GPUFrameEvent event = {0};

  event.kind   = GPU_FRAME_EVENT_GPU_COMPLETE;
  event.result = result;
  if (result == GPU_OK && isfinite(start) && isfinite(end) && start > 0.0 && end >= start) {
    event.gpuStartTime = start;
    event.gpuEndTime   = end;
    event.flags       = GPU_FRAME_EVENT_GPU_TIME_VALID;
  }
  feedbackEmit(feedback, &event);
}

GPU_HIDE
void
frameFeedbackPresent(GPUFrameFeedback *feedback, double time) {
  GPUFrameEvent event = {0};

  event.kind = GPU_FRAME_EVENT_PRESENTATION_UNAVAILABLE;
  if (isfinite(time) && time > 0.0) {
    event.kind          = GPU_FRAME_EVENT_PRESENTED;
    event.presentedTime = time;
  }
  feedbackEmit(feedback, &event);
}

GPU_HIDE
void
frameFeedbackDiscard(GPUFrameFeedback *feedback) {
  GPUFrameEvent event = {0};

  event.kind = GPU_FRAME_EVENT_DISCARDED;
  feedbackEmit(feedback, &event);
}
