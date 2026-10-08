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

typedef struct CacheFileGuard {
  intptr_t native;
  bool     locked;
} CacheFileGuard;

GPU_HIDE
bool
cacheFileBegin(const char *path, CacheFileGuard    *guard);

GPU_HIDE
void
cacheFileEnd(CacheFileGuard    *guard);

GPU_HIDE
char*
cacheFileTemporaryPath(const char *path, const void *identity);

GPU_HIDE
bool
cacheFileReplace(const char *source, const char *destination);

#endif /* gpu_backend_cache_file_h */
