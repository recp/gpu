#include "test.h"
#include "../../src/api/device_internal.h"

enum {
  GPU_UNTYPED_POINTER_VALUE_COUNT = 4u
};

int
gpu_test_untyped_pointer(GPUDevice *device, const char *bytecodePath) {
  GPUComputePipelineCreateInfo pipelineInfo  = {0};
  GPUBufferCreateInfo          bufferInfo    = {0};
  GPUBindGroupCreateInfo       groupInfo     = {0};
  GPUQueueSubmitInfo           submitInfo    = {0};
  GPUBindGroupEntry            entry         = {0};
  GPUCommandBuffer            *submitList[1] = {0};
  uint32_t                     values[GPU_UNTYPED_POINTER_VALUE_COUNT] = {0};
  GPUApi                       *api;
  GPUQueue                     *queue        = NULL;
  GPUShaderLibrary             *library      = NULL;
  GPUShaderLayout              *shaderLayout = NULL;
  GPUComputePipeline           *pipeline     = NULL;
  GPUBuffer                    *buffer       = NULL;
  GPUBindGroup                 *group        = NULL;
  GPUCommandBuffer             *cmdb         = NULL;
  GPUComputePassEncoder        *pass         = NULL;
  GPUFence                     *fence        = NULL;
  void                         *bytecode      = NULL;
  uint64_t                      bytecodeSize  = 0u;
  GPUResult                     submitResult;
  int                           ok            = 0;

  api = gpuDeviceApi(device);
  if (!api || api->backend != GPU_BACKEND_VULKAN) {
    return 1;
  }
  if (!device->uslUntypedPointers) {
    puts("untyped-pointer execution skipped: unsupported adapter");
    return 1;
  }
  if (!bytecodePath ||
      !(bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize))) {
    fprintf(stderr, "untyped-pointer artifact load failed\n");
    goto cleanup;
  }

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "untyped-pointer shader setup failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "untyped_pointer_cs";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "untyped-pointer pipeline creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.sizeBytes        = sizeof(values);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK || !buffer ||
      GPUQueueWriteBuffer(queue,
                          buffer,
                          0u,
                          values,
                          sizeof(values)) != GPU_OK) {
    fprintf(stderr, "untyped-pointer buffer setup failed\n");
    goto cleanup;
  }

  entry.binding       = 0u;
  entry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entry.buffer.buffer = buffer;
  entry.buffer.size   = sizeof(values);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &entry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group ||
      GPUAcquireCommandBuffer(queue, "untyped-pointer", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "untyped-pointer"))) {
    fprintf(stderr, "untyped-pointer command setup failed\n");
    goto cleanup;
  }

  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, GPU_UNTYPED_POINTER_VALUE_COUNT, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "untyped-pointer fence creation failed\n");
    goto cleanup;
  }
  submitList[0]                  = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  submitResult = GPUQueueSubmit(queue, &submitInfo);
  if (submitResult != GPU_OK) {
    fprintf(stderr, "untyped-pointer submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;
  if (GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    fprintf(stderr, "untyped-pointer fence wait failed\n");
    goto cleanup;
  }

  if (GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         values,
                         sizeof(values)) != GPU_OK) {
    fprintf(stderr, "untyped-pointer readback failed\n");
    goto cleanup;
  }
  for (uint32_t i = 0u; i < GPU_UNTYPED_POINTER_VALUE_COUNT; i++) {
    uint32_t expected;

    expected = i * 3u + 7u;
    if (values[i] != expected) {
      fprintf(stderr,
              "untyped-pointer mismatch at %u: %u != %u\n",
              i,
              values[i],
              expected);
      goto cleanup;
    }
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  if (cmdb) {
    GPUDiscardCommandBuffer(cmdb);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(buffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  free(bytecode);
  return ok;
}
