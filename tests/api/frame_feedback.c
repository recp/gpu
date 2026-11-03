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

#include "../../src/api/frame_feedback_internal.h"
#include "../../src/api/frame_internal.h"
#include "../../src/api/device_internal.h"
#include <math.h>
#include <pthread.h>

typedef struct Observation {
  GPUFrameEvent     events[4];
  GPUFrameFeedback *feedback;
  uint32_t          count;
  bool              destroySelf;
} Observation;

typedef struct Fixture {
  GPUApi       api;
  GPUDevice    device;
  GPUFrame     frame;
  Observation  observation;
} Fixture;

typedef struct Drain {
  pthread_mutex_t mutex;
  pthread_cond_t  condition;
  GPUFrameFeedback *feedback;
  bool              entered;
  bool              release;
  bool              exited;
  bool              destroying;
  bool              drained;
} Drain;

/* this target links the common owner directly, with no native runtime. */
GPU_EXPORT
bool
GPUIsFeatureEnabled(const GPUDevice *device, GPUFeature feature) {
  return device && feature == GPU_FEATURE_FRAME_FEEDBACK
         && (device->enabledFeatureMask & (UINT64_C(1) << GPU_FEATURE_FRAME_FEEDBACK));
}

static GPUResult
observe(GPUFrame *frame, GPUFrameFeedback *feedback) {
  return frame && feedback ? GPU_OK : GPU_ERROR_INVALID_ARGUMENT;
}

static void
record(const GPUFrameEvent *event, void *user) {
  Observation *observation;

  observation = user;
  if (observation->count < GPU_ARRAY_LEN(observation->events))
    observation->events[observation->count] = *event;

  observation->count++;
  if (observation->destroySelf) {
    GPUDestroyFrameFeedback(observation->feedback);
    observation->feedback = NULL;
  }
}

static bool
create(Fixture *fixture, double target, GPUFrameFeedbackFn callback, void *user) {
  GPUFrameFeedbackCreateInfo info = {0};

  memset(fixture, 0, sizeof(*fixture));
  fixture->api.frame.observe          = observe;
  fixture->device._api               = &fixture->api;
  fixture->device.enabledFeatureMask = UINT64_C(1) << GPU_FEATURE_FRAME_FEEDBACK;
  fixture->frame.device              = &fixture->device;
  fixture->frame.drawable            = &fixture->frame;
  fixture->frame.targetPresentTime   = target;
  info.chain.sType      = GPU_STRUCTURE_TYPE_FRAME_FEEDBACK_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.callback         = callback ? callback : record;
  info.user             = user ? user : &fixture->observation;
  info.frameId          = UINT64_C(0xfedcba9876543210);
  return GPUCreateFrameFeedback(&fixture->frame, &info, &fixture->observation.feedback) == GPU_OK;
}

static void
destroy(Fixture *fixture) {
  GPUDestroyFrameFeedback(fixture->observation.feedback);
  frameFeedbackRelease(fixture->frame._feedback);
  fixture->observation.feedback = NULL;
  fixture->frame._feedback      = NULL;
}

