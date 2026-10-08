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
#include "../../src/api/ml_internal.h"
#include "../../src/api/memory_internal.h"
#include "../../src/api/cmdqueue_internal.h"

#define ROUNDS 3u

typedef struct MLShape {
  uint32_t  m;
  uint32_t  n;
  uint32_t  k;
  uint32_t  aStride;
  uint32_t  bStride;
  uint32_t  cStride;
  uint32_t  round;
} MLShape;

static const char *mlProcs[] = {
  "GPUCreateMLModelEXT", "GPUDestroyMLModelEXT",
  "GPUCreateMLPipelineEXT", "GPUGetMLPipelineInfoEXT", "GPUDestroyMLPipelineEXT",
  "GPUCreateMLBindingsEXT", "GPUDestroyMLBindingsEXT", "GPUEncodeMLEXT"
};

static int
make_compute(GPUDevice           *device,
             GPUShaderLibrary   **outLibrary,
             GPUBindGroupLayout **outGroup,
             GPUPipelineLayout  **outLayout,
             GPUComputePipeline **prepare,
             GPUComputePipeline **consume) {
  GPUBindGroupLayoutEntry      entries[4]   = {0};
  GPUBindGroupLayoutCreateInfo groupInfo    = {0};
  GPUPipelineLayoutCreateInfo  layoutInfo   = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUShaderLibraryCreateInfo   libraryInfo  = {0};
  void                        *source;
  uint64_t                     size;
  uint32_t                     i;
  int                          ok;

  if (!(source = gpu_test_read_file(GPU_TEST_ML_ROOT "/timeline.metal", &size))) {
    return 0;
  }

  libraryInfo.sourceData = source;
  libraryInfo.sourceSize = size;
  libraryInfo.sourceKind = GPU_SHADER_SOURCE_MSL_TEXT;
  ok                     = GPUCreateShaderLibrary(device, &libraryInfo, outLibrary) == GPU_OK;
  free(source);

  if (!ok) {
    return 0;
  }

  for (i = 0u; i < GPU_ARRAY_LEN(entries); i++) {
    entries[i].binding     = i;
    entries[i].arrayCount  = 1u;
    entries[i].bindingType = GPU_BINDING_STORAGE_BUFFER;
    entries[i].visibility  = GPU_SHADER_STAGE_COMPUTE_BIT;
  }

  groupInfo.pEntries   = entries;
  groupInfo.entryCount = GPU_ARRAY_LEN(entries);

  if (GPUCreateBindGroupLayout(device, &groupInfo, outGroup) != GPU_OK) {
    return 0;
  }

  layoutInfo.ppBindGroupLayouts    = outGroup;
  layoutInfo.bindGroupLayoutCount  = 1u;
  layoutInfo.pushConstantSizeBytes = sizeof(MLShape);
  layoutInfo.pushConstantStages    = GPU_SHADER_STAGE_COMPUTE_BIT;

  if (GPUCreatePipelineLayout(device, &layoutInfo, outLayout) != GPU_OK) {
    return 0;
  }

  pipelineInfo.library    = *outLibrary;
  pipelineInfo.layout     = *outLayout;
  pipelineInfo.entryPoint = "prepare";

  if (GPUCreateComputePipeline(device, &pipelineInfo, prepare) != GPU_OK) {
    return 0;
  }

  pipelineInfo.entryPoint = "consume";

  return GPUCreateComputePipeline(device, &pipelineInfo, consume) == GPU_OK;
}

static int
invalid_bindings(GPUDevice *device, GPUMLBindingsCreateInfoEXT *info, GPUBuffer *backing) {
  GPUTensorViewCreateInfoEXT   viewInfo = {0};
  GPUMLTensorBindingEXT        items[3];
  GPUHeap                      small;
  GPUHeap                     *scratch;
  const GPUMLTensorBindingEXT *original;
  GPUMLBindingsEXT            *bad;
  GPUTensorEXT                *alias = NULL;
  GPUResult                    result;
  int                          ok;

  original        = info->pBindings;
  scratch         = info->scratch;
  small           = *scratch;
  small.sizeBytes = 0u;
  info->scratch   = &small;
  bad             = (GPUMLBindingsEXT *)(uintptr_t)1u;
  result          = GPUCreateMLBindingsEXT(device, info, &bad);
  info->scratch   = scratch;

  if (result != GPU_ERROR_INVALID_ARGUMENT || bad) {
    return 0;
  }

  memcpy(items, original, sizeof(items));
  items[1].slot   = items[0].slot;
  info->pBindings = items;
  result          = GPUCreateMLBindingsEXT(device, info, &bad);
  info->pBindings = original;

  if (result != GPU_ERROR_INVALID_ARGUMENT || bad) {
    return 0;
  }

  viewInfo.buffer = backing;
  viewInfo.pDesc  = GPUGetTensorDescEXT(original[2].tensor);

  if (GPUCreateTensorViewEXT(device, &viewInfo, &alias) != GPU_OK) {
    return 0;
  }

  memcpy(items, original, sizeof(items));
  items[2].tensor = alias;
  info->pBindings = items;
  result          = GPUCreateMLBindingsEXT(device, info, &bad);
  info->pBindings = original;
  ok              = result == GPU_ERROR_INVALID_ARGUMENT && !bad;
  GPUDestroyMLBindingsEXT(bad);
  GPUDestroyTensorEXT(alias);
  return ok;
}

