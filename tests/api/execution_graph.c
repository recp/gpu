#include "test.h"
#include "../../src/api/buffer_internal.h"
#include "../../src/api/device_internal.h"
#include "../../src/api/execution_graph_internal.h"

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
  GPUApi                            api      = {0};
  GPUDevice                         device   = {0};
  GPUBuffer                         buffer   = {0};
  GPUExecutionGraphEXT              graph    = {0};
  GPUExecutionGraphInstanceEXT      instance = {0};
  GPUComputePassEncoder             pass     = {0};
  GPUExecutionGraphInputEXT         cpuInput = {0};
  GPUExecutionGraphBufferInputEXT   bufferInput = {0};
  _Alignas(16) uint8_t              records[48] = {0};

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
