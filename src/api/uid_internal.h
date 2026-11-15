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

/*
 * ids for caches' keys (bind groups, pipelines): an object made where a
 * destroyed one lived has its address, never its id, so it can't meet what
 * was cached for the old one. Objects are calloc'd: 0 means none yet. One
 * atomic add per object, the rest is a load
 */

#ifndef gpu_uid_internal_h
#define gpu_uid_internal_h

#include "../common.h"

/* an object's first use as a key: a new id, the first of racing threads' wins */
GPU_HIDE
uint64_t
gpuObjectUidNew(uint64_t *slot);

/* the id is only a number, set once from 0: a plain (relaxed) load */
static GPU_INLINE uint64_t
gpuObjectUid(uint64_t *slot) {
  uint64_t uid;
#if defined(_MSC_VER) && !defined(__clang__)
  uid = (uint64_t)*(volatile const LONG64 *)slot; /* aligned: one read */
#else
  uid = __atomic_load_n(slot, __ATOMIC_RELAXED);
#endif
  return uid ? uid : gpuObjectUidNew(slot);
}

#endif /* gpu_uid_internal_h */
