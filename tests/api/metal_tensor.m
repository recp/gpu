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

#include "../../src/backend/mt/common.h"
#include "../../src/api/tensor_internal.h"

int
gpu_test_metal_tensor(const GPUTensorEXT *tensor, bool placed) {
#if MT_HAS_METAL4
  uint8_t                 readback[256];
  NSInteger               origins[] = {0, 0};
  NSInteger               dimensions[2];
  NSInteger               strides[2];
  const GPUTensorDescEXT *desc;
  id<MTLTensor>           native;
  id<MTLBuffer>           buffer;
  MTLTensorExtents       *origin;
  MTLTensorExtents       *extents;
  MTLTensorExtents       *layout;
  const uint8_t          *bytes;
  uint64_t                elementBytes;
  uint64_t                expected;
  uint32_t                row;
  uint32_t                col;
  uint32_t                byte;
  int                     ok;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    @autoreleasepool {
      desc   = GPUGetTensorDescEXT(tensor);
      native = tensor->_priv;
      buffer = tensor->buffer->_priv;

      if (!native || native.buffer != buffer || native.bufferOffset != tensor->offsetBytes
          || native.storageMode != (placed ? MTLStorageModePrivate : MTLStorageModeShared)
          || native.dimensions.rank != 2u || native.strides.rank != 2u
          || ((native.usage & MTLTensorUsageCompute) != 0u) != ((desc->usage & GPU_TENSOR_USAGE_COMPUTE_EXT) != 0u)
          || ((native.usage & MTLTensorUsageMachineLearning) != 0u) != ((desc->usage & GPU_TENSOR_USAGE_ML_EXT) != 0u)
          || native.dataType != (desc->dataType == GPU_TENSOR_DATA_TYPE_F16_EXT
                                  ? MTLTensorDataTypeFloat16 : MTLTensorDataTypeFloat32)
          || strcmp(tensor->label, "tensor view") != 0) {
        return 0;
      }

      for (row = 0u; row < 2u; row++) {
        if ((uint64_t)[native.dimensions extentAtDimensionIndex:row] != desc->pDimensions[row]
            || (uint64_t)[native.strides extentAtDimensionIndex:row] != desc->pStrides[row]) {
          return 0;
        }
      }

      if (placed) {
        return tensor->buffer->_heapOffset != 0u && tensor->offsetBytes == 0u;
      }

      elementBytes  = desc->dataType == GPU_TENSOR_DATA_TYPE_F16_EXT ? 2u : 4u;
      dimensions[0] = (NSInteger)desc->pDimensions[0];
      dimensions[1] = (NSInteger)desc->pDimensions[1];
      strides[0]    = 1;
      strides[1]    = dimensions[0];

      origin  = [[MTLTensorExtents alloc] initWithRank:2u values:origins];
      extents = [[MTLTensorExtents alloc] initWithRank:2u values:dimensions];
      layout  = [[MTLTensorExtents alloc] initWithRank:2u values:strides];
      ok      = origin && extents && layout
                && desc->pDimensions[0] * desc->pDimensions[1] * elementBytes <= sizeof(readback);

      if (ok) {
        [native getBytes:readback strides:layout fromSliceOrigin:origin sliceDimensions:extents];
        bytes = buffer.contents;

        for (row = 0u; row < desc->pDimensions[1]; row++) {
          for (col = 0u; col < desc->pDimensions[0]; col++) {
            for (byte = 0u; byte < elementBytes; byte++) {
              expected = tensor->offsetBytes + (row * desc->pStrides[1] + col) * elementBytes + byte;
              ok       = readback[(row * desc->pDimensions[0] + col) * elementBytes + byte] == bytes[expected] && ok;
            }
          }
        }
      }

      [origin release];
      [extents release];
      [layout release];
      return ok;
    }
  }
#else
  GPU__UNUSED(tensor);
  GPU__UNUSED(placed);
#endif

  return 0;
}