static bool
check_order(bool presentFirst) {
  Fixture        fixture;
  GPUFrameEvent *gpu, *present;
  bool           ok;

  if (!create(&fixture, 32.0, NULL, NULL))
    return false;

  if (presentFirst)
    frameFeedbackPresent(fixture.frame._feedback, 33.0);

  frameFeedbackComplete(fixture.frame._feedback, GPU_OK, 30.0, 31.0);
  if (!presentFirst)
    frameFeedbackPresent(fixture.frame._feedback, 33.0);

  frameFeedbackComplete(fixture.frame._feedback, GPU_ERROR_BACKEND_FAILURE, 0.0, 0.0);
  frameFeedbackPresent(fixture.frame._feedback, 34.0);
  frameFeedbackDiscard(fixture.frame._feedback);
  gpu     = &fixture.observation.events[presentFirst ? 1 : 0];
  present = &fixture.observation.events[presentFirst ? 0 : 1];
  ok = fixture.observation.count == 2u
       && gpu->kind == GPU_FRAME_EVENT_GPU_COMPLETE && present->kind == GPU_FRAME_EVENT_PRESENTED
       && gpu->frameId == UINT64_C(0xfedcba9876543210) && present->frameId == gpu->frameId
       && gpu->result == GPU_OK && present->result == GPU_OK
       && gpu->flags == (GPU_FRAME_EVENT_TARGET_TIME_VALID | GPU_FRAME_EVENT_GPU_TIME_VALID)
       && present->flags == GPU_FRAME_EVENT_TARGET_TIME_VALID
       && gpu->targetPresentTime == 32.0 && present->targetPresentTime == 32.0
       && gpu->gpuStartTime == 30.0 && gpu->gpuEndTime == 31.0 && gpu->presentedTime == 0.0
       && present->presentedTime == 33.0 && present->gpuStartTime == 0.0 && present->gpuEndTime == 0.0;
  destroy(&fixture);
  return ok;
}

static bool
check_invalid_times(void) {
  const double invalid[] = {0.0, -1.0, INFINITY, NAN};
  const double ranges[][2] = {{0.0, 2.0}, {-1.0, 2.0}, {3.0, 2.0},
                              {NAN, 2.0}, {1.0, INFINITY}, {1.0, 2.0}};
  Fixture        fixture;
  GPUFrameEvent *event;
  GPUResult      result;
  uint32_t       i;
  bool           ok;

  for (i = 0u; i < GPU_ARRAY_LEN(invalid); i++) {
    if (!create(&fixture, invalid[i], NULL, NULL))
      return false;

    frameFeedbackPresent(fixture.frame._feedback, invalid[i]);
    frameFeedbackPresent(fixture.frame._feedback, 5.0);
    event = &fixture.observation.events[0];
    ok = fixture.observation.count == 1u && event->kind == GPU_FRAME_EVENT_PRESENTATION_UNAVAILABLE
         && event->flags == 0u && event->result == GPU_OK && event->targetPresentTime == 0.0
         && event->presentedTime == 0.0;
    destroy(&fixture);
    if (!ok)
      return false;
  }

  for (i = 0u; i < GPU_ARRAY_LEN(ranges); i++) {
    if (!create(&fixture, 0.0, NULL, NULL))
      return false;

    result = i + 1u == GPU_ARRAY_LEN(ranges) ? GPU_ERROR_BACKEND_FAILURE : GPU_OK;
    frameFeedbackComplete(fixture.frame._feedback, result, ranges[i][0], ranges[i][1]);
    event = &fixture.observation.events[0];
    ok = fixture.observation.count == 1u && event->kind == GPU_FRAME_EVENT_GPU_COMPLETE
         && event->result == result && event->flags == 0u
         && event->gpuStartTime == 0.0 && event->gpuEndTime == 0.0;
    destroy(&fixture);
    if (!ok)
      return false;
  }
  return true;
}

static bool
check_discard_cancel(void) {
  Fixture fixture;
  bool    ok;

  if (!create(&fixture, 0.0, NULL, NULL))
    return false;

  frameFeedbackDiscard(fixture.frame._feedback);
  frameFeedbackDiscard(fixture.frame._feedback);
  frameFeedbackPresent(fixture.frame._feedback, 1.0);
  frameFeedbackComplete(fixture.frame._feedback, GPU_OK, 1.0, 2.0);
  ok = fixture.observation.count == 1u && fixture.observation.events[0].kind == GPU_FRAME_EVENT_DISCARDED
       && fixture.observation.events[0].flags == 0u;
  destroy(&fixture);
  if (!ok || !create(&fixture, 0.0, NULL, NULL))
    return false;

  GPUDestroyFrameFeedback(fixture.observation.feedback);
  fixture.observation.feedback = NULL;
  frameFeedbackComplete(fixture.frame._feedback, GPU_OK, 1.0, 2.0);
  frameFeedbackPresent(fixture.frame._feedback, 3.0);
  ok = fixture.observation.count == 0u;
  destroy(&fixture);
  if (!ok || !create(&fixture, 0.0, NULL, NULL))
    return false;

  fixture.observation.destroySelf = true;
  frameFeedbackComplete(fixture.frame._feedback, GPU_OK, 1.0, 2.0);
  frameFeedbackPresent(fixture.frame._feedback, 3.0);
  ok = fixture.observation.count == 1u && !fixture.observation.feedback;
  destroy(&fixture);
  return ok;
}

