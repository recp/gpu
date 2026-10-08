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

#include "test.h"
#include "../../src/api/buffer_internal.h"

typedef struct TensorInvalidCase {
  uint64_t               dimensions[2];
  uint64_t               strides[2];
  GPUTensorUsageFlagsEXT usage;
  uint32_t               rank;
  GPUResult              result;
} TensorInvalidCase;

static const TensorInvalidCase invalid_cases[] = {
  {{0u, 3u},          {1u, 16u},        GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_INVALID_ARGUMENT},
  {{UINT64_MAX, 3u},  {1u, 16u},        GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_INVALID_ARGUMENT},
  {{4u, 3u},          {1u, INT64_MAX},  GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_INVALID_ARGUMENT},
  {{1u, INT64_MAX},   {1u, 1u},         GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_INVALID_ARGUMENT},
  {{4u, 3u},          {0u, 16u},        GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_INVALID_ARGUMENT},
  {{4u, 3u},          {1u, 16u},        0u,                           2u, GPU_ERROR_INVALID_ARGUMENT},
  {{4u, 3u},          {1u, 16u},        1u << 31,                     2u, GPU_ERROR_INVALID_ARGUMENT},
  {{4u, 3u},          {1u, 16u},        GPU_TENSOR_USAGE_RENDER_EXT,  2u, GPU_ERROR_UNSUPPORTED},
  {{4u, 3u},          {1u, 16u},        GPU_TENSOR_USAGE_COMPUTE_EXT, 1u, GPU_ERROR_UNSUPPORTED},
  {{4u, 3u},          {2u, 16u},        GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_UNSUPPORTED},
  {{4u, 3u},          {1u, 3u},         GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_UNSUPPORTED},
  {{4u, 3u},          {1u, 5u},         GPU_TENSOR_USAGE_ML_EXT,      2u, GPU_ERROR_UNSUPPORTED},
  {{1ull << 48, 2u},  {1u, 1ull << 48}, GPU_TENSOR_USAGE_COMPUTE_EXT, 2u, GPU_ERROR_UNSUPPORTED}
};

static const GPUTensorUsageFlagsEXT tensor_usages[] = {
  GPU_TENSOR_USAGE_COMPUTE_EXT,
  GPU_TENSOR_USAGE_ML_EXT,
  GPU_TENSOR_USAGE_COMPUTE_EXT | GPU_TENSOR_USAGE_ML_EXT
};

static const char *tensor_procs[] = {
  "GPUGetTensorBufferRequirementsEXT",
  "GPUCreateTensorViewEXT",
  "GPUGetTensorDescEXT",
  "GPUGetTensorBufferEXT",
  "GPUDestroyTensorEXT"
};

#if GPU_TEST_METAL_TENSORS
int
gpu_test_metal_tensor(const GPUTensorEXT *tensor, bool placed);
#endif

static int
expect_query(GPUDevice *device, const GPUTensorDescEXT *desc, GPUResult expected) {
  GPUTensorBufferRequirementsEXT requirements;
  const uint8_t                  zero[sizeof(requirements)] = {0};
  GPUResult                      result;

  memset(&requirements, 0xff, sizeof(requirements));
  result = GPUGetTensorBufferRequirementsEXT(device, desc, &requirements);

  if (result != expected || (result != GPU_OK && memcmp(&requirements, zero, sizeof(requirements)) != 0)) {
    fprintf(stderr, "tensor query: expected=%d actual=%d\n", expected, result);
    return 0;
  }

  return 1;
}

static int
expect_view(GPUDevice *device, const GPUTensorViewCreateInfoEXT *info, GPUResult expected) {
  GPUTensorEXT *tensor;
  GPUResult     result;

  tensor = (GPUTensorEXT *)(uintptr_t)1u;
  result = GPUCreateTensorViewEXT(device, info, &tensor);

  if (result != expected || tensor != NULL) {
    fprintf(stderr, "tensor view: expected=%d actual=%d output=%p\n", expected, result, (void *)tensor);

    if (result == GPU_OK) {
      GPUDestroyTensorEXT(tensor);
    }

    return 0;
  }

  return 1;
}