static int
placement_alias(GPUDevice *device, GPUMLBindingsCreateInfoEXT *info) {
  GPUBufferCreateInfo            bufferInfo         = {0};
  GPUMemoryRequirements          requirements       = {0};
  GPUHeapCreateInfo              heapInfo           = {0};
  GPUTensorViewCreateInfoEXT     viewInfo           = {0};
  GPUTensorBufferRequirementsEXT tensorRequirements = {0};
  GPUMLTensorBindingEXT          items[3];
  GPUBuffer                     *buffers[2] = {NULL};
  GPUTensorEXT                  *tensors[3] = {NULL};
  GPUHeap                       *heap       = NULL;
  GPUHeap                       *scratch;
  const GPUMLTensorBindingEXT   *original;
  GPUMLBindingsEXT              *bindings   = NULL;
  GPUResult                      result;
  uint32_t                       i;
  int                            ok = 0;

  original         = info->pBindings;
  scratch          = info->scratch;
  bufferInfo.usage = GPU_BUFFER_USAGE_STORAGE;

  for (i = 0u; i < 3u; i++) {
    if (GPUGetTensorBufferRequirementsEXT(device,
                                         GPUGetTensorDescEXT(original[i].tensor),
                                         &tensorRequirements) != GPU_OK) {
      goto cleanup;
    }

    if (tensorRequirements.sizeBytes > bufferInfo.sizeBytes) {
      bufferInfo.sizeBytes = tensorRequirements.sizeBytes;
    }
  }

  if (GPUGetBufferMemoryRequirements(device, &bufferInfo, &requirements) != GPU_OK) {
    goto cleanup;
  }

  heapInfo.sizeBytes         = requirements.sizeBytes;
  heapInfo.compatibilityMask = requirements.compatibilityMask;

  if (GPUCreateHeap(device, &heapInfo, &heap) != GPU_OK) {
    goto cleanup;
  }

  for (i = 0u; i < 2u; i++) {
    if (GPUCreatePlacedBuffer(device, &bufferInfo, heap, 0u, &buffers[i]) != GPU_OK) {
      goto cleanup;
    }
  }

  for (i = 0u; i < 3u; i++) {
    viewInfo.buffer = buffers[i == 0u ? 0u : 1u];
    viewInfo.pDesc  = GPUGetTensorDescEXT(original[i].tensor);

    if (GPUCreateTensorViewEXT(device, &viewInfo, &tensors[i]) != GPU_OK) {
      goto cleanup;
    }
  }

  memcpy(items, original, sizeof(items));
  items[0].tensor = tensors[0];
  items[1].tensor = tensors[1];
  info->pBindings = items;

  /* the two read-only inputs may overlap without an overlapping output. */
  if (GPUCreateMLBindingsEXT(device, info, &bindings) != GPU_OK) {
    goto cleanup;
  }

  GPUDestroyMLBindingsEXT(bindings);
  bindings        = NULL;
  items[2].tensor = tensors[2];
  result          = GPUCreateMLBindingsEXT(device, info, &bindings);

  if (result != GPU_ERROR_INVALID_ARGUMENT || bindings) {
    goto cleanup;
  }

  /* a sufficiently large scratch heap still cannot back a bound tensor. */
  items[2]      = original[2];
  info->scratch = heap;
  result        = GPUCreateMLBindingsEXT(device, info, &bindings);
  ok            = result == GPU_ERROR_INVALID_ARGUMENT && !bindings;

cleanup:
  info->scratch   = scratch;
  info->pBindings = original;
  GPUDestroyMLBindingsEXT(bindings);

  for (i = 0u; i < 3u; i++) {
    GPUDestroyTensorEXT(tensors[i]);
  }

  for (i = 0u; i < 2u; i++) {
    GPUDestroyBuffer(buffers[i]);
  }

  GPUDestroyHeap(heap);
  return ok;
}

