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

#ifndef gpu_gpudef_cmdbuff_h
#define gpu_gpudef_cmdbuff_h
#ifdef __cplusplus
extern "C" {
#endif

#include "../common.h"
#include "../gpu.h"

typedef struct GPUQuerySet GPUQuerySet;
typedef struct GPUQuerySetCreateInfo GPUQuerySetCreateInfo;

typedef struct GPUApiCommandBuffer {
  void (*presentDrawable)(GPUCommandBuffer *cmdb, GPUFrame *frame);
  GPUResult (*createQuerySet)(GPUDevice *device,
                              const GPUQuerySetCreateInfo *info,
                              GPUQuerySet *set);
  void (*destroyQuerySet)(GPUQuerySet *set);
  void (*writeTimestamp)(GPUCommandBuffer *cmdb,
                         GPUQuerySet *set,
                         uint32_t queryIndex);
  void (*beginOcclusionQuery)(GPURenderPassEncoder *pass,
                              GPUQuerySet *set,
                              uint32_t queryIndex);
  void (*endOcclusionQuery)(GPURenderPassEncoder *pass,
                            GPUQuerySet *set,
                            uint32_t queryIndex);
  void (*beginPipelineStatisticsQuery)(GPUCommandBuffer *cmdb,
                                       GPUQuerySet *set,
                                       uint32_t queryIndex);
  void (*endPipelineStatisticsQuery)(GPUCommandBuffer *cmdb,
                                     GPUQuerySet *set,
                                     uint32_t queryIndex);
  void (*resolveQuerySet)(GPUCommandBuffer *cmdb,
                          GPUQuerySet *set,
                          uint32_t firstQuery,
                          uint32_t queryCount,
                          GPUBuffer *dstBuffer,
                          uint64_t dstOffset);
} GPUApiCommandBuffer;

#ifdef __cplusplus
}
#endif
#endif /* gpu_gpudef_cmdbuff_h */
