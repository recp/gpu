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
#include "../../api/tensor.h"
#include "../../../api/tensor_internal.h"

#if MT_HAS_METAL4
static MTLTensorDescriptor*
mt_tensorDescriptor(const GPUTensorDescEXT *desc) API_AVAILABLE(macos(26.0), ios(26.0));

static GPUResult
mt_tensorRequirements(GPUDevice                      *device,
                      const GPUTensorDescEXT         *desc,
                      MTLTensorDescriptor            *nativeDesc,
                      uint64_t                        spanBytes,
                      GPUTensorBufferRequirementsEXT *outRequirements) API_AVAILABLE(macos(26.0), ios(26.0));

static MTLTensorDescriptor*
mt_tensorDescriptor(const GPUTensorDescEXT *desc) {
  NSInteger            dimensions[2];
  NSInteger            strides[2];
  MTLTensorDescriptor *nativeDesc;
  MTLTensorExtents    *nativeDimensions;
  MTLTensorExtents    *nativeStrides;
  uint32_t             i;

  for (i = 0u; i < 2u; i++) {
    dimensions[i] = (NSInteger)desc->pDimensions[i];
    strides[i]    = (NSInteger)desc->pStrides[i];
  }

  nativeDesc       = [MTLTensorDescriptor new];
  nativeDimensions = [[MTLTensorExtents alloc] initWithRank:2u values:dimensions];
  nativeStrides    = [[MTLTensorExtents alloc] initWithRank:2u values:strides];

  if (!nativeDesc || !nativeDimensions || !nativeStrides) {
    [nativeDesc release];
    [nativeDimensions release];
    [nativeStrides release];
    return nil;
  }

  nativeDesc.dimensions = nativeDimensions;
  nativeDesc.strides    = nativeStrides;
  nativeDesc.dataType   = desc->dataType == GPU_TENSOR_DATA_TYPE_F16_EXT
                           ? MTLTensorDataTypeFloat16 : MTLTensorDataTypeFloat32;
  nativeDesc.usage      = 0u;

  if ((desc->usage & GPU_TENSOR_USAGE_COMPUTE_EXT) != 0u) {
    nativeDesc.usage |= MTLTensorUsageCompute;
  }

  if ((desc->usage & GPU_TENSOR_USAGE_ML_EXT) != 0u) {
    nativeDesc.usage |= MTLTensorUsageMachineLearning;
  }

  [nativeDimensions release];
  [nativeStrides release];

  return nativeDesc;
}