static int
host_binding(GPUDevice *device, GPUMLBindingsCreateInfoEXT *info) {
  GPUMLTensorBindingEXT          items[3];
  GPUBufferHostMemoryEXT         host       = {0};
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUTensorViewCreateInfoEXT     viewInfo   = {0};
  GPUTensorBufferRequirementsEXT requirements;
  GPUBuffer                     *buffers[5] = {NULL};
  GPUTensorEXT                  *tensors[6] = {NULL};
  const GPUMLTensorBindingEXT   *original;
  GPUMLBindingsEXT              *bindings   = NULL;
  uint8_t                       *allocation = NULL;
  uint8_t                       *bytes;
  uint64_t                       extent;
  size_t                         page;
  GPUResult                      result;
  uint32_t                       i;
  uint32_t                       slot;
  int                            ok         = 0;

  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_BUFFER_HOST_MEMORY_EXT))
    return 1;

  original = info->pBindings;
  page     = gpu_test_host_page_size();
  extent   = 0u;

  for (i = 0u; i < 3u; i++) {
    if (GPUGetTensorBufferRequirementsEXT(device,
                                         GPUGetTensorDescEXT(original[i].tensor),
                                         &requirements) != GPU_OK)
      goto cleanup;

    if (requirements.sizeBytes > extent)
      extent = requirements.sizeBytes;
  }

  if (page == 0u || page > SIZE_MAX / 4u || extent > (SIZE_MAX - 4u * page) / 3u)
    goto cleanup;

  extent = (extent + page - 1u) / page * page;

  if (!(allocation = malloc((size_t)(3u * extent) + page)))
    goto cleanup;

  bytes = (uint8_t *)(((uintptr_t)allocation + page - 1u) / page * page);
  memset(bytes, 0xa5, (size_t)(3u * extent));

  host.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_HOST_MEMORY_EXT;
  host.chain.structSize = sizeof(host);
  host.allocationSize   = 2u * extent;

  bufferInfo.sizeBytes = 2u * extent;
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE;

  for (i = 0u; i < 5u; i++) {
    if (i == 3u) {
      bufferInfo.chain.pNext = NULL;
    } else {
      bufferInfo.chain.pNext = &host;
      host.pData            = i == 4u ? buffers[3]->_hostMemory : bytes + (i == 2u ? extent : 0u);
    }

    if (GPUCreateBuffer(device, &bufferInfo, &buffers[i]) != GPU_OK)
      goto cleanup;
  }

  for (i = 0u; i < 6u; i++) {
    slot            = i < 3u ? i : (i == 3u ? 0u : 2u);
    viewInfo.buffer = buffers[i == 5u ? 1u : i];
    viewInfo.pDesc  = GPUGetTensorDescEXT(original[slot].tensor);

    if (GPUCreateTensorViewEXT(device, &viewInfo, &tensors[i]) != GPU_OK)
      goto cleanup;
  }

  memcpy(items, original, sizeof(items));
  info->pBindings = items;

  for (i = 0u; i < 3u; i++) {
    items[i].tensor = tensors[i];
  }

  /* reads alias; overlapping allocations still have disjoint writable tensor spans. */
  if ((result = GPUCreateMLBindingsEXT(device, info, &bindings)) != GPU_OK) {
    fprintf(stderr, "ML disjoint host binding failed: result=%d\n", result);
    goto cleanup;
  }

  GPUDestroyMLBindingsEXT(bindings);
  bindings        = NULL;
  items[1].tensor = original[1].tensor;
  items[2].tensor = tensors[5];

  if ((result = GPUCreateMLBindingsEXT(device, info, &bindings)) != GPU_ERROR_INVALID_ARGUMENT || bindings) {
    fprintf(stderr, "ML writable import/import overlap was not rejected: result=%d\n", result);
    goto cleanup;
  }

  /* an import aliases ordinary shared storage through a different buffer handle. */
  items[0].tensor = tensors[3];
  items[2].tensor = tensors[4];

  if ((result = GPUCreateMLBindingsEXT(device, info, &bindings)) != GPU_ERROR_INVALID_ARGUMENT || bindings) {
    fprintf(stderr, "ML writable import/shared overlap was not rejected: result=%d\n", result);
    goto cleanup;
  }

  items[2].tensor = tensors[2];

  if (GPUCreateMLBindingsEXT(device, info, &bindings) != GPU_OK)
    goto cleanup;

  printf("ML host aliases: read/read allowed, disjoint spans allowed, writable import/import and import/shared rejected\n");
  ok = 1;

