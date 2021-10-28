/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef gpu_common_h
#define gpu_common_h

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#if defined(_MSC_VER)
#  ifdef GPU_STATIC
#    define GPU_EXPORT
#  elif defined(GPU_EXPORTS)
#    define GPU_EXPORT __declspec(dllexport)
#  else
#    define GPU_EXPORT __declspec(dllimport)
#  endif
#  define GPU_HIDE
#  define GPU_INLINE __forceinline
#  define likely(x)   x
#  define unlikely(x) x
#  define GPU_NONNULL
#else
#  define GPU_EXPORT   __attribute__((visibility("default")))
#  define GPU_INLINE   inline __attribute((always_inline))
#  define GPU_HIDE     __attribute__((visibility("hidden")))
#  define likely(x)    __builtin_expect(!!(x), 1)
#  define unlikely(x)  __builtin_expect(!!(x), 0)
#  define GPU_NONNULL  __attribute__((nonnull))
#endif

#define GPU_ARRAY_LEN(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#define GPU_STRINGIFY(...)  #__VA_ARGS__
#define GPU_STRINGIFY2(x)   GPU_STRINGIFY(x)

#define GPU_FLG(FLAGS, FLAG) ((FLAGS & FLAG) == FLAG)

#endif /* gpu_common_h */
