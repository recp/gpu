/*
 * Copyright (c), Recep Aslantas.
 *
 * MIT License (MIT), http://opensource.org/licenses/MIT
 * Full license can be found in the LICENSE file
 */

#ifndef gpu_common_h
#define gpu_common_h

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

#if defined(_WIN32)
#  ifdef _gpu_dll
#    define GPU_EXPORT __declspec(dllexport)
#  else
#    define GPU_EXPORT __declspec(dllimport)
#  endif
#  define _gpu_hide
#  define GPU_INLINE __forceinline
#  define likely(x)   x
#  define unlikely(x) x
#  define GPU_NONNULL
#else
#  define GPU_EXPORT   __attribute__((visibility("default")))
#  define _gpu_hide    __attribute__((visibility("hidden")))
#  define GPU_INLINE inline __attribute((always_inline))
#  define likely(x)    __builtin_expect(!!(x), 1)
#  define unlikely(x)  __builtin_expect(!!(x), 0)
#  define GPU_NONNULL  __attribute__((nonnull))
#endif

#define GPU_ARRAY_LEN(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#define GPU_STRINGIFY(...)  #__VA_ARGS__
#define GPU_STRINGIFY2(x)   GPU_STRINGIFY(x)

#define GPU_FLG(FLAGS, FLAG) ((FLAGS & FLAG) == FLAG)

typedef struct GPUViewport {
  double originX, originY, width, height, znear, zfar;
} GPUViewport;

typedef enum GPUFuncType {
  GPU_FUNC_VERT = 1,
  GPU_FUNC_FRAG = 2
} GPUFunctionType;

#endif /* gpu_common_h */
