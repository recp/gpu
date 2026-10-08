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

#include "../test.h"
#include "../../../src/backend/mt/common.h"
#include "../../../src/api/ml_internal.h"

int
gpu_test_metal_ml_scratch(GPUDevice *device) {
#if MT_HAS_METAL4
  uint64_t                             dimensions[] = {16u, 8u};
  uint64_t                             strides[]    = {1u, 16u};
  GPUMLTensorShapeEXT                  input        = {dimensions, GPU_TENSOR_DATA_TYPE_F32_EXT, 0u, 2u};
  GPUMLShapeProfileEXT                 profile      = {&input, 7u, 1u};
  GPUMLModelCreateInfoEXT              modelInfo    = {0};
  GPUMLPipelineCreateInfoEXT           pipelineInfo = {0};
  GPUMLBindingsCreateInfoEXT           bindingInfo  = {0};
  GPUMLTensorBindingEXT                items[2]     = {0};
  GPUTensorDescEXT                     desc         = {0};
  GPUTensorBufferRequirementsEXT       requirements = {0};
  GPUTensorViewCreateInfoEXT           viewInfo     = {0};
  GPUBufferCreateInfo                  bufferInfo   = {0};
  GPUHeapCreateInfo                    heapInfo;
  GPUQueueSubmitInfo                   submit       = {0};
  GPUFenceCreateInfo                   fenceInfo    = {0};
  GPUBarrierBatch                      barrier      = {0};
  GPUBuffer                           *buffers[2]   = {NULL};
  GPUTensorEXT                        *tensors[2]   = {NULL};
  GPUMLModelEXT                       *model        = NULL;
  GPUMLPipelineEXT                    *pipeline     = NULL;
  GPUMLPipelineEXT                    *cached       = NULL;
  GPUMLBindingsEXT                    *bindings     = NULL;
  GPUHeap                             *small        = NULL;
  GPUHeap                             *scratch      = NULL;
  GPUCommandBuffer                    *cmdb         = NULL;
  GPUFence                            *fence        = NULL;
  const GPUMLPipelineInfoEXT          *info;
  GPUQueue                            *queue;
  uint8_t                             *bytes        = NULL;
  float                               *values;
  id<MTL4MachineLearningPipelineState> native;
  uint64_t                             offset;
  uint64_t                             nativeSize;
  GPUResult                            result;
  uint32_t                             i;
  uint32_t                             j;
  int                                  ok           = 0;

  if (@available(macOS 26.0, iOS 26.0, *)) {
    modelInfo.path         = GPU_TEST_ML_ROOT "/identity.mtlpackage";
    modelInfo.functionName = "main";
    modelInfo.pProfiles    = &profile;
    modelInfo.profileCount = 1u;

    if (GPUCreateMLModelEXT(device, &modelInfo, &model) != GPU_OK)
      goto cleanup;

    pipelineInfo.model     = model;
    pipelineInfo.profileId = profile.id;

    if (GPUCreateMLPipelineEXT(device, &pipelineInfo, &pipeline) != GPU_OK
        || GPUCreateMLPipelineEXT(device, &pipelineInfo, &cached) != GPU_OK
        || pipeline->profile->_priv != cached->profile->_priv)
      goto cleanup;

    native     = pipeline->profile->_priv;
    nativeSize = native.intermediatesHeapSize;
    info       = GPUGetMLPipelineInfoEXT(pipeline);
    queue      = GPUGetCommandQueue(device, GPU_QUEUE_COMPUTE);

    if (!queue || nativeSize > 64u || info->scratchHeap.sizeBytes != 64u
        || info->bindingCount != 2u || info->pBindings[0].shape.slot != 0u
        || info->pBindings[0].access != GPU_ACCESS_SHADER_READ
        || info->pBindings[1].shape.slot != 1u
        || info->pBindings[1].access != GPU_ACCESS_SHADER_WRITE)
      goto cleanup;

    heapInfo           = info->scratchHeap;
    heapInfo.sizeBytes = 63u;

    if (GPUCreateHeap(device, &heapInfo, &small) != GPU_OK
        || GPUCreateHeap(device, &info->scratchHeap, &scratch) != GPU_OK)
      goto cleanup;

    desc.pDimensions = dimensions;
    desc.pStrides    = strides;
    desc.dataType    = GPU_TENSOR_DATA_TYPE_F32_EXT;
    desc.rank        = 2u;
    desc.usage       = GPU_TENSOR_USAGE_ML_EXT;

    if (GPUGetTensorBufferRequirementsEXT(device, &desc, &requirements) != GPU_OK
        || requirements.sizeBytes > SIZE_MAX - 256u
        || !(bytes = malloc((size_t)requirements.sizeBytes + 256u)))
      goto cleanup;

    bufferInfo.sizeBytes = requirements.sizeBytes + 256u;
    bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;

    for (i = 0u; i < 2u; i++) {
      if (GPUCreateBuffer(device, &bufferInfo, &buffers[i]) != GPU_OK)
        goto cleanup;

      memset(bytes, 0xa5, (size_t)bufferInfo.sizeBytes);
      values = (float *)bytes;

      if (i == 0u) {
        for (j = 0u; j < 128u; j++) {
          values[j] = (float)((int)(j * 3u) - 127);
        }
      }

      if (GPUQueueWriteBuffer(queue, buffers[i], 0u, bytes, bufferInfo.sizeBytes) != GPU_OK)
        goto cleanup;

      viewInfo.buffer = buffers[i];
      viewInfo.pDesc  = &desc;

      if (GPUCreateTensorViewEXT(device, &viewInfo, &tensors[i]) != GPU_OK)
        goto cleanup;

      items[i].slot   = i;
      items[i].tensor = tensors[i];
    }

    bindingInfo.pipeline     = pipeline;
    bindingInfo.pBindings    = items;
    bindingInfo.bindingCount = 2u;
    bindings                 = (GPUMLBindingsEXT *)(uintptr_t)1u;
    result                   = GPUCreateMLBindingsEXT(device, &bindingInfo, &bindings);

    if (result != GPU_ERROR_INVALID_ARGUMENT || bindings) {
      if (result != GPU_OK)
        bindings = NULL;

      goto cleanup;
    }

    bindingInfo.scratch = small;
    bindings           = (GPUMLBindingsEXT *)(uintptr_t)1u;
    result             = GPUCreateMLBindingsEXT(device, &bindingInfo, &bindings);

    if (result != GPU_ERROR_INVALID_ARGUMENT || bindings) {
      if (result != GPU_OK)
        bindings = NULL;

      goto cleanup;
    }

    bindingInfo.scratch = scratch;

    if (GPUCreateMLBindingsEXT(device, &bindingInfo, &bindings) != GPU_OK
        || GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK
        || GPUAcquireCommandBuffer(queue, "minimal ml scratch", &cmdb) != GPU_OK)
      goto cleanup;

    for (i = 0u; i < 3u; i++) {
      if (i > 0u) {
        barrier.srcStages = GPU_STAGE_ML_EXT;
        barrier.dstStages = GPU_STAGE_ML_EXT;
        GPUEncodeBarriers(cmdb, &barrier);
      }

      if (GPUEncodeMLEXT(cmdb, bindings) != GPU_OK)
        goto cleanup;
    }

    submit.ppCommandBuffers   = &cmdb;
    submit.commandBufferCount = 1u;
    submit.fence              = fence;

    if (GPUQueueSubmit(queue, &submit) != GPU_OK)
      goto cleanup;

    cmdb = NULL;

    if (GPUWaitFence(fence, UINT64_C(30000000000)) != GPU_OK)
      goto cleanup;

    for (i = 0u; i < 2u; i++) {
      if (GPUQueueReadBuffer(queue, buffers[i], 0u, bytes, bufferInfo.sizeBytes) != GPU_OK)
        goto cleanup;

      values = (float *)bytes;

      for (j = 0u; j < 128u; j++) {
        if (values[j] != (float)((int)(j * 3u) - 127))
          goto cleanup;
      }

      for (offset = requirements.sizeBytes; offset < bufferInfo.sizeBytes; offset++) {
        if (bytes[offset] != 0xa5u)
          goto cleanup;
      }
    }

    printf("ML minimal scratch: native=%llu public=64 passes=3 exact=256 guards=512 "
           "null/undersized rejected; zero-native=%s\n", (unsigned long long)nativeSize,
           nativeSize == 0u ? "yes" : "no");
    ok = 1;

cleanup:
    if (cmdb)
      GPUDiscardCommandBuffer(cmdb);

    (void)deviceApi(device)->device.waitIdle(device);
    GPUDestroyFence(fence);
    GPUDestroyMLBindingsEXT(bindings);

    for (i = 0u; i < 2u; i++) {
      GPUDestroyTensorEXT(tensors[i]);
      GPUDestroyBuffer(buffers[i]);
    }

    GPUDestroyHeap(scratch);
    GPUDestroyHeap(small);
    GPUDestroyMLPipelineEXT(cached);
    GPUDestroyMLPipelineEXT(pipeline);
    GPUDestroyMLModelEXT(model);
    free(bytes);
    return ok;
  }
#else
  GPU__UNUSED(device);
#endif

  return 0;
}
