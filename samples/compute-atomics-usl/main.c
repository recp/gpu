#include <gpu/gpu.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef GPU_SAMPLE_BACKEND
#error GPU_SAMPLE_BACKEND must select the sample backend
#endif

enum {
  GPU_ATOMIC_VALUE_COUNT       = 208,
  GPU_ATOMIC_STORAGE_OLD_BASE  = 16,
  GPU_ATOMIC_SHARED_OLD_BASE   = 80,
  GPU_ATOMIC_STORAGE_BASE      = 144,
  GPU_ATOMIC_THREAD_COUNT      = 64
};

static const uint32_t kExpectedSummary[] = {
  64u, 64u, 0u, 63u, 9u, 0u, 7u, 17u, 0u, 13u
};

static const char *
backend_name(GPUBackend backend) {
  switch (backend) {
    case GPU_BACKEND_METAL:
      return "Metal";
    case GPU_BACKEND_VULKAN:
      return "Vulkan";
    case GPU_BACKEND_DX12:
      return "Direct3D 12";
    default:
      return "unknown";
  }
}

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = fopen(path, "rb");
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

static int
results_match(const uint32_t *values) {
  uint64_t sharedSeen;
  uint64_t storageSeen;
  size_t   i;

  sharedSeen  = 0u;
  storageSeen = 0u;
  for (i = 0u; i < sizeof(kExpectedSummary) / sizeof(kExpectedSummary[0]); i++) {
    if (values[i] != kExpectedSummary[i]) {
      fprintf(stderr,
              "atomic summary mismatch at %zu: expected %u, got %u\n",
              i,
              kExpectedSummary[i],
              values[i]);
      return 0;
    }
  }
  for (i = 0u; i < GPU_ATOMIC_THREAD_COUNT; i++) {
    uint32_t sharedOld;
    uint32_t storageOld;

    sharedOld  = values[GPU_ATOMIC_SHARED_OLD_BASE + i];
    storageOld = values[GPU_ATOMIC_STORAGE_OLD_BASE + i];
    if (sharedOld >= GPU_ATOMIC_THREAD_COUNT ||
        storageOld >= GPU_ATOMIC_THREAD_COUNT ||
        (sharedSeen & (UINT64_C(1) << sharedOld)) != 0u ||
        (storageSeen & (UINT64_C(1) << storageOld)) != 0u) {
      fprintf(stderr,
              "atomic old-value permutation mismatch at %zu: %u, %u\n",
              i,
              sharedOld,
              storageOld);
      return 0;
    }
    sharedSeen  |= UINT64_C(1) << sharedOld;
    storageSeen |= UINT64_C(1) << storageOld;
  }
  if (sharedSeen != UINT64_MAX || storageSeen != UINT64_MAX) {
    fprintf(stderr, "atomic old-value permutation incomplete\n");
    return 0;
  }
  for (i = GPU_ATOMIC_STORAGE_BASE;
       i < GPU_ATOMIC_STORAGE_BASE + GPU_ATOMIC_THREAD_COUNT;
       i++) {
    if (values[i] != 17u) {
      fprintf(stderr,
              "storage atomic mismatch at %zu: expected 17, got %u\n",
              i,
              values[i]);
      return 0;
    }
  }
  return 1;
}

int
main(int argc, char **argv) {
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUQueue              *queue;
  GPUShaderLibrary      *library;
  GPUShaderLayout       *shaderLayout;
  GPUComputePipeline    *pipeline;
  GPUBuffer             *buffer;
  GPUBindGroup          *bindGroup;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  GPUFence              *fence;
  void                  *artifact;
  const char            *artifactPath;
  const char            *backendName;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPUDeviceCreateInfo          deviceInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            groupEntry = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  GPUCommandBuffer            *submitList[1] = {0};
  uint32_t                     values[GPU_ATOMIC_VALUE_COUNT] = {0};
  const GPUFeature             requiredFeatures[] = {GPU_FEATURE_COMPUTE};
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUResult                      result;
  uint64_t                       artifactSize;
  uint32_t                       adapterCount;
  uint32_t                       layoutEntryCount;
  int                            ok;

  if (argc > 2) {
    fprintf(stderr, "usage: gpu-compute-atomics-usl [artifact.us]\n");
    return 1;
  }

  instance       = NULL;
  adapter        = NULL;
  device         = NULL;
  queue          = NULL;
  library        = NULL;
  shaderLayout   = NULL;
  pipeline       = NULL;
  buffer         = NULL;
  bindGroup      = NULL;
  cmdb           = NULL;
  pass           = NULL;
  fence          = NULL;
  artifact       = NULL;
  artifactSize   = 0u;
  artifactPath   = argc == 2 ? argv[1] : "compute_atomics.us";
  backendName    = backend_name(GPU_SAMPLE_BACKEND);
  ok             = 0;

  artifact = read_file(artifactPath, &artifactSize);
  if (!artifact) {
    fprintf(stderr, "%s USL artifact read failed\n", backendName);
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_SAMPLE_BACKEND;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "%s instance creation failed\n", backendName);
    goto cleanup;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    fprintf(stderr, "%s adapter enumeration failed\n", backendName);
    goto cleanup;
  }

  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = requiredFeatures;
  deviceInfo.required.featureCount =
    (uint32_t)(sizeof(requiredFeatures) / sizeof(requiredFeatures[0]));
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "%s device creation failed\n", backendName);
    goto cleanup;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!queue) {
    fprintf(stderr, "%s compute queue unavailable\n", backendName);
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || !shaderLayout->pipelineLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts[0]) {
    fprintf(stderr, "%s USL shader layout creation failed\n", backendName);
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_STORAGE_BUFFER ||
      layoutEntries[0].visibility != GPU_SHADER_STAGE_COMPUTE_BIT ||
      layoutEntries[0].arrayCount != 1u ||
      layoutEntries[0].hasDynamicOffset) {
    fprintf(stderr, "%s atomic reflection layout mismatch\n", backendName);
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "usl-memory-atomics";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "verify_atomics";
  if (GPUCreateComputePipeline(device, &pipelineInfo, &pipeline) != GPU_OK ||
      !pipeline) {
    fprintf(stderr, "%s atomic pipeline creation failed\n", backendName);
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "usl-memory-atomic-values";
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
    fprintf(stderr, "%s atomic buffer creation failed\n", backendName);
    goto cleanup;
  }

  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  groupEntry.buffer.buffer = buffer;
  groupEntry.buffer.size   = sizeof(values);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "usl-memory-atomic-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &groupEntry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(device, &groupInfo, &bindGroup) != GPU_OK ||
      !bindGroup) {
    fprintf(stderr, "%s atomic bind group creation failed\n", backendName);
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "usl-memory-atomics", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "verify-atomics"))) {
    fprintf(stderr, "%s atomic command encoding failed\n", backendName);
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipeline);
  GPUBindComputeGroup(pass, 0u, bindGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "%s atomic fence creation failed\n", backendName);
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
    fprintf(stderr, "%s atomic submit failed\n", backendName);
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         buffer,
                         0u,
                         values,
                         sizeof(values)) != GPU_OK ||
      !results_match(values)) {
    fprintf(stderr, "%s atomic readback validation failed\n", backendName);
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  GPUDestroyFence(fence);
  GPUDestroyBindGroup(bindGroup);
  GPUDestroyBuffer(buffer);
  GPUDestroyComputePipeline(pipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (!ok) {
    return 1;
  }
  printf("%s USL memory atomic validation passed\n", backendName);
  return 0;
}
