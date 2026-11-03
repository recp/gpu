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
#include "../../src/api/frame_feedback_internal.h"

typedef struct CompletionState {
  GPUCommandBuffer          *command;
  GPUCommandBufferRecycleFn  recycle;
  GPUFrameFeedback          *feedback;
  GPUDevice                 *statsDevice;
  GPUDevice                 *errorDevice;
  double                     milliseconds;
  double                     start;
  double                     end;
  GPUResult                  result;
  GPUResult                  errorResult;
  GPUDeviceErrorType         errorType;
  uint32_t                   sequence;
  uint32_t                   stats;
  uint32_t                   events;
  uint32_t                   errors;
  uint32_t                   finishes;
  uint32_t                   statsOrder;
  uint32_t                   eventOrder;
  uint32_t                   errorOrder;
  uint32_t                   finishOrder;
} CompletionState;

@interface TimingProbe : NSObject {
@public
  NSError                *failure;
  MTLCommandBufferStatus  completionStatus;
  uint32_t                starts;
  uint32_t                ends;
}
- (MTLCommandBufferStatus)status;
- (NSError*)error;
- (CFTimeInterval)GPUStartTime;
- (CFTimeInterval)GPUEndTime;
@end

static CompletionState kCompletion;

static void
record_time(GPUDevice *device, double milliseconds);

static void
complete_feedback(GPUFrameFeedback *feedback, GPUResult result, double start, double end);

static void
report_error(GPUDevice *device, GPUDeviceErrorType type, GPUDeviceLostReason reason,
             GPUResult result, const char *message);

static void
finish_command(GPUCommandBuffer *command, GPUCommandBufferRecycleFn recycle);

/* exercise the actual completion handlers; replace only their external sinks. */

#define deviceRecordGPUFrameTime record_time
#define frameFeedbackComplete complete_feedback
#define deviceReportError report_error
#define finishCommandBuffer finish_command
#ifndef GPU_COMPLETION_TIMING_SOURCE
#  define GPU_COMPLETION_TIMING_SOURCE "../../src/backend/mt/impl/cmd/cmdque.m"
#endif
#include GPU_COMPLETION_TIMING_SOURCE
#undef finishCommandBuffer
#undef deviceReportError
#undef frameFeedbackComplete
#undef deviceRecordGPUFrameTime

@implementation TimingProbe
- (MTLCommandBufferStatus)status {
  return completionStatus;
}

- (NSError*)error {
  return failure;
}

- (CFTimeInterval)GPUStartTime {
  starts++;
  return 16.0;
}

- (CFTimeInterval)GPUEndTime {
  ends++;
  return 16.125;
}
@end

static void
record_time(GPUDevice *device, double milliseconds) {
  kCompletion.stats++;
  kCompletion.statsDevice  = device;
  kCompletion.milliseconds = milliseconds;
  kCompletion.statsOrder   = ++kCompletion.sequence;
}

static void
complete_feedback(GPUFrameFeedback *feedback, GPUResult result, double start, double end) {
  kCompletion.events++;
  kCompletion.feedback   = feedback;
  kCompletion.result     = result;
  kCompletion.start      = start;
  kCompletion.end        = end;
  kCompletion.eventOrder = ++kCompletion.sequence;
}

static void
report_error(GPUDevice *device, GPUDeviceErrorType type, GPUDeviceLostReason reason,
             GPUResult result, const char *message) {
  (void)reason;
  (void)message;
  kCompletion.errors++;
  kCompletion.errorDevice = device;
  kCompletion.errorType   = type;
  kCompletion.errorResult = result;
  kCompletion.errorOrder  = ++kCompletion.sequence;
}

static void
finish_command(GPUCommandBuffer *command, GPUCommandBufferRecycleFn recycle) {
  kCompletion.finishes++;
  kCompletion.command     = command;
  kCompletion.recycle     = recycle;
  kCompletion.finishOrder = ++kCompletion.sequence;
}

