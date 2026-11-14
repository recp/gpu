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

#include <gpu/gpu.h>
#include "../../../src/api/buffer_internal.h"
#include "../../../src/api/cmdqueue_internal.h"
#include "../../../src/api/device_internal.h"
#include "../../../src/backend/cuda/common.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  ValueCount          = 512u,
  RetireDispatchCount = 64u,
  RetireRoundCount    = 8u
};

typedef struct Params {
  float scale;
  float bias;
} Params;

static void*
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;

  if (!file || fseek(file, 0, SEEK_END) != 0
      || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return NULL;
  }

  if (!(data = malloc((size_t)size)) || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *outSize = (uint64_t)size;

  return data;
}

static int
values_match(const float values[ValueCount], const Params *params) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    const float expected = (float)i * params->scale + params->bias;

    if (fabsf(values[i] - expected) > 0.0001f) {
      return 0;
    }
  }

  return 1;
}

static int
dispatch_saxpy(GPUQueue           *queue,
               GPUComputePipeline *pipeline,
               GPUBindGroup       *paramsGroup,
               GPUBindGroup       *dataGroup,
               GPUFence           *fence,
               uint32_t            dynamicOffset) {
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUQueueSubmitInfo     submitInfo = {0};

  cmdb = NULL;
  pass = NULL;

  if (GPUAcquireCommandBuffer(queue, "cuda-saxpy", &cmdb) != GPU_OK
      || !cmdb || !(pass = GPUBeginComputePass(cmdb, "saxpy"))) {
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, paramsGroup, 1u, &dynamicOffset);
  GPUBindComputeGroup(pass, 1u, dataGroup, 0u, NULL);
  GPUDispatch(pass, ValueCount / 256u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = &cmdb;
  submitInfo.fence              = fence;
  submitInfo.commandBufferCount = 1u;

  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    goto cleanup;
  }

  cmdb = NULL;

  if (GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    return 0;
  }

  GPUResetFence(fence);

  return 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }

  if (cmdb && !cmdb->_submitted) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }

  return 0;
}

static GPUBuffer*
create_values_buffer(GPUDevice *device, const char *label, const float values[ValueCount]) {
  GPUBufferCreateInfo info = {0};
  GPUBuffer          *buffer;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.sizeBytes        = ValueCount * sizeof(float);
  info.usage            = GPU_BUFFER_USAGE_STORAGE |
                          GPU_BUFFER_USAGE_COPY_SRC |
                          GPU_BUFFER_USAGE_COPY_DST;
  buffer                = NULL;

  if (GPUCreateBuffer(device, &info, &buffer) != GPU_OK || !buffer) {
    return NULL;
  }

  if (values && GPUQueueWriteBuffer(GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u),
                                    buffer,
                                    0u,
                                    values,
                                    info.sizeBytes) != GPU_OK) {
    GPUDestroyBuffer(buffer);
    return NULL;
  }

  return buffer;
}

/* a buffer destroyed while dispatches still read it keeps its pooled range
 * until they finish, so a buffer made meanwhile gets other memory, and
 * nothing waits any more once their fence is signaled. a round tells only
 * if the dispatches were still running when the new buffer was made: the
 * queue hadn't finished their serial even after it. */