static GPUResult
mt_tensorRequirements(GPUDevice                      *device,
                      const GPUTensorDescEXT         *desc,
                      MTLTensorDescriptor            *nativeDesc,
                      uint64_t                        spanBytes,
                      GPUTensorBufferRequirementsEXT *outRequirements) {
  MTLSizeAndAlign sizeAndAlign;
  GPUDeviceMT    *deviceMT;
  uint32_t        i;

  deviceMT = device->_priv;

  if (!deviceMT || deviceMT->commandMode != MTCommandMode4) {
    return GPU_ERROR_UNSUPPORTED;
  }

  for (i = 0u; i < desc->rank; i++) {
    if (desc->pDimensions[i] > NSIntegerMax || desc->pStrides[i] > NSIntegerMax) {
      return GPU_ERROR_INVALID_ARGUMENT;
    }
  }

  if (spanBytes > deviceMT->device.maxBufferLength) {
    return GPU_ERROR_UNSUPPORTED;
  }

  sizeAndAlign = [deviceMT->device tensorSizeAndAlignWithDescriptor:nativeDesc];

  if (sizeAndAlign.size < spanBytes || sizeAndAlign.size > deviceMT->device.maxBufferLength
      || sizeAndAlign.align == 0u || (sizeAndAlign.align & (sizeAndAlign.align - 1u)) != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  outRequirements->sizeBytes            = sizeAndAlign.size;
  outRequirements->offsetAlignmentBytes = sizeAndAlign.align;
  outRequirements->requiredBufferUsage  = GPU_BUFFER_USAGE_STORAGE;
  outRequirements->requiresZeroOffset   = (desc->usage & GPU_TENSOR_USAGE_ML_EXT) != 0u;

  return GPU_OK;
}
#endif

static GPUResult
mt_getTensorBufferRequirements(GPUDevice                      *device,
                               const GPUTensorDescEXT         *desc,
                               uint64_t                        spanBytes,
                               GPUTensorBufferRequirementsEXT *outRequirements) {
#if MT_HAS_METAL4
  MTLTensorDescriptor *nativeDesc;
  GPUResult            result;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    @autoreleasepool {
      if (!(nativeDesc = mt_tensorDescriptor(desc))) {
        return GPU_ERROR_OUT_OF_MEMORY;
      }

      result = mt_tensorRequirements(device, desc, nativeDesc, spanBytes, outRequirements);
      [nativeDesc release];

      return result;
    }
  }
#else
  GPU__UNUSED(device);
  GPU__UNUSED(desc);
  GPU__UNUSED(spanBytes);
  GPU__UNUSED(outRequirements);
#endif

  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
mt_createTensorView(GPUTensorEXT *tensor, uint64_t spanBytes) {
#if MT_HAS_METAL4
  GPUTensorBufferRequirementsEXT requirements = {0};
  MTLTensorDescriptor           *nativeDesc;
  id<MTLBuffer>                  buffer;
  id<MTLTensor>                  nativeTensor;
  NSError                       *error;
  GPUResult                      result;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    @autoreleasepool {
      buffer = tensor->buffer->_priv;

      if (!buffer) {
        return GPU_ERROR_BACKEND_FAILURE;
      }

      if (!(nativeDesc = mt_tensorDescriptor(&tensor->desc))) {
        return GPU_ERROR_OUT_OF_MEMORY;
      }

      nativeDesc.resourceOptions = buffer.resourceOptions;

      result = mt_tensorRequirements(tensor->device, &tensor->desc, nativeDesc, spanBytes, &requirements);

      if (result != GPU_OK) {
        [nativeDesc release];
        return result;
      }

      if (tensor->offsetBytes % requirements.offsetAlignmentBytes != 0u
          || !bufferRangeValid(tensor->buffer, tensor->offsetBytes, requirements.sizeBytes)) {
        [nativeDesc release];
        return GPU_ERROR_INVALID_ARGUMENT;
      }

      error = nil;

      nativeTensor = [buffer newTensorWithDescriptor:nativeDesc offset:(NSUInteger)tensor->offsetBytes error:&error];
      [nativeDesc release];

      if (!nativeTensor) {
        return error.code == MTLTensorErrorInvalidDescriptor ? GPU_ERROR_UNSUPPORTED : GPU_ERROR_BACKEND_FAILURE;
      }

#if GPU_BUILD_WITH_DEBUG_MARKERS
      if (deviceDebugMarkersEnabled(tensor->device) && tensor->label[0] != '\0') {
        nativeTensor.label = [NSString stringWithUTF8String:tensor->label];
      }
#endif
      tensor->_priv     = nativeTensor;
      tensor->sizeBytes = requirements.sizeBytes;

      return GPU_OK;
    }
  }
#else
  GPU__UNUSED(tensor);
  GPU__UNUSED(spanBytes);
#endif

  return GPU_ERROR_UNSUPPORTED;
}

static void
mt_destroyTensor(GPUTensorEXT *tensor) {
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    [(id<MTLTensor>)tensor->_priv release];
  }
#else
  GPU__UNUSED(tensor);
#endif
}

GPU_HIDE
void
mt_initTensor(GPUTensorApi *api) {
  api->getBufferRequirements = mt_getTensorBufferRequirements;
  api->createView            = mt_createTensorView;
  api->destroy               = mt_destroyTensor;
}
