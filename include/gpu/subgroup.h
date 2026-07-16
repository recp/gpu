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

#ifndef gpu_subgroup_h
#define gpu_subgroup_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"

typedef struct GPUAdapter GPUAdapter;

typedef enum GPUSubgroupMatrixComponentTypeEXT {
  GPU_SUBGROUP_MATRIX_COMPONENT_UNKNOWN_EXT = 0,
  GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT     = 1,
  GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT     = 2,
  GPU_SUBGROUP_MATRIX_COMPONENT_F64_EXT     = 3,
  GPU_SUBGROUP_MATRIX_COMPONENT_I8_EXT      = 4,
  GPU_SUBGROUP_MATRIX_COMPONENT_I16_EXT     = 5,
  GPU_SUBGROUP_MATRIX_COMPONENT_I32_EXT     = 6,
  GPU_SUBGROUP_MATRIX_COMPONENT_I64_EXT     = 7,
  GPU_SUBGROUP_MATRIX_COMPONENT_U8_EXT      = 8,
  GPU_SUBGROUP_MATRIX_COMPONENT_U16_EXT     = 9,
  GPU_SUBGROUP_MATRIX_COMPONENT_U32_EXT     = 10,
  GPU_SUBGROUP_MATRIX_COMPONENT_U64_EXT     = 11,
  GPU_SUBGROUP_MATRIX_COMPONENT_BF16_EXT    = 12
} GPUSubgroupMatrixComponentTypeEXT;

typedef enum GPUSubgroupMatrixScopeEXT {
  GPU_SUBGROUP_MATRIX_SCOPE_UNKNOWN_EXT  = 0,
  GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT = 1
} GPUSubgroupMatrixScopeEXT;

typedef struct GPUSubgroupMatrixPropertiesEXT {
  uint32_t                          m;
  uint32_t                          n;
  uint32_t                          k;
  GPUSubgroupMatrixComponentTypeEXT aType;
  GPUSubgroupMatrixComponentTypeEXT bType;
  GPUSubgroupMatrixComponentTypeEXT cType;
  GPUSubgroupMatrixComponentTypeEXT resultType;
  GPUShaderStageFlags               stages;
  GPUSubgroupMatrixScopeEXT         scope;
  bool                              saturatingAccumulation;
} GPUSubgroupMatrixPropertiesEXT;

/* Enumerates exact supported profiles; pass NULL to query the count. */
GPU_EXPORT
GPUResult
GPUGetSubgroupMatrixPropertiesEXT(
  const GPUAdapter                   *adapter,
  uint32_t                           *inoutPropertyCount,
  GPUSubgroupMatrixPropertiesEXT     *outProperties
);

#ifdef __cplusplus
}
#endif
#endif /* gpu_subgroup_h */
