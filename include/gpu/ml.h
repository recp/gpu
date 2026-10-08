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

#ifndef gpu_ml_h
#define gpu_ml_h

#include "device.h"
#include "tensor.h"
#include "memory.h"
#include "barrier.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GPUMLModelEXT    GPUMLModelEXT;
typedef struct GPUMLPipelineEXT GPUMLPipelineEXT;
typedef struct GPUMLBindingsEXT GPUMLBindingsEXT;

/* axis0 is innermost; dimensions count elements, never bytes. */
typedef struct GPUMLTensorShapeEXT {
  const uint64_t      *pDimensions;
  GPUTensorDataTypeEXT dataType;
  uint32_t             slot;
  uint32_t             rank;
} GPUMLTensorShapeEXT;

/* trusted model-owner metadata; graph-dependent shape validity must be checked offline. */
typedef struct GPUMLShapeProfileEXT {
  const GPUMLTensorShapeEXT *pInputs;
  uint32_t                   id;
  uint32_t                   inputCount;
} GPUMLShapeProfileEXT;

typedef struct GPUMLModelCreateInfoEXT {
  GPUChainedStruct            chain;
  const char                 *label;
  const char                 *path;
  const char                 *functionName;
  const GPUMLShapeProfileEXT *pProfiles;
  uint32_t                    profileCount;
} GPUMLModelCreateInfoEXT;

typedef struct GPUMLPipelineCreateInfoEXT {
  GPUChainedStruct chain;
  const char      *label;
  GPUMLModelEXT   *model;
  uint32_t         profileId;
} GPUMLPipelineCreateInfoEXT;

/* access uses existing shader-read/write bits for the tensor backing buffer. */
typedef struct GPUMLBindingInfoEXT {
  const char         *name;
  GPUAccessMask       access;
  GPUMLTensorShapeEXT shape;
} GPUMLBindingInfoEXT;

/* immutable pipeline-owned metadata; scratchHeap is ready for GPUCreateHeap. */
typedef struct GPUMLPipelineInfoEXT {
  const GPUMLBindingInfoEXT *pBindings;
  uint32_t                   bindingCount;
  GPUHeapCreateInfo          scratchHeap;
} GPUMLPipelineInfoEXT;

typedef struct GPUMLTensorBindingEXT {
  GPUTensorEXT *tensor;
  uint32_t      slot;
} GPUMLTensorBindingEXT;

typedef struct GPUMLBindingsCreateInfoEXT {
  GPUChainedStruct             chain;
  const char                  *label;
  GPUMLPipelineEXT            *pipeline;
  GPUHeap                     *scratch;
  const GPUMLTensorBindingEXT *pBindings;
  uint32_t                     bindingCount;
} GPUMLBindingsCreateInfoEXT;

/* requires compute, tensor, placement and model features; copies profile metadata.
 * validate each profile for this exact trusted package/function/target offline;
 * native provider aborts are not recoverable errors for unvalidated profiles.
 */
GPU_EXPORT
GPUResult
GPUCreateMLModelEXT(GPUDevice                     *device,
                    const GPUMLModelCreateInfoEXT *info,
                    GPUMLModelEXT                **outModel);

GPU_EXPORT
void
GPUDestroyMLModelEXT(GPUMLModelEXT *model);

/* model and device must outlive pipelines, their bindings and submitted uses. */
GPU_EXPORT
GPUResult
GPUCreateMLPipelineEXT(GPUDevice                        *device,
                       const GPUMLPipelineCreateInfoEXT *info,
                       GPUMLPipelineEXT                **outPipeline);

GPU_EXPORT
const GPUMLPipelineInfoEXT*
GPUGetMLPipelineInfoEXT(const GPUMLPipelineEXT *pipeline);

GPU_EXPORT
void
GPUDestroyMLPipelineEXT(GPUMLPipelineEXT *pipeline);

/* borrows fixed tensor views and scratch until destruction and submission completion. */
GPU_EXPORT
GPUResult
GPUCreateMLBindingsEXT(GPUDevice                        *device,
                       const GPUMLBindingsCreateInfoEXT *info,
                       GPUMLBindingsEXT                **outBindings);

GPU_EXPORT
void
GPUDestroyMLBindingsEXT(GPUMLBindingsEXT *bindings);

/* encodes one complete network pass; barriers and submission remain explicit. */
GPU_EXPORT
GPUResult
GPUEncodeMLEXT(GPUCommandBuffer *cmdb, GPUMLBindingsEXT *bindings);

#ifdef __cplusplus
}
#endif
#endif /* gpu_ml_h */
