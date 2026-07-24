#include "test.h"

static int
gpu_testBufferDescriptorArray(GPUDevice *device,
                              const char *bytecodePath,
                              bool bindless,
                              bool dynamic) {
  GPUQueue                     *queue;
  GPUShaderLibrary             *library;
  GPUShaderLayout              *shaderLayout;
  GPUBindGroupLayout           *bindlessLayout;
  GPUPipelineLayout            *bindlessPipelineLayout;
  GPUBindGroupLayout           *activeGroupLayout;
  GPUPipelineLayout            *activePipelineLayout;
  GPUComputePipeline           *pipeline;
  GPUBindGroup                 *group;
  GPUBuffer                    *inputs[2];
  GPUBuffer                    *selectionBuffer;
  GPUBuffer                    *outputBuffer;
  GPUCommandBuffer             *cmdb;
  GPUCommandBuffer             *submitBuffers[1];
  GPUComputePassEncoder        *pass;
  GPUFence                     *fence;
  void                         *bytecode;
  GPUComputePipelineCreateInfo  pipelineInfo = {0};
  GPUBufferCreateInfo           bufferInfo   = {0};
  GPUBindGroupEntry             entries[4]   = {0};
  GPUBindGroupCreateInfo        groupInfo    = {0};
  GPUBindlessLayoutEXT          bindlessInfo = {0};
  GPUBindGroupLayoutCreateInfo  bindlessLayoutInfo = {0};
  GPUPipelineLayoutCreateInfo   pipelineLayoutInfo = {0};
  GPUBindGroupLayout           *pipelineGroups[2];
  GPUQueueSubmitInfo            submitInfo   = {0};
  float                         values[2][4] = {
    {1.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 1.0f, 0.0f, 1.0f}
  };
  float                         output[4] = {0.0f};
  float                         updated[4] = {0.0f};
  uint32_t                      selection[64] = {1u};
  uint32_t                      dynamicOffsets[2] = {256u, 256u};
  uint64_t                      bytecodeSize;
  int                           ok;

  queue           = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  library         = NULL;
  shaderLayout    = NULL;
  bindlessLayout  = NULL;
  bindlessPipelineLayout = NULL;
  activeGroupLayout      = NULL;
  activePipelineLayout   = NULL;
  pipeline        = NULL;
  group           = NULL;
  inputs[0]       = NULL;
  inputs[1]       = NULL;
  selectionBuffer = NULL;
  outputBuffer    = NULL;
  cmdb            = NULL;
  pass            = NULL;
  fence           = NULL;
  bytecodeSize    = 0u;
  bytecode        = gpu_test_read_file(bytecodePath, &bytecodeSize);
  ok              = queue && bytecode;
  if (!ok) {
    fprintf(stderr, "buffer descriptor array fixture setup failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "buffer descriptor array shader setup failed\n");
    ok = 0;
    goto cleanup;
  }

  activeGroupLayout    = shaderLayout->bindGroupLayouts[1];
  activePipelineLayout = shaderLayout->pipelineLayout;
  if (bindless) {
    bindlessInfo.chain.sType      = GPU_STRUCTURE_TYPE_BINDLESS_LAYOUT_EXT;
    bindlessInfo.chain.structSize = sizeof(bindlessInfo);
    bindlessInfo.sourceLayout     = activeGroupLayout;
    bindlessLayoutInfo.chain.sType =
      GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
    bindlessLayoutInfo.chain.structSize = sizeof(bindlessLayoutInfo);
    bindlessLayoutInfo.chain.pNext      = &bindlessInfo;
    bindlessLayoutInfo.label = "api-buffer-descriptor-array-bindless";
    if (GPUCreateBindGroupLayout(device,
                                 &bindlessLayoutInfo,
                                 &bindlessLayout) != GPU_OK ||
        !bindlessLayout) {
      fprintf(stderr, "buffer descriptor array bindless layout failed\n");
      ok = 0;
      goto cleanup;
    }

    pipelineGroups[0] = shaderLayout->bindGroupLayouts[0];
    pipelineGroups[1] = bindlessLayout;
    pipelineLayoutInfo.chain.sType =
      GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.chain.structSize = sizeof(pipelineLayoutInfo);
    pipelineLayoutInfo.label = "api-buffer-descriptor-array-bindless";
    pipelineLayoutInfo.bindGroupLayoutCount = 2u;
    pipelineLayoutInfo.ppBindGroupLayouts = pipelineGroups;
    if (GPUCreatePipelineLayout(device,
                                &pipelineLayoutInfo,
                                &bindlessPipelineLayout) != GPU_OK ||
        !bindlessPipelineLayout) {
      fprintf(stderr,
              "buffer descriptor array bindless pipeline layout failed\n");
      ok = 0;
      goto cleanup;
    }
    activeGroupLayout    = bindlessLayout;
    activePipelineLayout = bindlessPipelineLayout;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "api-buffer-descriptor-array";
  pipelineInfo.layout           = activePipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "buffer_descriptor_array_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "buffer descriptor array pipeline failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "api-buffer-descriptor-array-input";
  bufferInfo.sizeBytes        = sizeof(values[0]) + (dynamic ? 256u : 0u);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t i = 0u; i < 2u; i++) {
    if (GPUCreateBuffer(device, &bufferInfo, &inputs[i]) != GPU_OK ||
        !inputs[i] ||
        GPUQueueWriteBuffer(queue,
                            inputs[i],
                            dynamic ? 256u : 0u,
                            values[i],
                            sizeof(values[i])) != GPU_OK) {
      fprintf(stderr, "buffer descriptor array input creation failed\n");
      ok = 0;
      goto cleanup;
    }
  }

  bufferInfo.label     = "api-buffer-descriptor-array-selection";
  bufferInfo.sizeBytes = sizeof(selection);
  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &selectionBuffer) != GPU_OK ||
      !selectionBuffer ||
      GPUQueueWriteBuffer(queue,
                          selectionBuffer,
                          0u,
                          selection,
                          sizeof(selection)) != GPU_OK) {
    fprintf(stderr, "buffer descriptor array selection creation failed\n");
    ok = 0;
    goto cleanup;
  }

  bufferInfo.label     = "api-buffer-descriptor-array-output";
  bufferInfo.sizeBytes = sizeof(output);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "buffer descriptor array output creation failed\n");
    ok = 0;
    goto cleanup;
  }

  for (uint32_t i = 0u; i < 2u; i++) {
    entries[i].binding       = 0u;
    entries[i].arrayIndex    = i;
    entries[i].bindingType   = dynamic
                                 ? GPU_BINDING_STORAGE_BUFFER
                                 : GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
    entries[i].buffer.buffer = inputs[i];
    entries[i].buffer.size   = sizeof(values[i]);
  }
  entries[2].binding       = 2u;
  entries[2].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entries[2].buffer.buffer = selectionBuffer;
  entries[2].buffer.size   = sizeof(selection);
  entries[3].binding       = 3u;
  entries[3].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[3].buffer.buffer = outputBuffer;
  entries[3].buffer.size   = sizeof(output);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "api-buffer-descriptor-array";
  groupInfo.layout           = activeGroupLayout;
  groupInfo.entryCount       = bindless
                                 ? 0u
                                 : (uint32_t)GPU_ARRAY_LEN(entries);
  groupInfo.pEntries         = bindless ? NULL : entries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "buffer descriptor array bind group failed\n");
    ok = 0;
    goto cleanup;
  }
  if (bindless) {
    entries[1].buffer.buffer = inputs[0];
    if (GPUUpdateBindGroupEXT(group, 2u, entries) != GPU_OK ||
        GPUUpdateBindGroupEXT(group, 2u, &entries[2]) != GPU_OK) {
      fprintf(stderr,
              "buffer descriptor array bindless partial update failed\n");
      ok = 0;
      goto cleanup;
    }

    entries[1].buffer.buffer = inputs[1];
    if (GPUUpdateBindGroupEXT(group, 1u, &entries[1]) != GPU_OK) {
      fprintf(stderr,
              "buffer descriptor array bindless replacement failed\n");
      ok = 0;
      goto cleanup;
    }
  }

  if (GPUAcquireCommandBuffer(queue,
                              "api-buffer-descriptor-array",
                              &cmdb) != GPU_OK ||
      !cmdb ||
      !(pass = GPUBeginComputePass(cmdb,
                                   "api-buffer-descriptor-array"))) {
    fprintf(stderr, "buffer descriptor array pass failed\n");
    ok = 0;
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass,
                      1u,
                      group,
                      dynamic ? 2u : 0u,
                      dynamic ? dynamicOffsets : NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "buffer descriptor array fence failed\n");
    ok = 0;
    goto cleanup;
  }
  submitBuffers[0]              = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = submitBuffers;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "buffer descriptor array submission failed\n");
    cmdb = NULL;
    ok = 0;
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      output[0] < -0.01f || output[0] > 0.01f ||
      output[1] < 0.99f || output[1] > 1.01f ||
      output[2] < -0.01f || output[2] > 0.01f ||
      output[3] < 0.99f || output[3] > 1.01f) {
    fprintf(stderr,
            "buffer descriptor array mismatch: %.3f %.3f %.3f %.3f\n",
            output[0],
            output[1],
            output[2],
            output[3]);
    ok = 0;
    goto cleanup;
  }
  if (dynamic &&
      (GPUQueueReadBuffer(queue,
                          inputs[1],
                          256u,
                          updated,
                          sizeof(updated)) != GPU_OK ||
       updated[0] < 0.24f || updated[0] > 0.26f ||
       updated[1] < 0.49f || updated[1] > 0.51f ||
       updated[2] < 0.74f || updated[2] > 0.76f ||
       updated[3] < 0.99f || updated[3] > 1.01f)) {
    fprintf(stderr,
            "buffer descriptor array write mismatch: %.3f %.3f %.3f %.3f\n",
            updated[0],
            updated[1],
            updated[2],
            updated[3]);
    ok = 0;
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(selectionBuffer);
  GPUDestroyBuffer(inputs[1]);
  GPUDestroyBuffer(inputs[0]);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyPipelineLayout(bindlessPipelineLayout);
  GPUDestroyBindGroupLayout(bindlessLayout);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}

