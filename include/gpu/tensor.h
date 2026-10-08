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

#ifndef gpu_tensor_h
#define gpu_tensor_h
#ifdef __cplusplus
extern "C" {
#endif

#include "buffer.h"

typedef struct GPUTensorEXT GPUTensorEXT;

typedef enum GPUTensorDataTypeEXT {
  GPU_TENSOR_DATA_TYPE_F32_EXT = 0,
  GPU_TENSOR_DATA_TYPE_F16_EXT = 1
} GPUTensorDataTypeEXT;

typedef uint32_t GPUTensorUsageFlagsEXT;
enum {
  GPU_TENSOR_USAGE_COMPUTE_EXT = 1u << 0,
  GPU_TENSOR_USAGE_RENDER_EXT  = 1u << 1,
  GPU_TENSOR_USAGE_ML_EXT      = 1u << 2
};

/* dimensions and explicit strides count elements, with axis0 innermost. */
typedef struct GPUTensorDescEXT {
  GPUChainedStruct       chain;
  const uint64_t        *pDimensions;
  const uint64_t        *pStrides;
  GPUTensorDataTypeEXT   dataType;
  GPUTensorUsageFlagsEXT usage;
  uint32_t               rank;
} GPUTensorDescEXT;

/* buffer data-span requirements; heap placement uses buffer memory requirements. */
typedef struct GPUTensorBufferRequirementsEXT {
  uint64_t            sizeBytes;
  uint64_t            offsetAlignmentBytes;
  GPUBufferUsageFlags requiredBufferUsage;
  bool                requiresZeroOffset;
} GPUTensorBufferRequirementsEXT;

typedef struct GPUTensorViewCreateInfoEXT {
  GPUChainedStruct        chain;
  const char             *label;
  GPUBuffer              *buffer;
  const GPUTensorDescEXT *pDesc;
  uint64_t                offsetBytes;
} GPUTensorViewCreateInfoEXT;

GPU_EXPORT
GPUResult
GPUGetTensorBufferRequirementsEXT(GPUDevice                      *device,
                                  const GPUTensorDescEXT         *desc,
                                  GPUTensorBufferRequirementsEXT *outRequirements);

/* copies metadata; the buffer must outlive the view and all recorded/submitted uses. */
GPU_EXPORT
GPUResult
GPUCreateTensorViewEXT(GPUDevice                        *device,
                       const GPUTensorViewCreateInfoEXT *info,
                       GPUTensorEXT                    **outTensor);

/* returns immutable view-owned metadata, valid until view destruction. */
GPU_EXPORT
const GPUTensorDescEXT*
GPUGetTensorDescEXT(const GPUTensorEXT *tensor);

GPU_EXPORT
GPUBuffer*
GPUGetTensorBufferEXT(const GPUTensorEXT *tensor, uint64_t *outOffsetBytes);

GPU_EXPORT
void
GPUDestroyTensorEXT(GPUTensorEXT *tensor);

#ifdef __cplusplus
}
#endif
#endif /* gpu_tensor_h */
