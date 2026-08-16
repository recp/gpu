#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  ValueCount     = 512u,
  RoundtripCount = 4u
};

typedef struct Params {
  float scale;
  float bias;
} Params;

typedef struct AdapterList {
  GPUAdapter **items;
  uint32_t     count;
} AdapterList;

typedef struct RoundtripState {
  GPUDeviceInteropEXT *interop;
  GPUDevice           *graphicsDevice;
  GPUDevice           *cudaDevice;
  GPUQueue            *graphicsQueue;
  GPUQueue            *cudaQueue;
  GPUBuffer           *graphicsBuffer;
  GPUBuffer           *cudaBuffer;
  GPUBuffer           *paramsBuffer;
  GPUSemaphore        *graphicsSemaphore;
  GPUSemaphore        *cudaSemaphore;
  GPUFence            *releaseFence;
  GPUFence            *acquireFence;
  GPUComputePipeline  *pipeline;
  GPUBindGroup        *paramsGroup;
  GPUBindGroup        *dataGroup;
} RoundtripState;

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

static GPUResult
enumerate_adapters(GPUInstance *instance, AdapterList *outList) {
  GPUResult result;
  uint32_t  count;

  if (!instance || !outList) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  outList->items = NULL;
  outList->count = 0u;
  count          = 0u;
  result = GPUEnumerateAdapters(instance, &count, NULL);
  if (result != GPU_OK || count == 0u) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }

  outList->items = calloc(count, sizeof(*outList->items));
  if (!outList->items) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  outList->count = count;
  result = GPUEnumerateAdapters(instance, &count, outList->items);
  if (result != GPU_OK || count != outList->count) {
    free(outList->items);
    outList->items = NULL;
    outList->count = 0u;
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

static int
find_matching_adapters(const AdapterList *graphicsAdapters,
                       const AdapterList *cudaAdapters,
                       GPUAdapter       **outGraphicsAdapter,
                       GPUAdapter       **outCudaAdapter) {
  for (uint32_t graphicsIndex = 0u;
       graphicsIndex < graphicsAdapters->count;
       graphicsIndex++) {
    for (uint32_t cudaIndex = 0u;
         cudaIndex < cudaAdapters->count;
         cudaIndex++) {
      bool sameDevice;

      sameDevice = false;
      if (GPUAdaptersSharePhysicalDevice(
            graphicsAdapters->items[graphicsIndex],
            cudaAdapters->items[cudaIndex],
            &sameDevice
          ) == GPU_OK && sameDevice) {
        *outGraphicsAdapter = graphicsAdapters->items[graphicsIndex];
        *outCudaAdapter     = cudaAdapters->items[cudaIndex];
        return 1;
      }
    }
  }
  return 0;
}

static int
values_match(const float values[ValueCount]) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    const float expected = (float)i * 2.0f + 1.0f;

    if (fabsf(values[i] - expected) > 0.0001f) {
      return 0;
    }
  }
  return 1;
}

