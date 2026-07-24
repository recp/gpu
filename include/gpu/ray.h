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
#include "bindgroup.h"
#include "buffer.h"
#include "cmdqueue.h"
#include "library.h"
#include "pipeline.h"
#include "stage-io.h"
#include "vertex.h"

typedef struct GPUAccelerationStructureEXT            GPUAccelerationStructureEXT;
typedef struct GPUAccelerationStructurePassEncoderEXT GPUAccelerationStructurePassEncoderEXT;
typedef struct GPURayTracingPipelineEXT                GPURayTracingPipelineEXT;
typedef struct GPUShaderTableEXT                       GPUShaderTableEXT;
typedef struct GPURayTracingPassEncoderEXT             GPURayTracingPassEncoderEXT;

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

typedef enum GPUAccelerationStructureGeometryTypeEXT {
  GPU_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_EXT = 0,
  GPU_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_EXT     = 1
} GPUAccelerationStructureGeometryTypeEXT;

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

typedef struct GPUAccelerationStructureAABBGeometryEXT {
  GPUBuffer                               *buffer;
  uint64_t                                 offset;
  uint32_t                                 count;
  uint32_t                                 stride;
  GPUAccelerationStructureGeometryFlagsEXT flags;
} GPUAccelerationStructureAABBGeometryEXT;

typedef struct GPUAccelerationStructureGeometryEXT {
  union {
    GPUAccelerationStructureTriangleGeometryEXT triangles;
    GPUAccelerationStructureAABBGeometryEXT     aabbs;
  };
  GPUAccelerationStructureGeometryTypeEXT type;
} GPUAccelerationStructureGeometryEXT;

typedef struct GPUAccelerationStructureInstanceEXT {
  GPUAccelerationStructureEXT              *structure;
  float                                     transform[3][4];
  uint32_t                                  hitGroupOffset;
  GPUAccelerationStructureInstanceFlagsEXT  flags;
  uint8_t                                   mask;
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
      const GPUAccelerationStructureGeometryEXT *pGeometries;
      uint32_t                                   geometryCount;
    } bottomLevel;
    struct {
      const GPUAccelerationStructureInstanceEXT *pInstances;
      uint32_t                                   instanceCount;
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
  GPUBuffer                                  *scratchBuffer,
  uint64_t                                    scratchOffset);

GPU_EXPORT
void
GPUEndAccelerationStructurePassEXT(GPUAccelerationStructurePassEncoderEXT *pass);

typedef enum GPURayTracingShaderGroupTypeEXT {
  GPU_RAY_TRACING_SHADER_GROUP_GENERAL_EXT          = 0,
  GPU_RAY_TRACING_SHADER_GROUP_TRIANGLES_HIT_EXT    = 1,
  GPU_RAY_TRACING_SHADER_GROUP_PROCEDURAL_HIT_EXT   = 2
} GPURayTracingShaderGroupTypeEXT;

typedef struct GPURayTracingShaderGroupEXT {
  const char                       *generalEntry;
  const char                       *closestHitEntry;
  const char                       *anyHitEntry;
  const char                       *intersectionEntry;
  GPURayTracingShaderGroupTypeEXT   type;
  GPUShaderStageFlags               generalStage;
} GPURayTracingShaderGroupEXT;

typedef struct GPURayTracingPipelineCreateInfoEXT {
  GPUChainedStruct                    chain;
  const char                         *label;
  GPUShaderLibrary                   *library;
  GPUPipelineLayout                  *layout;
  GPUPipelineCache                   *cache;
  const GPURayTracingShaderGroupEXT  *pGroups;
  uint32_t                            groupCount;
  uint32_t                            maxRecursionDepth;
  uint32_t                            maxPayloadSizeBytes;      /* 0 = infer from shader metadata. */
  uint32_t                            maxHitAttributeSizeBytes; /* 0 = infer from shader metadata. */
} GPURayTracingPipelineCreateInfoEXT;

typedef struct GPUShaderTableRecordEXT {
  uint32_t groupIndex;
} GPUShaderTableRecordEXT;

typedef struct GPUShaderTableCreateInfoEXT {
  GPUChainedStruct               chain;
  const char                    *label;
  GPURayTracingPipelineEXT      *pipeline;
  const GPUShaderTableRecordEXT *pRayGenerationRecord;
  const GPUShaderTableRecordEXT *pMissRecords;
  const GPUShaderTableRecordEXT *pHitGroupRecords;
  const GPUShaderTableRecordEXT *pCallableRecords;
  uint32_t                       missRecordCount;
  uint32_t                       hitGroupRecordCount;
  uint32_t                       callableRecordCount;
} GPUShaderTableCreateInfoEXT;

GPU_EXPORT
GPUResult
GPUCreateRayTracingPipelineEXT(GPUDevice                                *device,
                               const GPURayTracingPipelineCreateInfoEXT *info,
                               GPURayTracingPipelineEXT                **outPipeline);

GPU_EXPORT
void
GPUDestroyRayTracingPipelineEXT(GPURayTracingPipelineEXT *pipeline);

GPU_EXPORT
GPUResult
GPUCreateShaderTableEXT(GPUDevice                         *device,
                        const GPUShaderTableCreateInfoEXT *info,
                        GPUShaderTableEXT                **outTable);

GPU_EXPORT
void
GPUDestroyShaderTableEXT(GPUShaderTableEXT *table);

GPU_EXPORT
GPURayTracingPassEncoderEXT *
GPUBeginRayTracingPassEXT(GPUCommandBuffer *cmdb, const char *label);

GPU_EXPORT
void
GPUBindRayTracingPipelineEXT(GPURayTracingPassEncoderEXT *pass,
                             GPURayTracingPipelineEXT    *pipeline);

GPU_EXPORT
void
GPUBindRayTracingGroupEXT(GPURayTracingPassEncoderEXT *pass,
                          uint32_t                      groupIndex,
                          GPUBindGroup                 *group,
                          uint32_t                      dynamicOffsetCount,
                          const uint32_t               *pDynamicOffsets);

GPU_EXPORT
void
GPUDispatchRaysEXT(GPURayTracingPassEncoderEXT *pass,
                   GPUShaderTableEXT           *table,
                   uint32_t                     width,
                   uint32_t                     height,
                   uint32_t                     depth);

GPU_EXPORT
void
GPUEndRayTracingPassEXT(GPURayTracingPassEncoderEXT *pass);

#ifdef __cplusplus
}
#endif
#endif /* gpu_ray_h */
