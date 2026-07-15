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

#ifndef gpu_ray_h
#define gpu_ray_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "buffer.h"
#include "cmdqueue.h"
#include "stage-io.h"
#include "vertex.h"

typedef struct GPUAccelerationStructureEXT            GPUAccelerationStructureEXT;
typedef struct GPUAccelerationStructurePassEncoderEXT GPUAccelerationStructurePassEncoderEXT;

typedef enum GPUAccelerationStructureTypeEXT {
  GPU_ACCELERATION_STRUCTURE_BOTTOM_LEVEL_EXT = 0,
  GPU_ACCELERATION_STRUCTURE_TOP_LEVEL_EXT    = 1
} GPUAccelerationStructureTypeEXT;

typedef uint32_t GPUAccelerationStructureBuildFlagsEXT;
enum {
  GPU_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_EXT      = 1u << 0,
  GPU_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_EXT = 1u << 1,
  GPU_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_EXT = 1u << 2
};

typedef enum GPUAccelerationStructureBuildModeEXT {
  GPU_ACCELERATION_STRUCTURE_BUILD_EXT  = 0,
  GPU_ACCELERATION_STRUCTURE_UPDATE_EXT = 1
} GPUAccelerationStructureBuildModeEXT;

typedef uint32_t GPUAccelerationStructureGeometryFlagsEXT;
enum {
  GPU_ACCELERATION_STRUCTURE_GEOMETRY_NON_OPAQUE_BIT_EXT = 1u << 0
};

typedef uint32_t GPUAccelerationStructureInstanceFlagsEXT;
enum {
  GPU_ACCELERATION_STRUCTURE_INSTANCE_DISABLE_CULL_BIT_EXT     = 1u << 0,
  GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_OPAQUE_BIT_EXT     = 1u << 1,
  GPU_ACCELERATION_STRUCTURE_INSTANCE_FORCE_NON_OPAQUE_BIT_EXT = 1u << 2
};

typedef struct GPUAccelerationStructureTriangleGeometryEXT {
  GPUBuffer                                   *vertexBuffer;
  GPUBuffer                                   *indexBuffer;
  uint64_t                                     vertexOffset;
  uint64_t                                     indexOffset;
  uint32_t                                     vertexCount;
  uint32_t                                     vertexStride;
  uint32_t                                     indexCount;
  GPUVertexFormat                              vertexFormat;
  GPUIndexType                                 indexType;
  GPUAccelerationStructureGeometryFlagsEXT     flags;
} GPUAccelerationStructureTriangleGeometryEXT;

typedef struct GPUAccelerationStructureInstanceEXT {
  GPUAccelerationStructureEXT              *structure;
  float                                      transform[3][4];
  GPUAccelerationStructureInstanceFlagsEXT  flags;
  uint8_t                                    mask;
} GPUAccelerationStructureInstanceEXT;

typedef struct GPUAccelerationStructureBuildInfoEXT {
  GPUChainedStruct                       chain;
  const char                            *label;
  GPUAccelerationStructureEXT           *source;
  GPUAccelerationStructureTypeEXT        type;
  GPUAccelerationStructureBuildFlagsEXT  flags;
  GPUAccelerationStructureBuildModeEXT   mode;
  union {
    struct {
      const GPUAccelerationStructureTriangleGeometryEXT *pGeometries;
      uint32_t                                            geometryCount;
    } bottomLevel;
    struct {
      const GPUAccelerationStructureInstanceEXT *pInstances;
      uint32_t                                    instanceCount;
    } topLevel;
  };
} GPUAccelerationStructureBuildInfoEXT;

typedef struct GPUAccelerationStructureCreateInfoEXT {
  GPUChainedStruct                 chain;
  const char                      *label;
  GPUAccelerationStructureTypeEXT  type;
  uint64_t                         sizeBytes;
} GPUAccelerationStructureCreateInfoEXT;

typedef struct GPUAccelerationStructureSizesEXT {
  uint64_t accelerationStructureSize;
  uint64_t buildScratchSize;
  uint64_t updateScratchSize;
} GPUAccelerationStructureSizesEXT;

GPU_EXPORT
GPUResult
GPUGetAccelerationStructureSizesEXT(
  GPUDevice                                    *device,
  const GPUAccelerationStructureBuildInfoEXT  *info,
  GPUAccelerationStructureSizesEXT            *outSizes);

GPU_EXPORT
GPUResult
GPUCreateAccelerationStructureEXT(
  GPUDevice                                    *device,
  const GPUAccelerationStructureCreateInfoEXT *info,
  GPUAccelerationStructureEXT                **outStructure);

GPU_EXPORT
void
GPUDestroyAccelerationStructureEXT(GPUAccelerationStructureEXT *structure);

GPU_EXPORT
GPUAccelerationStructurePassEncoderEXT *
GPUBeginAccelerationStructurePassEXT(GPUCommandBuffer *cmdb,
                                     const char       *label);

GPU_EXPORT
GPUResult
GPUBuildAccelerationStructureEXT(
  GPUAccelerationStructurePassEncoderEXT     *pass,
  GPUAccelerationStructureEXT                *dst,
  const GPUAccelerationStructureBuildInfoEXT *info,
  GPUBuffer                                   *scratchBuffer,
  uint64_t                                     scratchOffset);

GPU_EXPORT
void
GPUEndAccelerationStructurePassEXT(
  GPUAccelerationStructurePassEncoderEXT *pass);

#ifdef __cplusplus
}
#endif
#endif /* gpu_ray_h */