static int
check_destroy_while_in_use(GPUDevice          *device,
                           GPUQueue           *queue,
                           GPUComputePipeline *pipeline,
                           GPUBindGroupLayout *dataLayout,
                           GPUBindGroup       *paramsGroup,
                           GPUFence           *fence,
                           uint32_t            dynamicOffset,
                           const Params       *params) {
  GPUBindGroupCreateInfo groupInfo  = {0};
  GPUBindGroupEntry      entries[2] = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  float                  values[ValueCount];
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUBuffer             *input;
  GPUBuffer             *output;
  GPUBuffer             *later;
  GPUBindGroup          *group;
  uint64_t               serial;
  uint64_t               address;
  uint32_t               held;
  uint32_t               round;
  uint32_t               i;

  for (i = 0u; i < ValueCount; i++) {
    values[i] = (float)i;
  }

  held = 0u;

  for (round = 0u; round < RetireRoundCount; round++) {
    cmdb   = NULL;
    pass   = NULL;
    later  = NULL;
    group  = NULL;
    output = NULL;

    if (!(input = create_values_buffer(device, "cuda-retire-input", values))
        || !(output = create_values_buffer(device, "cuda-retire-output", NULL))) {
      fprintf(stderr, "CUDA retire buffers creation failed\n");
      goto done;
    }

    entries[0].binding       = 0u;
    entries[0].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
    entries[0].buffer.buffer = input;
    entries[0].buffer.size   = sizeof(values);
    entries[1].binding       = 1u;
    entries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[1].buffer.buffer = output;
    entries[1].buffer.size   = sizeof(values);
    groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
    groupInfo.chain.structSize = sizeof(groupInfo);
    groupInfo.label            = "cuda-retire-group";
    groupInfo.layout           = dataLayout;
    groupInfo.entryCount       = 2u;
    groupInfo.pEntries         = entries;

    if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group
        || GPUAcquireCommandBuffer(queue, "cuda-retire", &cmdb) != GPU_OK || !cmdb
        || !(pass = GPUBeginComputePass(cmdb, "cuda-retire"))) {
      fprintf(stderr, "CUDA retire dispatch setup failed\n");
      goto done;
    }

    GPUBindComputePipeline(pass, pipeline);
    GPUBindComputeGroup(pass, 0u, paramsGroup, 1u, &dynamicOffset);
    GPUBindComputeGroup(pass, 1u, group, 0u, NULL);

    for (i = 0u; i < RetireDispatchCount; i++) {
      GPUDispatch(pass, ValueCount / 256u, 1u, 1u);
    }

    GPUEndComputePass(pass);
    pass = NULL;

    submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize   = sizeof(submitInfo);
    submitInfo.ppCommandBuffers   = &cmdb;
    submitInfo.fence              = fence;
    submitInfo.commandBufferCount = 1u;

    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
      cmdb = NULL;
      fprintf(stderr, "CUDA retire submit failed\n");
      goto done;
    }

    cmdb    = NULL;
    serial  = gpuRetireLoad(&queue->_retire.submitted);
    address = input->_gpuAddress;
    GPUDestroyBindGroup(group);
    GPUDestroyBuffer(input);
    group = NULL;
    input = NULL;

    if (!(later = create_values_buffer(device, "cuda-retire-later", NULL))) {
      fprintf(stderr, "CUDA retire later buffer creation failed\n");
      goto done;
    }

    if (gpuRetireLoad(&queue->_retire.done) < serial) {
      held++;

      if (later->_gpuAddress == address) {
        fprintf(stderr, "CUDA buffer memory reused while dispatches still read it\n");
        goto done;
      }
    }

    if (GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
      fprintf(stderr, "CUDA retire fence wait failed\n");
      goto done;
    }

    GPUResetFence(fence);

    if (cuda_device(device)->retire.head) {
      fprintf(stderr, "CUDA destroyed buffer still waiting after its fence\n");
      goto done;
    }

    if (GPUQueueReadBuffer(queue, output, 0u, values, sizeof(values)) != GPU_OK
        || !values_match(values, params)) {
      fprintf(stderr, "CUDA retire readback validation failed\n");
      goto done;
    }

    for (i = 0u; i < ValueCount; i++) {
      values[i] = (float)i;
    }

    GPUDestroyBuffer(later);
    GPUDestroyBuffer(output);
  }

  printf("CUDA destroy while in use: %u of %u rounds still running at the destroy\n",
         held,
         (unsigned)RetireRoundCount);

  return 1;