int
gpu_test_buffer_descriptor_array(GPUAdapter *adapter,
                                 const char *bytecodePath,
                                 const char *dynamicBytecodePath) {
  GPUFeature          features[2] = {
    GPU_FEATURE_DESCRIPTOR_INDEXING,
    GPU_FEATURE_BINDLESS
  };
  GPUDeviceCreateInfo deviceInfo = {0};
  GPUDevice          *device;
  bool                bindless;
  int                 ok;

  if (!GPUIsFeatureSupported(adapter, features[0])) {
    puts("buffer descriptor array skipped: unsupported adapter");
    return 1;
  }
  if (!bytecodePath || !dynamicBytecodePath) {
    fprintf(stderr, "buffer descriptor array artifact is missing\n");
    return 0;
  }

  deviceInfo.chain.sType            = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize       = sizeof(deviceInfo);
  bindless                          = GPUIsFeatureSupported(adapter,
                                                           features[1]);
  deviceInfo.required.featureCount = bindless ? 2u : 1u;
  deviceInfo.required.pFeatures    = features;
  device                            = NULL;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "buffer descriptor array device creation failed\n");
    return 0;
  }

  ok = gpu_testBufferDescriptorArray(device, bytecodePath, false, false);
  if (ok && bindless) {
    ok = gpu_testBufferDescriptorArray(device, bytecodePath, true, false);
  }
  if (ok) {
    ok = gpu_testBufferDescriptorArray(device,
                                       dynamicBytecodePath,
                                       false,
                                       true);
  }
  GPUDestroyDevice(device);
  return ok;
}