static void
blocked_callback(const GPUFrameEvent *event, void *user) {
  Drain *drain;

  (void)event;
  drain = user;
  pthread_mutex_lock(&drain->mutex);
  drain->entered = true;
  pthread_cond_broadcast(&drain->condition);
  while (!drain->release)
    pthread_cond_wait(&drain->condition, &drain->mutex);

  drain->exited = true;
  pthread_mutex_unlock(&drain->mutex);
}

static void*
emit_thread(void *user) {
  Drain *drain;

  drain = user;
  frameFeedbackComplete(drain->feedback, GPU_OK, 1.0, 2.0);
  return NULL;
}

static void*
destroy_thread(void *user) {
  Drain *drain;

  drain = user;
  pthread_mutex_lock(&drain->mutex);
  drain->destroying = true;
  pthread_cond_broadcast(&drain->condition);
  pthread_mutex_unlock(&drain->mutex);

  GPUDestroyFrameFeedback(drain->feedback);
  pthread_mutex_lock(&drain->mutex);
  drain->drained = drain->exited;
  pthread_mutex_unlock(&drain->mutex);
  return NULL;
}

static bool
check_drain(void) {
  Fixture   fixture;
  Drain     drain = {0};
  pthread_t emitter, destroyer;
  bool      ok;

  if (pthread_mutex_init(&drain.mutex, NULL) != 0)
    return false;

  if (pthread_cond_init(&drain.condition, NULL) != 0) {
    pthread_mutex_destroy(&drain.mutex);
    return false;
  }

  if (!create(&fixture, 0.0, blocked_callback, &drain)) {
    pthread_cond_destroy(&drain.condition);
    pthread_mutex_destroy(&drain.mutex);
    return false;
  }

  drain.feedback = fixture.observation.feedback;
  if (pthread_create(&emitter, NULL, emit_thread, &drain) != 0)
    abort();

  pthread_mutex_lock(&drain.mutex);
  while (!drain.entered)
    pthread_cond_wait(&drain.condition, &drain.mutex);

  pthread_mutex_unlock(&drain.mutex);
  if (pthread_create(&destroyer, NULL, destroy_thread, &drain) != 0)
    abort();

  pthread_mutex_lock(&drain.mutex);
  while (!drain.destroying)
    pthread_cond_wait(&drain.condition, &drain.mutex);

  drain.release = true;
  pthread_cond_broadcast(&drain.condition);
  pthread_mutex_unlock(&drain.mutex);
  if (pthread_join(emitter, NULL) != 0 || pthread_join(destroyer, NULL) != 0)
    abort();

  fixture.observation.feedback = NULL;
  ok = drain.drained;
  destroy(&fixture);
  pthread_cond_destroy(&drain.condition);
  pthread_mutex_destroy(&drain.mutex);
  return ok;
}

int
main(void) {
  if (!check_order(false) || !check_order(true) || !check_invalid_times()
      || !check_discard_cancel() || !check_drain()) {
    fprintf(stderr, "frame feedback common regression failed\n");
    return 1;
  }

  puts("frame feedback: event order, dedup, invalid times, discard, cancellation, self-destroy, drain=pass");
  return 0;
}
