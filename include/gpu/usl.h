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

#ifndef gpu_usl_h
#define gpu_usl_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef enum GPUUSLSamplerFilter {
  GPUUSLSamplerFilterNearest = 0,
  GPUUSLSamplerFilterLinear = 1
} GPUUSLSamplerFilter;

typedef enum GPUUSLSamplerAddressMode {
  GPUUSLSamplerAddressClampToEdge = 0,
  GPUUSLSamplerAddressRepeat = 1,
  GPUUSLSamplerAddressMirroredRepeat = 2,
  GPUUSLSamplerAddressClampToZero = 3,
  GPUUSLSamplerAddressClampToBorder = 4
} GPUUSLSamplerAddressMode;

typedef enum GPUUSLSamplerCoordSpace {
  GPUUSLSamplerCoordNormalized = 0,
  GPUUSLSamplerCoordPixel = 1
} GPUUSLSamplerCoordSpace;

typedef enum GPUUSLSamplerCompareFunc {
  GPUUSLSamplerCompareNever = 0,
  GPUUSLSamplerCompareLess = 1,
  GPUUSLSamplerCompareEqual = 2,
  GPUUSLSamplerCompareLessEqual = 3,
  GPUUSLSamplerCompareGreater = 4,
  GPUUSLSamplerCompareNotEqual = 5,
  GPUUSLSamplerCompareGreaterEqual = 6,
  GPUUSLSamplerCompareAlways = 7
} GPUUSLSamplerCompareFunc;

typedef struct GPUUSLStaticSamplerDesc {
  uint32_t logicalIndex;
  uint32_t minFilter;
  uint32_t magFilter;
  uint32_t mipFilter;
  uint32_t addressMode;
  uint32_t coordSpace;
  uint32_t compareFunc;
  uint32_t hasCompare;
  uint32_t maxAnisotropy;
} GPUUSLStaticSamplerDesc;

GPU_EXPORT
int
GPUUSLStaticSamplerDescIsValid(const GPUUSLStaticSamplerDesc *desc);

#ifdef __cplusplus
}
#endif
#endif /* gpu_usl_h */
