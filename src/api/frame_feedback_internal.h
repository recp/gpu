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

#ifndef gpu_frame_feedback_internal_h
#define gpu_frame_feedback_internal_h

#include "../common.h"

GPU_HIDE
void
frameFeedbackRetain(GPUFrameFeedback *feedback);

GPU_HIDE
void
frameFeedbackRelease(GPUFrameFeedback *feedback);

/* the emitting owner keeps its reference through the entire call. */
GPU_HIDE
void
frameFeedbackComplete(GPUFrameFeedback *feedback, GPUResult result, double start, double end);

GPU_HIDE
void
frameFeedbackPresent(GPUFrameFeedback *feedback, double time);

GPU_HIDE
void
frameFeedbackDiscard(GPUFrameFeedback *feedback);

#endif /* gpu_frame_feedback_internal_h */