static bool
check_case(bool modern, uint32_t flags, bool error, bool missing, bool ready) {
  GPUDevice        device  = {0};
  MTCommandQueue   queue   = {0};
  MTCommandBuffer  native  = {0};
  GPUCommandBuffer command = {0};
  GPUQueue         handle  = {0};
  TimingProbe     *probe;
  GPUFrameFeedback *token;
  uint32_t         getters;
  uint32_t         stats;
  uint32_t         events;
  uint32_t         errors;
  bool             ok;

  probe = [TimingProbe new];
  probe->completionStatus = error ? MTLCommandBufferStatusError : MTLCommandBufferStatusCompleted;
  if (error)
    probe->failure = [NSError errorWithDomain:MTLCommandBufferErrorDomain
                                        code:MTLCommandBufferErrorOutOfMemory
                                    userInfo:nil];

  kCompletion = (CompletionState){0};
  token       = (GPUFrameFeedback *)&kCompletion;
  handle._device               = &device;
  command._queue               = &handle;
  command._recordsGPUFrameTime = (flags & 1u) != 0u;
  command._frameFeedback       = flags & 2u ? token : NULL;
  queue.inFlightGroup          = dispatch_group_create();
  atomic_init(&native.completionReady, ready);
  dispatch_group_enter(queue.inFlightGroup);

#if MT_HAS_METAL4
  if (modern)
    cmdoncomplete4(&command, &native, &queue, missing ? nil : probe);
  else
#endif
    cmdoncomplete(&command, &native, &queue, missing ? nil : (id<MTLCommandBuffer>)probe);

  getters = ready && !missing && flags != 0u ? 1u : 0u;
  stats   = ready && !missing && (flags & 1u) ? 1u : 0u;
  events  = ready && (flags & 2u) ? 1u : 0u;
  errors  = ready && !missing && error ? 1u : 0u;
  ok      = probe->starts == getters && probe->ends == getters
            && kCompletion.stats == stats && kCompletion.events == events
            && kCompletion.errors == errors && kCompletion.finishes == (ready ? 1u : 0u);

  if (stats)
    ok = ok && kCompletion.statsDevice == &device && kCompletion.milliseconds == 125.0;

  if (events) {
    ok = ok && kCompletion.feedback == token
         && kCompletion.result == (error || missing ? GPU_ERROR_BACKEND_FAILURE : GPU_OK)
         && kCompletion.start == (error || missing ? 0.0 : 16.0)
         && kCompletion.end == (error || missing ? 0.0 : 16.125)
         && kCompletion.eventOrder < kCompletion.finishOrder;
  }

  if (errors) {
    ok = ok && kCompletion.errorDevice == &device
         && kCompletion.errorType == GPU_DEVICE_ERROR_OUT_OF_MEMORY
         && kCompletion.errorResult == GPU_ERROR_OUT_OF_MEMORY
         && kCompletion.errorOrder == 1u;
  }

  if (ready) {
    ok = ok && kCompletion.command == &command && kCompletion.recycle == mt_recycleCommandBuffer
         && kCompletion.finishOrder == kCompletion.sequence
         && dispatch_group_wait(queue.inFlightGroup, DISPATCH_TIME_NOW) == 0;
  } else {
    ok = ok && dispatch_group_wait(queue.inFlightGroup, DISPATCH_TIME_NOW) != 0;
    dispatch_group_leave(queue.inFlightGroup);
  }

  if (!ok) {
    fprintf(stderr, "completion timing failed: modern=%u flags=%u error=%u missing=%u ready=%u "
                    "getters=%u/%u expected=%u stats=%u events=%u errors=%u finishes=%u\n",
            (unsigned)modern, flags, (unsigned)error, (unsigned)missing, (unsigned)ready,
            probe->starts, probe->ends, getters, kCompletion.stats, kCompletion.events,
            kCompletion.errors, kCompletion.finishes);
  }

  dispatch_release(queue.inFlightGroup);
  [probe release];
  return ok;
}

int
main(void) {
  uint32_t modes = 1u;
  uint32_t mode;
  uint32_t flags;
  uint32_t error;
  uint32_t checks = 0u;
  bool     ok = true;

  @autoreleasepool {
#if MT_HAS_METAL4
    if (@available(macOS 26.0, iOS 26.0, *))
      modes = 2u;
#endif

    for (mode = 0u; mode < modes; mode++) {
      for (flags = 0u; flags < 4u; flags++) {
        for (error = 0u; error < 2u; error++) {
          ok = check_case(mode != 0u, flags, error != 0u, false, true) && ok;
          checks++;
        }

        ok = check_case(mode != 0u, flags, false, true, true) && ok;
        ok = check_case(mode != 0u, flags, false, false, false) && ok;
        checks += 2u;
      }
    }
  }

  if (ok)
    printf("completion timing: %u checks across %u native completion paths passed\n", checks, modes);
  return ok ? 0 : 1;
}