cleanup:
  info->pBindings = original;
  GPUDestroyMLBindingsEXT(bindings);

  for (i = 0u; i < 6u; i++) {
    GPUDestroyTensorEXT(tensors[i]);
  }

  /* the imported wrapper must be released before its ordinary shared owner. */
  for (i = 5u; i > 0u; i--) {
    GPUDestroyBuffer(buffers[i - 1u]);
  }

  free(allocation);
  return ok;
}

static int
invalid_model(GPUDevice *device, const GPUMLModelCreateInfoEXT *info, GPUResult expected) {
  GPUMLModelEXT *model;
  GPUResult      result;

  model  = (GPUMLModelEXT *)(uintptr_t)1u;
  result = GPUCreateMLModelEXT(device, info, &model);

  if (result != expected || model) {
    if (result == GPU_OK) {
      GPUDestroyMLModelEXT(model);
    }

    return 0;
  }

  return 1;
}

static int
run_profile(GPUDevice          *device,
            GPUMLModelEXT      *model,
            uint32_t            profileId,
            MLShape             shape,
            GPUBindGroupLayout *groupLayout,
            GPUComputePipeline *prepare,
            GPUComputePipeline *consume,
            bool                imported) {
  GPUMLPipelineCreateInfoEXT     pipelineInfo    = {0};
  GPUMLBindingsCreateInfoEXT     bindingInfo     = {0};
  GPUTensorDescEXT               desc            = {0};
  GPUTensorViewCreateInfoEXT     viewInfo        = {0};
  GPUTensorBufferRequirementsEXT requirements[3] = {0};
  GPUBufferCreateInfo            bufferInfo      = {0};
  GPUBufferHostMemoryEXT         host            = {0};
  GPUBindGroupEntry              entries[4]      = {0};
  GPUBindGroupCreateInfo         groupInfo       = {0};
  GPUMLTensorBindingEXT          tensorBindings[3];
  GPUBarrierBatch                barrier         = {0};
  GPUQueueSubmitInfo             submit          = {0};
  GPUFenceCreateInfo             fenceInfo       = {0};
  GPUCommandBuffer               invalid         = {0};
  uint64_t                       strides[3][2];
  uint64_t                       sizes[4];
  uint8_t                       *allocations[4]  = {NULL};
  uint8_t                       *hostData[4]     = {NULL};
  GPUBuffer                     *buffers[4]      = {NULL};
  GPUTensorEXT                  *tensors[3]      = {NULL};
  GPUMLPipelineEXT              *pipeline        = NULL;
  GPUMLPipelineEXT              *cached          = NULL;
  GPUMLBindingsEXT              *bindings        = NULL;
  GPUBindGroup                  *group           = NULL;
  GPUCommandBuffer              *cmdb            = NULL;
  GPUTransferPassEncoder        *transfer        = NULL;
  GPUComputePassEncoder         *pass            = NULL;
  GPUFence                      *fence           = NULL;
  GPUQueue                      *queue;
  const GPUMLPipelineInfoEXT    *info;
  uint8_t                       *data            = NULL;
  float                         *values;
  uint64_t                       offset;
  uint64_t                       extent;
  uint64_t                       pageGuards      = 0u;
  size_t                         page;
  uint32_t                       i;
  uint32_t                       j;
  uint32_t                       row;
  uint32_t                       column;
  uint32_t                       k;
  uint32_t                       count;
  int                            expected;
  int                            a;
  int                            b;
  int                            ok              = 0;

  pipelineInfo.model     = model;
  pipelineInfo.profileId = profileId;

  if (GPUCreateMLPipelineEXT(device, &pipelineInfo, &pipeline) != GPU_OK
      || GPUCreateMLPipelineEXT(device, &pipelineInfo, &cached) != GPU_OK
      || pipeline->profile->_priv != cached->profile->_priv
      || GPUGetMLPipelineInfoEXT(pipeline) != GPUGetMLPipelineInfoEXT(cached)) {
    goto cleanup;
  }

  info  = GPUGetMLPipelineInfoEXT(pipeline);
  queue = GPUGetCommandQueue(device, GPU_QUEUE_COMPUTE);

  if (!queue || info->bindingCount != 3u || GPUCreateHeap(device, &info->scratchHeap, &bindingInfo.scratch) != GPU_OK) {
    goto cleanup;
  }

  for (i = 0u; i < 3u; i++) {
    if (info->pBindings[i].shape.slot != i || info->pBindings[i].shape.rank != 2u
        || info->pBindings[i].shape.dataType != GPU_TENSOR_DATA_TYPE_F32_EXT
        || info->pBindings[i].access != (i == 2u ? GPU_ACCESS_SHADER_WRITE : GPU_ACCESS_SHADER_READ)) {
      goto cleanup;
    }

    strides[i][0]    = 1u;
    strides[i][1]    = (info->pBindings[i].shape.pDimensions[0] + 15u) & ~UINT64_C(15);
    desc.pDimensions = info->pBindings[i].shape.pDimensions;
    desc.pStrides    = strides[i];
    desc.rank        = 2u;
    desc.dataType    = GPU_TENSOR_DATA_TYPE_F32_EXT;
    desc.usage       = GPU_TENSOR_USAGE_ML_EXT | GPU_TENSOR_USAGE_COMPUTE_EXT;

    if (GPUGetTensorBufferRequirementsEXT(device, &desc, &requirements[i]) != GPU_OK) {
      goto cleanup;
    }

  }

  page = gpu_test_host_page_size();

  shape.aStride = (uint32_t)strides[0][1];
  shape.bStride = (uint32_t)strides[1][1];
  shape.cStride = (uint32_t)strides[2][1];

  for (i = 0u; i < 4u; i++) {
    if (i == 3u) {
      sizes[i] = ROUNDS * shape.m * shape.n * sizeof(float) + 256u;
    } else {
      sizes[i] = requirements[i].sizeBytes + 256u;
    }

    bufferInfo.sizeBytes = sizes[i];
    bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

    if (imported) {
      if (page == 0u || page > SIZE_MAX / 2u || sizes[i] > SIZE_MAX - 2u * page)
        goto cleanup;

      extent = (sizes[i] + page - 1u) / page * page;

      if (!(allocations[i] = malloc((size_t)extent + page)))
        goto cleanup;

      hostData[i] = (uint8_t *)(((uintptr_t)allocations[i] + page - 1u) / page * page);
      memset(hostData[i], 0xa5, (size_t)extent);

      host.chain.sType       = GPU_STRUCTURE_TYPE_BUFFER_HOST_MEMORY_EXT;
      host.chain.structSize  = sizeof(host);
      host.pData             = hostData[i];
      host.allocationSize    = extent;
      bufferInfo.chain.pNext = &host;
    }

    if (GPUCreateBuffer(device, &bufferInfo, &buffers[i]) != GPU_OK) {
      goto cleanup;
    }

    if (!imported) {
      if (!(data = malloc((size_t)sizes[i]))) {
        goto cleanup;
      }

      memset(data, 0xa5, (size_t)sizes[i]);

      if (GPUQueueWriteBuffer(queue, buffers[i], 0u, data, sizes[i]) != GPU_OK) {
        goto cleanup;
      }

      free(data);
      data = NULL;
    }

    if (i < 3u) {
      desc.pDimensions = info->pBindings[i].shape.pDimensions;
      desc.pStrides    = strides[i];
      viewInfo.pDesc   = &desc;
      viewInfo.buffer  = buffers[i];

      if (GPUCreateTensorViewEXT(device, &viewInfo, &tensors[i]) != GPU_OK) {
        goto cleanup;
      }

      tensorBindings[i].slot   = info->pBindings[i].shape.slot;
      tensorBindings[i].tensor = tensors[i];
    }

    entries[i].binding       = i;
    entries[i].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[i].buffer.buffer = buffers[i];
    entries[i].buffer.size   = sizes[i];
  }

  bindingInfo.pipeline     = pipeline;
  bindingInfo.pBindings    = tensorBindings;
  bindingInfo.bindingCount = 3u;

  if ((!imported && (!invalid_bindings(device, &bindingInfo, buffers[0])
                    || !placement_alias(device, &bindingInfo)
                    || !host_binding(device, &bindingInfo)))
      || GPUCreateMLBindingsEXT(device, &bindingInfo, &bindings) != GPU_OK) {
    goto cleanup;
  }

  groupInfo.layout     = groupLayout;
  groupInfo.pEntries   = entries;
  groupInfo.entryCount = 4u;

  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK
      || GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK
      || GPUAcquireCommandBuffer(queue, "mixed ml timeline", &cmdb) != GPU_OK) {
    goto cleanup;
  }

  invalid._queue         = queue;
  invalid._activeEncoder = true;

  if (GPUEncodeMLEXT(&invalid, bindings) != GPU_ERROR_INVALID_ARGUMENT
      || GPUEncodeMLEXT(NULL, bindings) != GPU_ERROR_INVALID_ARGUMENT) {
    goto cleanup;
  }

  count = shape.m * shape.k;
  count = shape.k * shape.n > count ? shape.k * shape.n : count;
  count = shape.m * shape.n > count ? shape.m * shape.n : count;

  for (shape.round = 0u; shape.round < ROUNDS; shape.round++) {
    barrier.srcStages = GPU_STAGE_ML_EXT;
    barrier.dstStages = GPU_STAGE_COMPUTE;
    GPUEncodeBarriers(cmdb, &barrier);

    if (!(pass = GPUBeginComputePass(cmdb, "produce"))) {
      goto cleanup;
    }

    GPUBindComputePipeline(pass, prepare);
    GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
    GPUSetComputePushConstants(pass, 0u, sizeof(shape), &shape);
    GPUDispatch(pass, count, 1u, 1u);
    GPUEndComputePass(pass);
    pass = NULL;

    barrier.srcStages = GPU_STAGE_COMPUTE;
    barrier.dstStages = GPU_STAGE_ML_EXT;
    GPUEncodeBarriers(cmdb, &barrier);

    if (GPUEncodeMLEXT(cmdb, bindings) != GPU_OK) {
      goto cleanup;
    }

    if (shape.round == 1u) {
      /* one ordered ml -> ml reuse of the same immutable table and scratch. */
      barrier.srcStages = GPU_STAGE_ML_EXT;
      barrier.dstStages = GPU_STAGE_ML_EXT;
      GPUEncodeBarriers(cmdb, &barrier);

      if (GPUEncodeMLEXT(cmdb, bindings) != GPU_OK) {
        goto cleanup;
      }
    }

    barrier.srcStages = GPU_STAGE_ML_EXT;
    barrier.dstStages = GPU_STAGE_COMPUTE;
    GPUEncodeBarriers(cmdb, &barrier);

    if (shape.round == 1u) {
      /* an unrelated encoder must not consume a pending compute dependency. */
      if (!(transfer = GPUBeginTransferPass(cmdb, "intervening transfer"))) {
        goto cleanup;
      }

      GPUEndTransferPass(transfer);
      transfer = NULL;
    }

    if (!(pass = GPUBeginComputePass(cmdb, "consume"))) {
      goto cleanup;
    }

    GPUBindComputePipeline(pass, consume);
    GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
    GPUSetComputePushConstants(pass, 0u, sizeof(shape), &shape);
    GPUDispatch(pass, shape.m * shape.n, 1u, 1u);
    GPUEndComputePass(pass);
    pass = NULL;
  }

  submit.ppCommandBuffers   = &cmdb;
  submit.commandBufferCount = 1u;
  submit.fence              = fence;

  if (GPUQueueSubmit(queue, &submit) != GPU_OK) {
    goto cleanup;
  }

  cmdb = NULL;

  if (GPUWaitFence(fence, UINT64_C(30000000000)) != GPU_OK) {
    goto cleanup;
  }

  for (i = 0u; i < 4u; i++) {
    if (!(data = malloc((size_t)sizes[i])) || GPUQueueReadBuffer(queue, buffers[i], 0u, data, sizes[i]) != GPU_OK) {
      goto cleanup;
    }

    for (offset = sizes[i] - 256u; offset < sizes[i]; offset++) {
      if (data[offset] != 0xa5u) {
        fprintf(stderr, "ML guard changed: profile=%u buffer=%u offset=%llu\n", profileId, i,
                (unsigned long long)offset);
        goto cleanup;
      }
    }

    if (imported) {
      extent = (sizes[i] + page - 1u) / page * page;

      for (offset = sizes[i]; offset < extent; offset++) {
        if (hostData[i][offset] != 0xa5u) {
          fprintf(stderr, "ML host padding changed: profile=%u buffer=%u offset=%llu\n", profileId, i,
                  (unsigned long long)offset);
          goto cleanup;
        }

        pageGuards++;
      }
    }

    if (i == 3u) {
      values = (float *)data;

      for (j = 0u; j < ROUNDS; j++) {
        for (row = 0u; row < shape.m; row++) {
          for (column = 0u; column < shape.n; column++) {
            expected = (int)(j * 10000u + 1u);

            for (k = 0u; k < shape.k; k++) {
              a = (int)((row * 3u + k * 5u + j * 2u) % 7u) - 3;
              b = (int)((k * 2u + column * 5u + j * 3u) % 9u) - 4;
              expected += a * b;
            }

            if (values[j * shape.m * shape.n + row * shape.n + column] != (float)expected) {
              fprintf(stderr, "ML output mismatch: profile=%u round=%u row=%u col=%u\n", profileId, j, row, column);
              goto cleanup;
            }
          }
        }
      }
    }

    free(data);
    data = NULL;
  }

  printf("ML profile=%u storage=%s outputs=%u guards=1024 page-guards=%llu network-passes=4 "
         "repeated-native-pipeline=yes\n", profileId, imported ? "host" : "owned", ROUNDS * shape.m * shape.n,
         (unsigned long long)pageGuards);
  ok = 1;