static int
check_invalid(GPUDevice *device) {
  GPUDevice                  otherDevice;
  GPUBuffer                  otherBuffer;
  GPUBufferCreateInfo        bufferInfo   = {0};
  GPUTensorViewCreateInfoEXT info         = {0};
  GPUTensorDescEXT           desc         = {0};
  GPUChainedStruct           unknown      = {0};
  uint64_t                   dimensions[] = {4u, 3u};
  uint64_t                   strides[]    = {1u, 16u};
  GPUBuffer                 *buffer       = NULL;
  uint32_t                   i;
  int                        ok;

  desc.pDimensions = dimensions;
  desc.pStrides    = strides;
  desc.dataType    = GPU_TENSOR_DATA_TYPE_F32_EXT;
  desc.usage       = GPU_TENSOR_USAGE_ML_EXT;
  desc.rank        = 2u;

  bufferInfo.sizeBytes = 2048u;
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE;

  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK) {
    return 0;
  }

  ok = 1;

  for (i = 0u; i < GPU_ARRAY_LEN(invalid_cases); i++) {
    desc.pDimensions = invalid_cases[i].dimensions;
    desc.pStrides    = invalid_cases[i].strides;
    desc.usage       = invalid_cases[i].usage;
    desc.rank        = invalid_cases[i].rank;

    ok = expect_query(device, &desc, invalid_cases[i].result) && ok;
  }

  desc.pDimensions = dimensions;
  desc.pStrides    = strides;
  desc.usage       = GPU_TENSOR_USAGE_ML_EXT;
  desc.rank        = 2u;
  desc.dataType    = (GPUTensorDataTypeEXT)99;

  ok = expect_query(device, &desc, GPU_ERROR_INVALID_ARGUMENT) && ok;

  desc.dataType         = GPU_TENSOR_DATA_TYPE_F32_EXT;
  desc.pStrides         = NULL;
  ok = expect_query(device, &desc, GPU_ERROR_INVALID_ARGUMENT) && ok;

  desc.pStrides         = strides;
  desc.chain.structSize = 1u;
  ok = expect_query(device, &desc, GPU_ERROR_INVALID_ARGUMENT) && ok;

  desc.chain.structSize = 0u;
  desc.chain.pNext      = &unknown;
  unknown.pNext         = &unknown;
  ok = expect_query(device, &desc, GPU_ERROR_UNSUPPORTED) && ok;

  desc.chain.pNext      = NULL;

  info.buffer = buffer;
  info.pDesc  = &desc;

  info.offsetBytes = 64u;
  ok = expect_view(device, &info, GPU_ERROR_INVALID_ARGUMENT) && ok;

  info.offsetBytes = UINT64_MAX;
  ok = expect_view(device, &info, GPU_ERROR_INVALID_ARGUMENT) && ok;

  info.offsetBytes = 0u;
  info.chain.pNext = &unknown;
  ok = expect_view(device, &info, GPU_ERROR_UNSUPPORTED) && ok;

  info.chain.pNext = NULL;
  info.chain.sType = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  ok = expect_view(device, &info, GPU_ERROR_INVALID_ARGUMENT) && ok;

  info.chain.sType = GPU_STRUCTURE_TYPE_NONE;

  otherDevice        = *device;
  otherBuffer        = *buffer;
  otherBuffer.device = &otherDevice;
  info.buffer        = &otherBuffer;

  ok = expect_view(device, &info, GPU_ERROR_INVALID_ARGUMENT) && ok;

  otherBuffer.device    = device;
  otherBuffer.usage     = GPU_BUFFER_USAGE_COPY_SRC;
  ok = expect_view(device, &info, GPU_ERROR_INVALID_ARGUMENT) && ok;

  otherBuffer.usage     = buffer->usage;
  otherBuffer.sizeBytes = 1u;
  ok = expect_view(device, &info, GPU_ERROR_INVALID_ARGUMENT) && ok;

  info.buffer           = buffer;
  desc.usage            = GPU_TENSOR_USAGE_COMPUTE_EXT;
  info.offsetBytes      = 1u;
  ok = expect_view(device, &info, GPU_ERROR_INVALID_ARGUMENT) && ok;

  GPUDestroyBuffer(buffer);

  return ok;
}

