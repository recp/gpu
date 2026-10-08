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

#include "../common.h"
#include "buffer_internal.h"
#include "tensor_internal.h"

static GPUResult
gpu_tensorChain(const GPUChainedStruct *chain,
                GPUStructureType        type,
                size_t                  size) {
  if ((chain->sType != GPU_STRUCTURE_TYPE_NONE && chain->sType != type)
      || (chain->structSize != 0u && chain->structSize < size)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  return chain->pNext ? GPU_ERROR_UNSUPPORTED : GPU_OK;
}

static GPUResult
gpu_tensorDesc(const GPUDevice        *device,
               const GPUTensorDescEXT *desc,
               uint64_t               *outSpanBytes) {
  const GPUTensorUsageFlagsEXT known = GPU_TENSOR_USAGE_COMPUTE_EXT
                                      | GPU_TENSOR_USAGE_RENDER_EXT
                                      | GPU_TENSOR_USAGE_ML_EXT;
  uint64_t                    elements;
  uint64_t                    elementBytes;
  uint64_t                    rowStrideBytes;
  GPUResult                   result;
  uint32_t                    i;

  if (!device || !desc) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = gpu_tensorChain(&desc->chain, GPU_STRUCTURE_TYPE_TENSOR_DESC_EXT, sizeof(*desc));

  if (result != GPU_OK) {
    return result;
  }

  if (desc->usage == 0u || (desc->usage & ~known) != 0u
      || (desc->dataType != GPU_TENSOR_DATA_TYPE_F32_EXT
          && desc->dataType != GPU_TENSOR_DATA_TYPE_F16_EXT)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (desc->rank != 2u || (desc->usage & GPU_TENSOR_USAGE_RENDER_EXT) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!desc->pDimensions || !desc->pStrides) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (i = 0u; i < desc->rank; i++) {
    if (desc->pDimensions[i] == 0u || desc->pDimensions[i] > INT64_MAX
        || desc->pStrides[i] == 0u || desc->pStrides[i] > INT64_MAX) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  if (desc->pStrides[0] != 1u || desc->pStrides[1] < desc->pDimensions[0]) {
    return GPU_ERROR_UNSUPPORTED;
  }

  elementBytes = desc->dataType == GPU_TENSOR_DATA_TYPE_F16_EXT ? 2u : 4u;

  if (desc->pStrides[1] > UINT64_MAX / elementBytes
      || desc->pDimensions[1] - 1u > (UINT64_MAX - desc->pDimensions[0]) / desc->pStrides[1]) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  elements       = (desc->pDimensions[1] - 1u) * desc->pStrides[1] + desc->pDimensions[0];
  rowStrideBytes = desc->pStrides[1] * elementBytes;

  if (elements > UINT64_MAX / elementBytes) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if ((desc->usage & GPU_TENSOR_USAGE_ML_EXT) != 0u && rowStrideBytes % 64u != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  *outSpanBytes = elements * elementBytes;

  return GPU_OK;
}

GPU_EXPORT
GPUResult
GPUGetTensorBufferRequirementsEXT(GPUDevice                      *device,
                                  const GPUTensorDescEXT         *desc,
                                  GPUTensorBufferRequirementsEXT *outRequirements) {
  GPUApi   *api;
  uint64_t  spanBytes;
  GPUResult result;

  if (!outRequirements) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(outRequirements, 0, sizeof(*outRequirements));
  result = gpu_tensorDesc(device, desc, &spanBytes);

  if (result != GPU_OK) {
    return result;
  }

  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_TENSOR_RESOURCES_EXT)
      || !(api = gpuDeviceApi(device)) || !api->tensor.getBufferRequirements) {
    return GPU_ERROR_UNSUPPORTED;
  }

  result = api->tensor.getBufferRequirements(device, desc, spanBytes, outRequirements);

  if (result != GPU_OK) {
    memset(outRequirements, 0, sizeof(*outRequirements));
  }

  return result;
}

GPU_EXPORT
GPUResult
GPUCreateTensorViewEXT(GPUDevice                        *device,
                       const GPUTensorViewCreateInfoEXT *info,
                       GPUTensorEXT                    **outTensor) {
  GPUTensorEXT *tensor;
  GPUApi       *api;
  uint64_t      spanBytes;
  size_t        labelSize;
  GPUResult     result;

  if (!outTensor) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  *outTensor = NULL;

  if (!info) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  result = gpu_tensorChain(&info->chain, GPU_STRUCTURE_TYPE_TENSOR_VIEW_CREATE_INFO_EXT, sizeof(*info));

  if (result != GPU_OK) {
    return result;
  }

  result = gpu_tensorDesc(device, info->pDesc, &spanBytes);

  if (result != GPU_OK) {
    return result;
  }

  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_TENSOR_RESOURCES_EXT)
      || !(api = gpuDeviceApi(device)) || !api->tensor.createView || !api->tensor.destroy) {
    return GPU_ERROR_UNSUPPORTED;
  }

  if (!info->buffer || info->buffer->device != device
      || !gpuBufferHasUsage(info->buffer, GPU_BUFFER_USAGE_STORAGE)
      || !gpuBufferRangeValid(info->buffer, info->offsetBytes, spanBytes)
      || info->offsetBytes % (info->pDesc->dataType == GPU_TENSOR_DATA_TYPE_F16_EXT ? 2u : 4u) != 0u
      || ((info->pDesc->usage & GPU_TENSOR_USAGE_ML_EXT) != 0u && info->offsetBytes != 0u)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (info->buffer->_sparse) {
    return GPU_ERROR_UNSUPPORTED;
  }

  labelSize = info->label ? strlen(info->label) : 0u;

  if (labelSize > SIZE_MAX - sizeof(*tensor) - 1u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  if (!(tensor = calloc(1u, sizeof(*tensor) + labelSize + 1u))) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }

  tensor->device      = device;
  tensor->buffer      = info->buffer;
  tensor->offsetBytes = info->offsetBytes;
  tensor->label       = (char *)(tensor + 1);
  tensor->desc        = *info->pDesc;

  memcpy(tensor->label, info->label ? info->label : "", labelSize + 1u);
  memcpy(tensor->dimensions, info->pDesc->pDimensions, sizeof(tensor->dimensions));
  memcpy(tensor->strides, info->pDesc->pStrides, sizeof(tensor->strides));

  tensor->desc.chain.sType      = GPU_STRUCTURE_TYPE_TENSOR_DESC_EXT;
  tensor->desc.chain.structSize = sizeof(tensor->desc);
  tensor->desc.chain.pNext      = NULL;
  tensor->desc.pDimensions      = tensor->dimensions;
  tensor->desc.pStrides         = tensor->strides;

  result = api->tensor.createView(tensor, spanBytes);

  if (result != GPU_OK) {
    free(tensor);
    return result;
  }

  *outTensor = tensor;

  return GPU_OK;
}

GPU_EXPORT
const GPUTensorDescEXT*
GPUGetTensorDescEXT(const GPUTensorEXT *tensor) {
  return tensor ? &tensor->desc : NULL;
}

GPU_EXPORT
GPUBuffer*
GPUGetTensorBufferEXT(const GPUTensorEXT *tensor, uint64_t *outOffsetBytes) {
  if (outOffsetBytes) {
    *outOffsetBytes = tensor ? tensor->offsetBytes : 0u;
  }

  return tensor ? tensor->buffer : NULL;
}

GPU_EXPORT
void
GPUDestroyTensorEXT(GPUTensorEXT *tensor) {
  GPUApi *api;

  if (!tensor) {
    return;
  }

  if ((api = gpuDeviceApi(tensor->device)) && api->tensor.destroy) {
    api->tensor.destroy(tensor);
  }

  free(tensor);
}
