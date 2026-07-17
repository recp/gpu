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
#ifdef __cplusplus
extern "C" {
#define GPU_EXTERN extern "C"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>

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

enum {
  GPU_ENCODER_MAX_BIND_GROUPS = 4u
};

#define GPU_STRINGIFY(...)  #__VA_ARGS__
#define GPU_STRINGIFY2(x)   GPU_STRINGIFY(x)

#define GPU_FLG(FLAGS, FLAG) ((FLAGS & FLAG) == FLAG)

typedef enum GPUBackend {
  GPU_BACKEND_DEFAULT = 0,
  GPU_BACKEND_METAL   = 1,
  GPU_BACKEND_VULKAN  = 2,
  GPU_BACKEND_DX12    = 3
} GPUBackend;

typedef enum GPUStructureType {
  GPU_STRUCTURE_TYPE_NONE = 0,
  GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_SURFACE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_NATIVE_SURFACE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
  GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO,
  GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
  GPU_STRUCTURE_TYPE_SHADER_LIBRARY_CREATE_INFO,
  GPU_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO,
  GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO,
  GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO,
  GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
  GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO,
  GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO,
  GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
  GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_RUNTIME_CONFIG,
  GPU_STRUCTURE_TYPE_TRANSIENT_ALLOCATOR_CONFIG,
  GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
  GPU_STRUCTURE_TYPE_QUERY_SET_CREATE_INFO,
  GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO,
  GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT,
  GPU_STRUCTURE_TYPE_MESH_PIPELINE_EXT,
  GPU_STRUCTURE_TYPE_SHADING_RATE_ATTACHMENT_EXT,
  GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_CREATE_INFO_EXT,
  GPU_STRUCTURE_TYPE_RASTERIZATION_RATE_MAP_RENDER_PASS_EXT,
  GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_INFO_EXT,
  GPU_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_EXT,
  GPU_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_EXT,
  GPU_STRUCTURE_TYPE_SHADER_TABLE_CREATE_INFO_EXT,
  GPU_STRUCTURE_TYPE_HEAP_CREATE_INFO
} GPUStructureType;

typedef struct GPUChainedStruct {
  GPUStructureType sType;
  uint32_t         structSize;
  const void      *pNext;
} GPUChainedStruct;

typedef enum GPUResult {
  GPU_OK = 0,
  GPU_ERROR_INVALID_ARGUMENT = -1,
  GPU_ERROR_UNSUPPORTED = -2,
  GPU_ERROR_OUT_OF_MEMORY = -3,
  GPU_ERROR_BACKEND_FAILURE = -4,
  GPU_ERROR_INSUFFICIENT_CAPACITY = -5,
  GPU_ERROR_TIMEOUT = -6
} GPUResult;

typedef uint32_t GPUShaderStageFlags;
enum {
  GPU_SHADER_STAGE_VERTEX_BIT   = 1u << 0,
  GPU_SHADER_STAGE_FRAGMENT_BIT = 1u << 1,
  GPU_SHADER_STAGE_COMPUTE_BIT  = 1u << 2,
  GPU_SHADER_STAGE_TASK_BIT     = 1u << 3,
  GPU_SHADER_STAGE_MESH_BIT     = 1u << 4,
  GPU_SHADER_STAGE_RAY_GENERATION_BIT = 1u << 5,
  GPU_SHADER_STAGE_MISS_BIT           = 1u << 6,
  GPU_SHADER_STAGE_CLOSEST_HIT_BIT    = 1u << 7,
  GPU_SHADER_STAGE_ANY_HIT_BIT        = 1u << 8,
  GPU_SHADER_STAGE_INTERSECTION_BIT   = 1u << 9,
  GPU_SHADER_STAGE_CALLABLE_BIT       = 1u << 10
};

typedef enum GPUPipelineStageMask {
  GPU_STAGE_TOP      = 1u << 0,
  GPU_STAGE_VERTEX   = 1u << 1,
  GPU_STAGE_FRAGMENT = 1u << 2,
  GPU_STAGE_COMPUTE  = 1u << 3,
  GPU_STAGE_TRANSFER = 1u << 4,
  GPU_STAGE_BOTTOM   = 1u << 5
} GPUPipelineStageMask;

typedef enum GPUBindingType {
  GPU_BINDING_UNIFORM_BUFFER           = 0,
  GPU_BINDING_READ_ONLY_STORAGE_BUFFER = 1,
  GPU_BINDING_STORAGE_BUFFER           = 2,
  GPU_BINDING_SAMPLED_TEXTURE          = 3,
  GPU_BINDING_STORAGE_TEXTURE          = 4,
  GPU_BINDING_SAMPLER                  = 5,
  GPU_BINDING_ACCELERATION_STRUCTURE   = 6
} GPUBindingType;

#if defined(__APPLE__) && defined(__OBJC__)
#  include <TargetConditionals.h>
#  if TARGET_OS_IOS || TARGET_OS_TV
#    define GPUScreenScale(view) [UIScreen mainScreen].scale
#  elif TARGET_OS_MAC
#    define GPUScreenScale(view) view.window ? view.window.backingScaleFactor : [NSScreen mainScreen].backingScaleFactor
#  endif
#endif

#ifdef __cplusplus
}
#endif
#endif /* gpu_common_h */