done:
  if (pass) {
    GPUEndComputePass(pass);
  }

  if (cmdb && !cmdb->_submitted) {
    (void)GPUDiscardCommandBuffer(cmdb);
  }

  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(later);
  GPUDestroyBuffer(output);
  GPUDestroyBuffer(input);

  return 0;
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo        instanceInfo   = {0};
  GPUComputePipelineCreateInfo pipelineInfo   = {0};
  GPUBufferCreateInfo          bufferInfo     = {0};
  GPUBindGroupEntry            dataEntries[2] = {0};
  GPUBindGroupEntry            paramsEntry    = {0};
  GPUBindGroupCreateInfo       groupInfo      = {0};
  Params                       params[2]      = {{-3.0f, 7.0f}, {2.0f, 1.0f}};
  float                        input[ValueCount];
  float                        output[ValueCount];
  GPUInstance                 *instance;
  GPUAdapter                  *adapter;
  GPUDevice                   *device;
  GPUQueue                    *queue;
  GPUShaderLibrary            *library;
  GPUShaderLayout             *shaderLayout;
  GPUComputePipeline          *pipeline;
  GPUBuffer                   *inputBuffer;
  GPUBuffer                   *outputBuffer;
  GPUBuffer                   *paramsBuffer;
  GPUBindGroup                *dataGroup;
  GPUBindGroup                *paramsGroup;
  GPUFence                    *fence;
  void                        *artifact;
  uint64_t                     artifactSize;
  uint32_t                     adapterCount;
  uint32_t                     dynamicOffset;
  GPUResult                    result;
  int                          status;
  uint32_t fillIndex;
  uint32_t warmIndex;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-compute-buffer-cuda-usl artifact.us\n");
    return 1;
  }

  instance     = NULL;
  adapter      = NULL;
  device       = NULL;
  queue        = NULL;
  library      = NULL;
  shaderLayout = NULL;
  pipeline     = NULL;
  inputBuffer  = NULL;
  outputBuffer = NULL;
  paramsBuffer = NULL;
  dataGroup    = NULL;
  paramsGroup  = NULL;
  fence        = NULL;
  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  status       = 1;

  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_CUDA;
  instanceInfo.enableValidation = true;

  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    puts("CUDA Driver backend unavailable");
    status = 77;
    goto cleanup;
  }

  adapterCount = 1u;
  result       = GPUEnumerateAdapters(instance, &adapterCount, &adapter);

  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY)
      || !adapter) {
    puts("CUDA adapter unavailable");
    status = 77;
    goto cleanup;
  }

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);

  if (!device || !queue) {
    fprintf(stderr, "CUDA compute device creation failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK
      || !library
      || GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK
      || !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u
      || !shaderLayout->bindGroupLayouts
      || !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA USL shader layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "cuda-usl-saxpy";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "saxpy";

  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK
      || !pipeline) {
    fprintf(stderr, "CUDA compute pipeline creation failed\n");
    goto cleanup;
  }

  for (fillIndex = 0u; fillIndex < ValueCount; fillIndex++) {
    input[fillIndex]  = (float)fillIndex;
    output[fillIndex] = 0.0f;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "cuda-saxpy-input";
  bufferInfo.sizeBytes        = sizeof(input);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &inputBuffer) != GPU_OK
      || !inputBuffer
      || GPUQueueWriteBuffer(queue,
                             inputBuffer,
                             0u,
                             input,
                             sizeof(input)) != GPU_OK) {
    fprintf(stderr, "CUDA input buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "cuda-saxpy-output";
  bufferInfo.sizeBytes = sizeof(output);

  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK
      || !outputBuffer
      || GPUQueueWriteBuffer(queue,
                             outputBuffer,
                             0u,
                             output,
                             sizeof(output)) != GPU_OK) {
    fprintf(stderr, "CUDA output buffer creation failed\n");
    goto cleanup;
  }

  bufferInfo.label     = "cuda-saxpy-params";
  bufferInfo.sizeBytes = sizeof(params);
  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM |
                         GPU_BUFFER_USAGE_COPY_DST;

  if (GPUCreateBuffer(device, &bufferInfo, &paramsBuffer) != GPU_OK
      || !paramsBuffer
      || GPUQueueWriteBuffer(queue,
                             paramsBuffer,
                             0u,
                             params,
                             sizeof(params)) != GPU_OK) {
    fprintf(stderr, "CUDA uniform buffer creation failed\n");
    goto cleanup;
  }

  paramsEntry.binding        = 0u;
  paramsEntry.bindingType    = GPU_BINDING_UNIFORM_BUFFER;
  paramsEntry.buffer.buffer  = paramsBuffer;
  paramsEntry.buffer.size    = sizeof(params[0]);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-saxpy-params-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &paramsEntry;

  if (GPUCreateBindGroup(device, &groupInfo, &paramsGroup) != GPU_OK
      || !paramsGroup) {
    fprintf(stderr, "CUDA params bind group creation failed\n");
    goto cleanup;
  }

  dataEntries[0].binding       = 0u;
  dataEntries[0].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  dataEntries[0].buffer.buffer = inputBuffer;
  dataEntries[0].buffer.size   = sizeof(input);
  dataEntries[1].binding       = 1u;
  dataEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  dataEntries[1].buffer.buffer = outputBuffer;
  dataEntries[1].buffer.size   = sizeof(output);
  groupInfo.label              = "cuda-saxpy-data-group";
  groupInfo.layout             = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount         = 2u;
  groupInfo.pEntries           = dataEntries;

  if (GPUCreateBindGroup(device, &groupInfo, &dataGroup) != GPU_OK
      || !dataGroup) {
    fprintf(stderr, "CUDA data bind group creation failed\n");
    goto cleanup;
  }

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "CUDA completion fence creation failed\n");
    goto cleanup;
  }

  dynamicOffset = sizeof(params[0]);

  if (!dispatch_saxpy(queue,
                      pipeline,
                      paramsGroup,
                      dataGroup,
                      fence,
                      dynamicOffset)) {
    fprintf(stderr, "CUDA command dispatch failed\n");
    goto cleanup;
  }

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK
      || !values_match(output, &params[1])) {
    fprintf(stderr, "CUDA compute readback validation failed\n");
    goto cleanup;
  }

  GPUResetStats(device);

  for (warmIndex = 0u; warmIndex < 16u; warmIndex++) {
    if (!dispatch_saxpy(queue,
                        pipeline,
                        paramsGroup,
                        dataGroup,
                        fence,
                        dynamicOffset)) {
      fprintf(stderr, "CUDA warm dispatch failed\n");
      goto cleanup;
    }
  }

  if (device->currentFrameStats.hotPathAllocCount != 0u
      || device->currentFrameStats.hotPathAllocBytes != 0u
      || device->currentFrameStats.hotPathFreeCount != 0u
      || device->currentFrameStats.hotPathFreeBytes != 0u) {
    fprintf(stderr,
            "CUDA warm dispatch allocated: %llu allocs, %llu frees\n",
            (unsigned long long)
              device->currentFrameStats.hotPathAllocCount,
            (unsigned long long)
              device->currentFrameStats.hotPathFreeCount);
    goto cleanup;
  }

  if (!check_destroy_while_in_use(device,
                                  queue,
                                  pipeline,
                                  shaderLayout->bindGroupLayouts[1],
                                  paramsGroup,
                                  fence,
                                  dynamicOffset,
                                  &params[1])) {
    goto cleanup;
  }

  status = 0;

cleanup:
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(dataGroup);
  GPUDestroyBindGroup(paramsGroup);
  GPUDestroyBuffer(paramsBuffer);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(inputBuffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL compute buffer validation passed");
  }

  return status;
}