static int
check_view(GPUDevice              *device,
           GPUTensorDataTypeEXT    dataType,
           GPUTensorUsageFlagsEXT  usage,
           bool                    padded,
           uint64_t                offset,
           bool                    placed) {
  uint8_t                        payload[1024];
  uint8_t                        readback[1024];
  char                           label[] = "tensor view";
  uint64_t                       dimensions[2];
  uint64_t                       strides[2];
  GPUTensorDescEXT               desc         = {0};
  GPUTensorViewCreateInfoEXT     info         = {0};
  GPUTensorBufferRequirementsEXT requirements = {0};
  GPUBufferCreateInfo            bufferInfo   = {0};
  GPUHeapCreateInfo              heapInfo     = {0};
  GPUMemoryRequirements          memory       = {0};
  const GPUTensorDescEXT        *copied;
  const char                    *phase;
  GPUTensorEXT                  *tensor       = NULL;
  GPUBuffer                     *buffer       = NULL;
  GPUHeap                       *heap         = NULL;
  GPUQueue                      *queue;
  uint64_t                       heapOffset;
  uint64_t                       reportedOffset;
  uint32_t                       width;
  uint32_t                       i;
  int                            ok;

  ok            = 0;
  phase         = "buffer";
  width         = dataType == GPU_TENSOR_DATA_TYPE_F16_EXT ? 32u : 16u;
  dimensions[0] = padded ? 7u : width;
  dimensions[1] = 3u;
  strides[0]    = 1u;
  strides[1]    = width;

  desc.pDimensions = dimensions;
  desc.pStrides    = strides;
  desc.dataType    = dataType;
  desc.usage       = usage;
  desc.rank        = 2u;

  if (GPUGetTensorBufferRequirementsEXT(device, &desc, &requirements) != GPU_OK
      || requirements.sizeBytes < dimensions[0] * 3u * (64u / width)
      || requirements.sizeBytes > sizeof(payload) - 512u
      || requirements.offsetAlignmentBytes == 0u
      || requirements.requiredBufferUsage != GPU_BUFFER_USAGE_STORAGE
      || requirements.requiresZeroOffset != ((usage & GPU_TENSOR_USAGE_ML_EXT) != 0u)) {
    fprintf(stderr, "tensor requirements failed: type=%u usage=%u padded=%d\n", dataType, usage, padded);
    return 0;
  }

  bufferInfo.sizeBytes = requirements.sizeBytes + 512u;
  bufferInfo.usage     = requirements.requiredBufferUsage | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

  if (placed) {
    if (GPUGetBufferMemoryRequirements(device, &bufferInfo, &memory) != GPU_OK) {
      goto done;
    }

    heapOffset                = (memory.sizeBytes + memory.alignmentBytes - 1u) & ~(memory.alignmentBytes - 1u);
    heapInfo.sizeBytes         = heapOffset * 2u;
    heapInfo.compatibilityMask = memory.compatibilityMask;
    heapInfo.usage             = GPU_HEAP_USAGE_PLACED;

    if (GPUCreateHeap(device, &heapInfo, &heap) != GPU_OK
        || GPUCreatePlacedBuffer(device, &bufferInfo, heap, heapOffset, &buffer) != GPU_OK) {
      goto done;
    }
  } else if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK) {
    goto done;
  }

  info.buffer      = buffer;
  info.pDesc       = &desc;
  info.label       = label;
  info.offsetBytes = offset;

  phase = "create";

  if (GPUCreateTensorViewEXT(device, &info, &tensor) != GPU_OK) {
    goto done;
  }

  copied        = GPUGetTensorDescEXT(tensor);
  phase         = "metadata";
  dimensions[0] = 99u;
  strides[1]    = 99u;
  label[0]      = '?';

  if (!copied || copied->pDimensions == dimensions || copied->pStrides == strides
      || copied->pDimensions[0] != (padded ? 7u : width) || copied->pDimensions[1] != 3u
      || copied->pStrides[0] != 1u || copied->pStrides[1] != width
      || copied->chain.sType != GPU_STRUCTURE_TYPE_TENSOR_DESC_EXT
      || copied->chain.structSize != sizeof(*copied) || copied->chain.pNext != NULL
      || GPUGetTensorBufferEXT(tensor, &reportedOffset) != buffer || reportedOffset != offset) {
    goto done;
  }

  for (i = 0u; i < sizeof(payload); i++) {
    payload[i] = (uint8_t)(i * 17u + 3u);
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  phase = "upload";

  if (!queue || GPUQueueWriteBuffer(queue, buffer, 0u, payload, bufferInfo.sizeBytes) != GPU_OK) {
    goto done;
  }

#if GPU_TEST_METAL_TENSORS
  phase = "native alias";

  if (!gpu_test_metal_tensor(tensor, placed)) {
    goto done;
  }
#endif

  GPUDestroyTensorEXT(tensor);
  tensor = NULL;
  phase  = "borrowed buffer";

  /* destroying the view must preserve the borrowed buffer and its guards. */
  if (GPUQueueReadBuffer(queue, buffer, 0u, readback, bufferInfo.sizeBytes) != GPU_OK
      || memcmp(payload, readback, (size_t)bufferInfo.sizeBytes) != 0) {
    goto done;
  }

  ok = 1;

done:
  if (!ok) {
    fprintf(stderr, "tensor case failed: type=%u usage=%u padded=%d offset=%llu placed=%d phase=%s\n",
            dataType, usage, padded, (unsigned long long)offset, placed, phase);
  }

  GPUDestroyTensorEXT(tensor);
  GPUDestroyBuffer(buffer);
  GPUDestroyHeap(heap);
  return ok;
}

