#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/descr/descriptor_internal.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/execution_graph_internal.h"
#include "../../src/api/library_internal.h"

static uint32_t gGraphCpuDispatches;
static uint32_t gGraphBufferDispatches;

static void
graph_dispatch(GPUComputePassEncoder           *pass,
               GPUExecutionGraphInstanceEXT    *instance,
               uint32_t                         inputCount,
               const GPUExecutionGraphInputEXT *inputs) {
  (void)pass;
  (void)instance;
  (void)inputCount;
  (void)inputs;
  gGraphCpuDispatches++;
}

static void
graph_dispatch_buffer(GPUComputePassEncoder                 *pass,
                      GPUExecutionGraphInstanceEXT          *instance,
                      uint32_t                               inputCount,
                      const GPUExecutionGraphBufferInputEXT *inputs) {
  (void)pass;
  (void)instance;
  (void)inputCount;
  (void)inputs;
  gGraphBufferDispatches++;
}

int
gpu_test_execution_graph_validation(void) {
  GPUApi                           api           = {0};
  GPUDevice                        device        = {0};
  GPUDevice                        foreignDevice = {0};
  GPUBuffer                        buffer        = {0};
  GPUExecutionGraphEXT             graph         = {0};
  GPUExecutionGraphInstanceEXT     instance      = {0};
  GPUExecutionGraphCreateInfoEXT   graphInfo     = {0};
  GPUComputePassEncoder            pass          = {0};
  GPUPipelineLayout                layout        = {0};
  GPUShaderLibrary                 library       = {0};
  GPUExecutionGraphInputEXT        cpuInput      = {0};
  GPUExecutionGraphBufferInputEXT  bufferInput   = {0};
  GPUExecutionGraphEXT            *createdGraph  = NULL;
  _Alignas(16) uint8_t             records[48]   = {0};

  api.executionGraph.dispatch       = graph_dispatch;
  api.executionGraph.dispatchBuffer = graph_dispatch_buffer;
  device._api                       = &api;
  graph._api                        = &api;
  graph.device                      = &device;
  instance._api                     = &api;
  instance.device                   = &device;
  instance.graph                    = &graph;
  pass._api                         = &api;
  pass._device                      = &device;
  pass._pipeline                    = &graph;
  pass._hasPipeline                 = true;
  pass._executionGraph              = true;

  graphInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_CREATE_INFO_EXT;
  graphInfo.chain.structSize = sizeof(graphInfo);
  graphInfo.library          = &library;
  graphInfo.layout           = &layout;
  library._device            = &foreignDevice;
  layout._device             = &device;
  if (GPUCreateExecutionGraphEXT(&device, &graphInfo, &createdGraph) !=
        GPU_ERROR_INVALID_ARGUMENT ||
      createdGraph) {
    fprintf(stderr, "execution graph accepted a foreign shader library\n");
    return 0;
  }

  cpuInput.pRecords                   = records;
  cpuInput.entry.recordSizeBytes      = 16u;
  cpuInput.entry.recordAlignmentBytes = 16u;
  cpuInput.recordCount                = 3u;
  GPUDispatchExecutionGraphEXT(&pass, &instance, 1u, &cpuInput);
  if (gGraphCpuDispatches != 1u) {
    fprintf(stderr, "packed execution graph CPU input was rejected\n");
    return 0;
  }

  cpuInput.pRecords = records + 1u;
  GPUDispatchExecutionGraphEXT(&pass, &instance, 1u, &cpuInput);
  if (gGraphCpuDispatches != 1u) {
    fprintf(stderr, "misaligned execution graph CPU input was accepted\n");
    return 0;
  }

  buffer.device    = &device;
  buffer.sizeBytes = 32u;
  buffer.usage     = GPU_BUFFER_USAGE_DEVICE_ADDRESS_EXT |
                     GPU_BUFFER_USAGE_INDIRECT;
  bufferInput.records                    = &buffer;
  bufferInput.entry.recordSizeBytes      = 16u;
  bufferInput.entry.recordAlignmentBytes = 16u;
  bufferInput.recordCount                = 3u;
  GPUDispatchExecutionGraphBufferEXT(&pass,
                                     &instance,
                                     1u,
                                     &bufferInput);
  if (gGraphBufferDispatches != 0u) {
    fprintf(stderr, "short execution graph input buffer was accepted\n");
    return 0;
  }

  buffer.sizeBytes = sizeof(records);
  GPUDispatchExecutionGraphBufferEXT(&pass,
                                     &instance,
                                     1u,
                                     &bufferInput);
  if (gGraphBufferDispatches != 1u) {
    fprintf(stderr, "packed execution graph buffer input was rejected\n");
    return 0;
  }

  return 1;
}

