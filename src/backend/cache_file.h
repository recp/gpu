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

#ifndef gpu_backend_cache_file_h
#define gpu_backend_cache_file_h

#include "../common.h"

typedef struct GPUCacheFileGuard {
  intptr_t native;
  bool     locked;
} GPUCacheFileGuard;

GPU_HIDE
bool
gpuCacheFileBegin(const char *path, GPUCacheFileGuard *guard);

GPU_HIDE
void
gpuCacheFileEnd(GPUCacheFileGuard *guard);

GPU_HIDE
char *
gpuCacheFileTemporaryPath(const char *path, const void *identity);

GPU_HIDE
bool
gpuCacheFileReplace(const char *source, const char *destination);

#endif /* gpu_backend_cache_file_h */
