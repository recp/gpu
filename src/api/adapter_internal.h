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

#ifndef gpu_adapter_internal_h
#define gpu_adapter_internal_h

#include "../common.h"
#include "instance_internal.h"

struct GPUAdapter {
  struct GPUAdapter *next;
  GPUInstance       *inst;
  void              *_priv;
  GPUFeatureSet      supportedFeatures;
  uint32_t           supportedFeatureState;
  bool               supportsSwapchain;
  bool               supportsDisplayTiming;
  bool               supportsIncrementalPresent;
  bool               separatePresentQueue;
  GPUFeature         supportedFeatureStorage[
    GPU_FEATURE_INTERSECTION_FUNCTION_TABLE + 1u
  ];
};

static inline GPUApi *
gpuAdapterApi(const GPUAdapter *adapter) {
  return adapter ? gpuInstanceApi(adapter->inst) : NULL;
}

static inline uint32_t
gpuAdapterFeatureStateLoad(const GPUAdapter *adapter) {
#if defined(_WIN32) || defined(WIN32)
  return (uint32_t)InterlockedCompareExchange(
    (volatile LONG *)&adapter->supportedFeatureState,
    0,
    0
  );
#else
  return __atomic_load_n(&adapter->supportedFeatureState, __ATOMIC_ACQUIRE);
#endif
}

static inline bool
gpuAdapterFeatureStateBegin(GPUAdapter *adapter) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedCompareExchange(
    (volatile LONG *)&adapter->supportedFeatureState,
    1,
    0
  ) == 0;
#else
  uint32_t expected;

  expected = 0u;
  return __atomic_compare_exchange_n(&adapter->supportedFeatureState,
                                     &expected,
                                     1u,
                                     false,
                                     __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE);
#endif
}

static inline void
gpuAdapterFeatureStateComplete(GPUAdapter *adapter) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchange((volatile LONG *)&adapter->supportedFeatureState, 2);
#else
  __atomic_store_n(&adapter->supportedFeatureState, 2u, __ATOMIC_RELEASE);
#endif
}

#endif /* gpu_adapter_internal_h */