static int
run_roundtrip(RoundtripState *state, uint32_t iteration) {
  GPUCommandBuffer          *releaseCmdb, *cudaCmdb, *acquireCmdb;
  GPUComputePassEncoder     *pass;
  GPUSharedBufferBarrierEXT  toCuda = {0}, toGraphics = {0};
  GPUSharedBarrierBatchEXT   acquireCuda = {0}, acquireGraphics = {0};
  GPUQueueSemaphoreWait      wait = {0};
  GPUQueueSemaphoreSignal    signal = {0};
  GPUQueueSubmitExInfo       submit = {0};
  float                      values[ValueCount];
  uint64_t                   cudaValue, graphicsValue;
  uint32_t                   dynamicOffset;
  int                        releaseSubmitted, cudaSubmitted, ok;

  releaseCmdb      = NULL;
  cudaCmdb         = NULL;
  acquireCmdb      = NULL;
  pass             = NULL;
  releaseSubmitted = 0;
  cudaSubmitted    = 0;
  ok               = 0;
  cudaValue        = (uint64_t)iteration * 2u + 1u;
  graphicsValue    = cudaValue + 1u;
  for (uint32_t i = 0u; i < ValueCount; i++) {
    values[i] = (float)i;
  }

  if (GPUQueueWriteBuffer(state->graphicsQueue,
                          state->graphicsBuffer,
                          0u,
                          values,
                          sizeof(values)) != GPU_OK ||
      GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-release",
                              &releaseCmdb) != GPU_OK ||
      !releaseCmdb) {
    goto cleanup;
  }

  toCuda.sourceBuffer      = state->graphicsBuffer;
  toCuda.destinationBuffer = state->cudaBuffer;
  toCuda.sizeBytes         = sizeof(values);
  toCuda.srcAccess         = GPU_ACCESS_TRANSFER_WRITE;
  toCuda.dstAccess         = GPU_ACCESS_SHADER_WRITE;
  acquireCuda.pBufferBarriers    = &toCuda;
  acquireCuda.srcStages          = GPU_STAGE_TRANSFER;
  acquireCuda.dstStages          = GPU_STAGE_COMPUTE;
  acquireCuda.bufferBarrierCount = 1u;
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                releaseCmdb,
                                &acquireCuda) != GPU_OK) {
    goto cleanup;
  }

  signal.semaphore          = state->graphicsSemaphore;
  signal.value              = cudaValue;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = &releaseCmdb;
  submit.pSignals           = &signal;
  submit.fence              = state->releaseFence;
  submit.commandBufferCount = 1u;
  submit.signalCount        = 1u;
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  releaseCmdb      = NULL;
  releaseSubmitted = 1;

  if (GPUAcquireCommandBuffer(state->cudaQueue,
                              "cuda-graphics-roundtrip",
                              &cudaCmdb) != GPU_OK ||
      !cudaCmdb ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                cudaCmdb,
                                &acquireCuda) != GPU_OK ||
      !(pass = GPUBeginComputePass(cudaCmdb, "cuda-saxpy"))) {
    goto cleanup;
  }
  GPUBindComputePipeline(pass, state->pipeline);
  dynamicOffset = 0u;
  GPUBindComputeGroup(pass, 0u, state->paramsGroup, 1u, &dynamicOffset);
  GPUBindComputeGroup(pass, 1u, state->dataGroup, 0u, NULL);
  GPUDispatch(pass, ValueCount / 256u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  toGraphics.sourceBuffer      = state->cudaBuffer;
  toGraphics.destinationBuffer = state->graphicsBuffer;
  toGraphics.sizeBytes         = sizeof(values);
  toGraphics.srcAccess         = GPU_ACCESS_SHADER_WRITE;
  toGraphics.dstAccess         = GPU_ACCESS_TRANSFER_READ;
  acquireGraphics.pBufferBarriers    = &toGraphics;
  acquireGraphics.srcStages          = GPU_STAGE_COMPUTE;
  acquireGraphics.dstStages          = GPU_STAGE_TRANSFER;
  acquireGraphics.bufferBarrierCount = 1u;
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                cudaCmdb,
                                &acquireGraphics) != GPU_OK) {
    goto cleanup;
  }

  wait.semaphore            = state->cudaSemaphore;
  wait.value                = cudaValue;
  wait.waitStages           = GPU_STAGE_COMPUTE;
  signal.semaphore          = state->cudaSemaphore;
  signal.value              = graphicsValue;
  submit.ppCommandBuffers   = &cudaCmdb;
  submit.pWaits             = &wait;
  submit.pSignals           = &signal;
  submit.fence              = NULL;
  submit.waitCount          = 1u;
  submit.signalCount        = 1u;
  if (GPUQueueSubmitEx(state->cudaQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  cudaCmdb      = NULL;
  cudaSubmitted = 1;

  if (GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-acquire",
                              &acquireCmdb) != GPU_OK ||
      !acquireCmdb ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                acquireCmdb,
                                &acquireGraphics) != GPU_OK) {
    goto cleanup;
  }

  wait.semaphore            = state->graphicsSemaphore;
  wait.value                = graphicsValue;
  wait.waitStages           = GPU_STAGE_TRANSFER;
  submit.ppCommandBuffers   = &acquireCmdb;
  submit.pWaits             = &wait;
  submit.pSignals           = NULL;
  submit.fence              = state->acquireFence;
  submit.signalCount        = 0u;
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  acquireCmdb = NULL;
  if (GPUWaitFence(state->acquireFence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(state->graphicsQueue,
                         state->graphicsBuffer,
                         0u,
                         values,
                         sizeof(values)) != GPU_OK ||
      !values_match(values)) {
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  if (acquireCmdb) {
    (void)GPUDiscardCommandBuffer(acquireCmdb);
  }
  if (cudaCmdb) {
    (void)GPUDiscardCommandBuffer(cudaCmdb);
  }
  if (releaseCmdb) {
    (void)GPUDiscardCommandBuffer(releaseCmdb);
  }
  if (!ok && cudaSubmitted) {
    (void)GPUQueueReadBuffer(state->cudaQueue,
                             state->cudaBuffer,
                             0u,
                             values,
                             sizeof(values));
  }
  if (!ok && releaseSubmitted) {
    (void)GPUWaitFence(state->releaseFence, UINT64_MAX);
  }
  GPUResetFence(state->acquireFence);
  GPUResetFence(state->releaseFence);
  return ok;
}

int
main(int argc, char **argv) {
  GPUInstance                 *graphicsInstance, *cudaInstance;
  GPUAdapter                  *graphicsAdapter, *cudaAdapter;
  GPUShaderLibrary            *library;
  GPUShaderLayout             *shaderLayout;
  void                        *artifact;
  GPUInstanceCreateInfo        graphicsInstanceInfo = {0};
  GPUInstanceCreateInfo        cudaInstanceInfo = {0};
  GPUBufferCreateInfo          graphicsBufferInfo = {0};
  GPUBufferCreateInfo          cudaBufferInfo = {0};
  GPUBufferCreateInfo          paramsBufferInfo = {0};
  GPUSemaphoreCreateInfo       semaphoreInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBindGroupEntry            paramsEntry = {0};
  GPUBindGroupEntry            dataEntries[2] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUMemoryRequirements        memoryRequirements;
  AdapterList                  graphicsAdapters = {0}, cudaAdapters = {0};
  RoundtripState               state = {0};
  uint64_t                     artifactSize;
  Params                       params = {2.0f, 1.0f};
  int                          status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-graphics-cuda-interop-usl artifact.us\n");
    return 1;
  }

  graphicsInstance = NULL;
  cudaInstance     = NULL;
  graphicsAdapter  = NULL;
  cudaAdapter      = NULL;
  library          = NULL;
  shaderLayout     = NULL;
  artifactSize     = 0u;
  artifact         = read_file(argv[1], &artifactSize);
  status           = 1;
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  graphicsInstanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  graphicsInstanceInfo.chain.structSize = sizeof(graphicsInstanceInfo);
#if defined(_WIN32) || defined(WIN32)
  graphicsInstanceInfo.preferredBackend = GPU_BACKEND_DX12;
#else
  graphicsInstanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
#endif
  graphicsInstanceInfo.enableValidation = true;
  cudaInstanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  cudaInstanceInfo.chain.structSize = sizeof(cudaInstanceInfo);
  cudaInstanceInfo.preferredBackend = GPU_BACKEND_CUDA;
  cudaInstanceInfo.enableValidation = true;
  if (GPUCreateInstance(&graphicsInstanceInfo, &graphicsInstance) != GPU_OK ||
      !graphicsInstance ||
      GPUCreateInstance(&cudaInstanceInfo, &cudaInstance) != GPU_OK ||
      !cudaInstance ||
      enumerate_adapters(graphicsInstance, &graphicsAdapters) != GPU_OK ||
      enumerate_adapters(cudaInstance, &cudaAdapters) != GPU_OK ||
      !find_matching_adapters(&graphicsAdapters,
                              &cudaAdapters,
                              &graphicsAdapter,
                              &cudaAdapter)) {
    puts("matching graphics/CUDA adapters unavailable");
    status = 77;
    goto cleanup;
  }

  state.graphicsDevice = GPUCreateDeviceWithDefaultQueues(graphicsAdapter);
  state.cudaDevice     = GPUCreateDeviceWithDefaultQueues(cudaAdapter);
  state.graphicsQueue  = GPUGetQueue(state.graphicsDevice,
                                     GPU_QUEUE_GRAPHICS,
                                     0u);
  state.cudaQueue      = GPUGetQueue(state.cudaDevice, GPU_QUEUE_COMPUTE, 0u);
  if (!state.graphicsDevice || !state.cudaDevice ||
      !state.graphicsQueue || !state.cudaQueue ||
      GPUCreateDeviceInteropEXT(state.graphicsDevice,
                                state.cudaDevice,
                                &state.interop) != GPU_OK ||
      !state.interop) {
    fprintf(stderr, "graphics/CUDA device interop creation failed\n");
    goto cleanup;
  }

  graphicsBufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  graphicsBufferInfo.chain.structSize = sizeof(graphicsBufferInfo);
  graphicsBufferInfo.label       = "graphics-cuda-buffer";
  graphicsBufferInfo.sizeBytes   = sizeof(float) * ValueCount;
  graphicsBufferInfo.usage       = GPU_BUFFER_USAGE_COPY_SRC |
                                   GPU_BUFFER_USAGE_COPY_DST;
  cudaBufferInfo                 = graphicsBufferInfo;
  cudaBufferInfo.label           = "cuda-graphics-buffer";
  cudaBufferInfo.usage           = GPU_BUFFER_USAGE_STORAGE |
                                   GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUGetSharedBufferMemoryRequirementsEXT(state.interop,
                                                &graphicsBufferInfo,
                                                &cudaBufferInfo,
                                                &memoryRequirements) != GPU_OK ||
      memoryRequirements.sizeBytes < graphicsBufferInfo.sizeBytes ||
      GPUCreateSharedBufferEXT(state.interop,
                               &graphicsBufferInfo,
                               &cudaBufferInfo,
                               &state.graphicsBuffer,
                               &state.cudaBuffer) != GPU_OK ||
      !state.graphicsBuffer || !state.cudaBuffer) {
    fprintf(stderr, "shared graphics/CUDA buffer creation failed\n");
    goto cleanup;
  }

  semaphoreInfo.chain.sType      = GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.chain.structSize = sizeof(semaphoreInfo);
  semaphoreInfo.label            = "graphics-cuda-timeline";
  if (GPUCreateSharedSemaphoreEXT(state.interop,
                                  &semaphoreInfo,
                                  &state.graphicsSemaphore,
                                  &state.cudaSemaphore) != GPU_OK ||
      !state.graphicsSemaphore || !state.cudaSemaphore ||
      GPUCreateFence(state.graphicsDevice, NULL, &state.releaseFence) !=
        GPU_OK ||
      GPUCreateFence(state.graphicsDevice, NULL, &state.acquireFence) !=
        GPU_OK ||
      !state.releaseFence || !state.acquireFence) {
    fprintf(stderr, "shared graphics/CUDA synchronization creation failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(state.cudaDevice,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(state.cudaDevice,
                            library,
                            &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA USL shader layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "graphics-cuda-saxpy";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "saxpy";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.pipeline) != GPU_OK ||
      !state.pipeline) {
    fprintf(stderr, "CUDA interop compute pipeline creation failed\n");
    goto cleanup;
  }

  paramsBufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  paramsBufferInfo.chain.structSize = sizeof(paramsBufferInfo);
  paramsBufferInfo.label            = "graphics-cuda-params";
  paramsBufferInfo.sizeBytes        = sizeof(params);
  paramsBufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                      GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state.cudaDevice,
                      &paramsBufferInfo,
                      &state.paramsBuffer) != GPU_OK ||
      !state.paramsBuffer ||
      GPUQueueWriteBuffer(state.cudaQueue,
                          state.paramsBuffer,
                          0u,
                          &params,
                          sizeof(params)) != GPU_OK) {
    fprintf(stderr, "CUDA interop params buffer creation failed\n");
    goto cleanup;
  }

  paramsEntry.binding       = 0u;
  paramsEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  paramsEntry.buffer.buffer = state.paramsBuffer;
  paramsEntry.buffer.size   = sizeof(params);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "graphics-cuda-params-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &paramsEntry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.paramsGroup) != GPU_OK ||
      !state.paramsGroup) {
    fprintf(stderr, "CUDA interop params bind group creation failed\n");
    goto cleanup;
  }

  dataEntries[0].binding       = 0u;
  dataEntries[0].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  dataEntries[0].buffer.buffer = state.cudaBuffer;
  dataEntries[0].buffer.size   = cudaBufferInfo.sizeBytes;
  dataEntries[1].binding       = 1u;
  dataEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  dataEntries[1].buffer.buffer = state.cudaBuffer;
  dataEntries[1].buffer.size   = cudaBufferInfo.sizeBytes;
  groupInfo.label              = "graphics-cuda-data-group";
  groupInfo.layout             = shaderLayout->bindGroupLayouts[1];
  groupInfo.pEntries           = dataEntries;
  groupInfo.entryCount         = 2u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.dataGroup) != GPU_OK ||
      !state.dataGroup) {
    fprintf(stderr, "CUDA interop data bind group creation failed\n");
    goto cleanup;
  }

  for (uint32_t iteration = 0u; iteration < RoundtripCount; iteration++) {
    if (!run_roundtrip(&state, iteration)) {
      fprintf(stderr,
              "graphics/CUDA roundtrip %u failed\n",
              iteration + 1u);
      goto cleanup;
    }
  }
  status = 0;

cleanup:
  GPUDestroyBindGroup(state.dataGroup);
  GPUDestroyBindGroup(state.paramsGroup);
  GPUDestroyComputePipeline(state.pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyFence(state.acquireFence);
  GPUDestroyFence(state.releaseFence);
  GPUDestroySemaphore(state.cudaSemaphore);
  GPUDestroySemaphore(state.graphicsSemaphore);
  GPUDestroyBuffer(state.cudaBuffer);
  GPUDestroyBuffer(state.graphicsBuffer);
  GPUDestroyBuffer(state.paramsBuffer);
  GPUDestroyDeviceInteropEXT(state.interop);
  GPUDestroyDevice(state.cudaDevice);
  GPUDestroyDevice(state.graphicsDevice);
  free(cudaAdapters.items);
  free(graphicsAdapters.items);
  GPUDestroyInstance(cudaInstance);
  GPUDestroyInstance(graphicsInstance);
  free(artifact);

  if (status == 0) {
    puts("graphics/CUDA USL interop validation passed");
  }
  return status;
}