int
gpu_test_tensor(GPUDevice *baseDevice) {
  GPUDeviceCreateInfo        deviceInfo   = {0};
  GPUTensorDescEXT           desc         = {0};
  GPUTensorViewCreateInfoEXT info         = {0};
  const GPUFeature           features[]   = {GPU_FEATURE_COMPUTE, GPU_FEATURE_TENSOR_RESOURCES_EXT};
  const GPUFeature           optional[]   = {GPU_FEATURE_PLACED_RESOURCES};
  const uint64_t             dimensions[] = {4u, 3u};
  const uint64_t             strides[]    = {1u, 16u};
  GPUDevice                 *device       = NULL;
  uint64_t                   offset;
  GPUResult                  result;
  uint32_t                   type;
  uint32_t                   usage;
  uint32_t                   padded;
  uint32_t                   checks;
  uint32_t                   i;
  int                        ok;

  offset = 99u;
  GPUDestroyTensorEXT(NULL);

  if (GPUGetTensorDescEXT(NULL) != NULL || GPUGetTensorBufferEXT(NULL, &offset) != NULL || offset != 0u) {
    return 0;
  }

  desc.pDimensions = dimensions;
  desc.pStrides    = strides;
  desc.usage       = GPU_TENSOR_USAGE_ML_EXT;
  desc.rank        = 2u;
  info.pDesc       = &desc;

  if (!expect_query(baseDevice, &desc, GPU_ERROR_UNSUPPORTED)
      || !expect_query(NULL, &desc, GPU_ERROR_INVALID_ARGUMENT)
      || !expect_view(baseDevice, &info, GPU_ERROR_UNSUPPORTED)) {
    return 0;
  }

  for (i = 0u; i < GPU_ARRAY_LEN(tensor_procs); i++) {
    if (GPUGetProcAddr(baseDevice, tensor_procs[i]) != NULL) {
      return 0;
    }
  }

  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = GPU_ARRAY_LEN(features);
  deviceInfo.optional.pFeatures    = optional;
  deviceInfo.optional.featureCount = GPU_ARRAY_LEN(optional);

  result = gpu_test_create_device(baseDevice->adapter, &deviceInfo, &device);

  if (!GPUIsFeatureSupported(baseDevice->adapter, GPU_FEATURE_TENSOR_RESOURCES_EXT)) {
    if (device) {
      GPUDestroyDevice(device);
    }

    return result == GPU_ERROR_UNSUPPORTED;
  }

  if (result != GPU_OK || !device || !GPUIsFeatureEnabled(device, GPU_FEATURE_TENSOR_RESOURCES_EXT)) {
    GPUDestroyDevice(device);
    return 0;
  }

  ok     = check_invalid(device);
  checks = 0u;

  for (i = 0u; i < GPU_ARRAY_LEN(tensor_procs); i++) {
    ok = GPUGetProcAddr(device, tensor_procs[i]) != NULL && ok;
  }

  for (type = 0u; type < 2u; type++) {
    for (usage = 0u; usage < GPU_ARRAY_LEN(tensor_usages); usage++) {
      for (padded = 0u; padded < 2u; padded++) {
        ok = check_view(device, (GPUTensorDataTypeEXT)type, tensor_usages[usage], padded != 0u, 0u, false) && ok;
        checks++;
      }
    }

    ok = check_view(device, (GPUTensorDataTypeEXT)type, GPU_TENSOR_USAGE_COMPUTE_EXT, true, 256u, false) && ok;
    checks++;

    if (GPUIsFeatureEnabled(device, GPU_FEATURE_PLACED_RESOURCES)) {
      ok = check_view(device, (GPUTensorDataTypeEXT)type, GPU_TENSOR_USAGE_ML_EXT, true, 0u, true) && ok;
      checks++;
    }
  }

  printf("tensor: views=%u negative-layouts=%u native-and-borrowed-buffer=%s\n",
         checks, (unsigned)GPU_ARRAY_LEN(invalid_cases), ok ? "pass" : "fail");
  GPUDestroyDevice(device);
  return ok;
}