int
gpu_test_execution_graph(GPUAdapter *adapter, const char *bytecodePath) {
  static const uint32_t expected = 0x47524150u;
  GPUFeature                                  feature =
    GPU_FEATURE_EXECUTION_GRAPH;
  GPUDeviceCreateInfo                         deviceInfo   = {0};
  GPUExecutionGraphCreateInfoEXT              graphInfo    = {0};
  GPUExecutionGraphInstanceCreateInfoEXT      instanceInfo = {0};
  GPUExecutionGraphMemoryRequirementsEXT      requirements = {0};
  GPUExecutionGraphEntryEXT                   entry        = {0};
  GPUExecutionGraphInputEXT                   input        = {0};
  GPUBufferCreateInfo                         bufferInfo   = {0};
  GPUBindGroupEntry                           groupEntry   = {0};
  GPUBindGroupCreateInfo                      groupInfo    = {0};
  GPUQueueSubmitInfo                          submitInfo   = {0};
  GPUCommandBuffer                           *submitList[1] = {0};
  GPUDevice                                  *device       = NULL;
  GPUQueue                                   *queue        = NULL;
  GPUShaderLibrary                           *library      = NULL;
  GPUShaderLayout                            *shaderLayout = NULL;
  GPUExecutionGraphEXT                       *graph        = NULL;
  GPUExecutionGraphInstanceEXT               *instance     = NULL;
  GPUBuffer                                  *outputBuffer = NULL;
  GPUBindGroup                               *group        = NULL;
  GPUCommandBuffer                           *cmdb         = NULL;
  GPUComputePassEncoder                      *pass         = NULL;
  GPUFence                                   *fence        = NULL;
  void                                       *bytecode     = NULL;
  uint64_t                                    bytecodeSize = 0u;
  uint32_t                                    output       = 0u;
  int                                         ok           = 0;

  if (!adapter) {
    return 0;
  }
  if (!GPUIsFeatureSupported(adapter, feature)) {
    puts("execution-graph runtime skipped: unsupported adapter");
    return 1;
  }
  if (!bytecodePath) {
    puts("execution-graph runtime skipped: fixture unavailable");
    return 1;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device ||
      !GPUIsFeatureEnabled(device, feature) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_BUFFER_DEVICE_ADDRESS)) {
    fprintf(stderr, "execution-graph device setup failed\n");
    goto cleanup;
  }

  queue    = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  bytecode = gpu_test_read_file(bytecodePath, &bytecodeSize);
  if (!queue || !bytecode ||
      GPUCreateShaderLibraryFromUSL(device,
                                    bytecode,
                                    bytecodeSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u) {
    fprintf(stderr, "execution-graph shader setup failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "execution-graph-output";
  bufferInfo.sizeBytes        = sizeof(output);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          &output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "execution-graph output setup failed\n");
    goto cleanup;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = outputBuffer;
  groupEntry.buffer.size   = sizeof(output);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "execution-graph-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &groupEntry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "execution-graph bind group setup failed\n");
    goto cleanup;
  }

  graphInfo.chain.sType      = GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_CREATE_INFO_EXT;
  graphInfo.chain.structSize = sizeof(graphInfo);
  graphInfo.label            = "execution-graph";
  graphInfo.library          = library;
  graphInfo.layout           = shaderLayout->pipelineLayout;
  graphInfo.graphName        = "gpu_execution_graph";
  if (GPUCreateExecutionGraphEXT(device, &graphInfo, &graph) != GPU_OK ||
      !graph ||
      GPUGetExecutionGraphMemoryRequirementsEXT(graph,
                                                &requirements) != GPU_OK ||
      requirements.maxSizeBytes < requirements.minSizeBytes) {
    fprintf(stderr, "execution-graph creation failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_EXECUTION_GRAPH_INSTANCE_CREATE_INFO_EXT;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "execution-graph-instance";
  instanceInfo.graph            = graph;
  if (GPUCreateExecutionGraphInstanceEXT(device,
                                         &instanceInfo,
                                         &instance) != GPU_OK ||
      !instance ||
      GPUGetExecutionGraphEntryEXT(graph, "graph_entry", &entry) != GPU_OK) {
    fprintf(stderr, "execution-graph instance setup failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "execution-graph", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "execution-graph"))) {
    fprintf(stderr, "execution-graph command setup failed\n");
    goto cleanup;
  }
  GPUBindExecutionGraphEXT(pass, graph);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  input.entry       = entry;
  input.recordCount = 1u;
  GPUDispatchExecutionGraphEXT(pass, instance, 1u, &input);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "execution-graph fence creation failed\n");
    goto cleanup;
  }
  submitList[0]                  = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = submitList;
  submitInfo.commandBufferCount = 1u;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK) {
    cmdb = NULL;
    fprintf(stderr, "execution-graph submit failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         &output,
                         sizeof(output)) != GPU_OK ||
      output != expected) {
    fprintf(stderr, "execution-graph output mismatch: 0x%08x\n", output);
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  free(bytecode);
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyExecutionGraphInstanceEXT(instance);
  GPUDestroyExecutionGraphEXT(graph);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  return ok;
}