cleanup:
  if (transfer) {
    GPUEndTransferPass(transfer);
  }

  if (pass) {
    GPUEndComputePass(pass);
  }

  if (cmdb) {
    GPUDiscardCommandBuffer(cmdb);
  }

  (void)deviceApi(device)->device.waitIdle(device);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyMLBindingsEXT(bindings);

  for (i = 0u; i < 3u; i++) {
    GPUDestroyTensorEXT(tensors[i]);
  }

  for (i = 0u; i < 4u; i++) {
    GPUDestroyBuffer(buffers[i]);
    free(allocations[i]);
  }

  GPUDestroyHeap(bindingInfo.scratch);
  GPUDestroyMLPipelineEXT(cached);
  GPUDestroyMLPipelineEXT(pipeline);
  free(data);
  return ok;
}

int
gpu_test_ml(GPUDevice *baseDevice) {
  GPUDeviceCreateInfo        deviceInfo       = {0};
  GPUMLModelCreateInfoEXT    modelInfo        = {0};
  GPUMLPipelineCreateInfoEXT pipelineInfo     = {0};
  const GPUFeature           features[]       = {GPU_FEATURE_COMPUTE, GPU_FEATURE_TENSOR_RESOURCES_EXT,
                                                 GPU_FEATURE_PLACED_RESOURCES, GPU_FEATURE_ML_MODEL_EXT,
                                                 GPU_FEATURE_BUFFER_HOST_MEMORY_EXT};
  uint64_t                   dimensions[4][2] = {{16u, 8u}, {8u, 16u}, {24u, 12u}, {20u, 24u}};
  GPUMLTensorShapeEXT        inputs[2][2]     = {
    {{dimensions[0], GPU_TENSOR_DATA_TYPE_F32_EXT, 0u, 2u}, {dimensions[1], GPU_TENSOR_DATA_TYPE_F32_EXT, 1u, 2u}},
    {{dimensions[2], GPU_TENSOR_DATA_TYPE_F32_EXT, 0u, 2u}, {dimensions[3], GPU_TENSOR_DATA_TYPE_F32_EXT, 1u, 2u}}
  };
  GPUMLShapeProfileEXT       profiles[]       = {{inputs[0], 3u, 2u}, {inputs[1], 1u, 2u}};
  MLShape                    shapes[2]        = {{8u, 8u, 16u, 0u, 0u, 0u, 0u}, {12u, 20u, 24u, 0u, 0u, 0u, 0u}};
  GPUDevice                 *device      = NULL;
  GPUMLModelEXT             *model       = NULL;
  GPUMLModelEXT             *bad         = NULL;
  GPUMLPipelineEXT          *badPipeline = NULL;
  GPUShaderLibrary          *library     = NULL;
  GPUBindGroupLayout        *group       = NULL;
  GPUPipelineLayout         *layout      = NULL;
  GPUComputePipeline        *prepare     = NULL;
  GPUComputePipeline        *consume     = NULL;
  GPUResult                  result;
  uint32_t                   i;
  int                        ok = 0;

  GPUDestroyMLModelEXT(NULL);
  GPUDestroyMLPipelineEXT(NULL);
  GPUDestroyMLBindingsEXT(NULL);

  if (GPUGetMLPipelineInfoEXT(NULL) || GPUEncodeMLEXT(NULL, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    return 0;
  }

  modelInfo.path         = GPU_TEST_ML_ROOT "/matmul.mtlpackage";
  modelInfo.functionName = "main";
  modelInfo.pProfiles    = profiles;
  modelInfo.profileCount = GPU_ARRAY_LEN(profiles);

  if (GPUCreateMLModelEXT(baseDevice, &modelInfo, &bad) != GPU_ERROR_UNSUPPORTED || bad) {
    return 0;
  }

  for (i = 0u; i < GPU_ARRAY_LEN(mlProcs); i++) {
    if (GPUGetProcAddr(baseDevice, mlProcs[i])) {
      return 0;
    }
  }

  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = GPU_ARRAY_LEN(features)
                                     - !GPUIsFeatureSupported(baseDevice->adapter, GPU_FEATURE_BUFFER_HOST_MEMORY_EXT);
  result                           = gpu_test_create_device(baseDevice->adapter, &deviceInfo, &device);

  if (!GPUIsFeatureSupported(baseDevice->adapter, GPU_FEATURE_ML_MODEL_EXT)) {
    GPUDestroyDevice(device);
    return result == GPU_ERROR_UNSUPPORTED;
  }

  if (result != GPU_OK || !device) {
    return 0;
  }

  for (i = 0u; i < GPU_ARRAY_LEN(mlProcs); i++) {
    if (!GPUGetProcAddr(device, mlProcs[i])) {
      goto cleanup;
    }
  }

  dimensions[0][0] = 0u;

  if (!invalid_model(device, &modelInfo, GPU_ERROR_INVALID_ARGUMENT)) {
    goto cleanup;
  }

  dimensions[0][0] = 16u;
  profiles[1].id   = profiles[0].id;

  if (!invalid_model(device, &modelInfo, GPU_ERROR_INVALID_ARGUMENT)) {
    goto cleanup;
  }

  profiles[1].id    = 1u;
  inputs[0][1].slot = inputs[0][0].slot;

  if (!invalid_model(device, &modelInfo, GPU_ERROR_INVALID_ARGUMENT)) {
    goto cleanup;
  }

  inputs[0][1].slot     = 1u;
  inputs[0][0].dataType = GPU_TENSOR_DATA_TYPE_F16_EXT;

  if (!invalid_model(device, &modelInfo, GPU_ERROR_UNSUPPORTED)) {
    goto cleanup;
  }

  inputs[0][0].dataType      = GPU_TENSOR_DATA_TYPE_F32_EXT;
  modelInfo.chain.structSize = 1u;

  if (!invalid_model(device, &modelInfo, GPU_ERROR_INVALID_ARGUMENT)) {
    goto cleanup;
  }

  modelInfo.chain.structSize = 0u;
  inputs[0][0].rank          = 1u;
  bad                        = (GPUMLModelEXT *)(uintptr_t)1u;
  result                     = GPUCreateMLModelEXT(device, &modelInfo, &bad);
  inputs[0][0].rank          = 2u;

  if (result != GPU_ERROR_UNSUPPORTED || bad) {
    goto cleanup;
  }

  inputs[0][0].slot = 7u;
  result            = GPUCreateMLModelEXT(device, &modelInfo, &bad);
  inputs[0][0].slot = 0u;

  if (result != GPU_ERROR_INVALID_ARGUMENT || bad || GPUCreateMLModelEXT(device, &modelInfo, &model) != GPU_OK) {
    goto cleanup;
  }

  /* caller metadata may change after preparation without changing model profiles. */
  dimensions[0][0]       = 0u;
  pipelineInfo.model     = model;
  pipelineInfo.profileId = 9u;
  badPipeline            = (GPUMLPipelineEXT *)(uintptr_t)1u;

  if (GPUCreateMLPipelineEXT(device, &pipelineInfo, &badPipeline) != GPU_ERROR_INVALID_ARGUMENT
      || badPipeline || model->profiles[0]._priv || model->profiles[1]._priv) {
    goto cleanup;
  }

  if (!make_compute(device, &library, &group, &layout, &prepare, &consume)) {
    goto cleanup;
  }

  for (i = 0u; i < GPU_ARRAY_LEN(shapes); i++) {
    if (!run_profile(device, model, profiles[i].id, shapes[i], group, prepare, consume, false)
        || (GPUIsFeatureEnabled(device, GPU_FEATURE_BUFFER_HOST_MEMORY_EXT)
            && !run_profile(device, model, profiles[i].id, shapes[i], group, prepare, consume, true))) {
      goto cleanup;
    }
  }

#if defined(GPU_TEST_METAL_TENSORS)
  if (!gpu_test_metal_ml_scratch(device))
    goto cleanup;
#endif

  ok = 1;

cleanup:
  GPUDestroyComputePipeline(consume);
  GPUDestroyComputePipeline(prepare);
  GPUDestroyPipelineLayout(layout);
  GPUDestroyBindGroupLayout(group);
  GPUDestroyShaderLibrary(library);
  GPUDestroyMLModelEXT(model);
  GPUDestroyDevice(device);
  return ok;
}
