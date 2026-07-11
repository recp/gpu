#include <gpu/gpu.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct CompletionProbe {
  GPUCommandBuffer *cmdb;
  uint32_t          count;
} CompletionProbe;

static void
on_complete(void             * __restrict sender,
            GPUCommandBuffer * __restrict cmdb) {
  CompletionProbe *probe;

  probe = sender;
  if (!probe) {
    return;
  }

  probe->cmdb = cmdb;
  probe->count++;
}

static GPUAdapter*
first_adapter(GPUInstance *instance) {
  GPUAdapter *adapter;
  uint32_t    count;
  GPUResult   result;

  adapter = NULL;
  count   = 1u;
  result  = GPUEnumerateAdapters(instance, &count, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      count == 0u) {
    return NULL;
  }
  return adapter;
}

static GPUCommandBuffer*
submit_empty(GPUCommandQueue *queue,
             GPUFence        *fence,
             CompletionProbe *probe) {
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUQueueSubmitInfo submitInfo;

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(queue, "dx12-empty", &cmdb) != GPU_OK || !cmdb) {
    return NULL;
  }

  GPUSetCommandBufferCompletionHandler(cmdb, probe, on_complete);
  buffers[0] = cmdb;
  memset(&submitInfo, 0, sizeof(submitInfo));
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
      GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
      !GPUIsFenceSignaled(fence)) {
    return NULL;
  }
  return cmdb;
}

int
main(void) {
  GPUInstanceCreateInfo instanceInfo;
  GPUDeviceCreateInfo   deviceInfo;
  GPUFenceCreateInfo    fenceInfo;
  GPUQueueRequest       queueRequest;
  GPUFeature            requiredFeature;
  GPUCommandBuffer     *firstCmdb;
  GPUCommandBuffer     *secondCmdb;
  GPUCommandQueue      *queue0;
  GPUCommandQueue      *queue1;
  CompletionProbe       probe;
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUFence             *fence;

  memset(&instanceInfo, 0, sizeof(instanceInfo));
  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DX12;
  instanceInfo.enableValidation = true;

  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "DX12 instance creation failed\n");
    return 1;
  }

  adapter = first_adapter(instance);
  if (!adapter) {
    fprintf(stderr, "DX12 adapter enumeration failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_COMPUTE) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureSupported(adapter, GPU_FEATURE_MULTI_DRAW)) {
    fprintf(stderr, "DX12 feature reporting failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }

  memset(&queueRequest, 0, sizeof(queueRequest));
  queueRequest.type  = GPU_QUEUE_GRAPHICS;
  queueRequest.count = 2u;

  memset(&deviceInfo, 0, sizeof(deviceInfo));
  deviceInfo.chain.sType             = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize        = sizeof(deviceInfo);
  requiredFeature                    = GPU_FEATURE_MULTI_DRAW;
  deviceInfo.required.featureCount   = 1u;
  deviceInfo.required.pFeatures      = &requiredFeature;
  deviceInfo.queues.chain.sType      = GPU_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  deviceInfo.queues.chain.structSize = sizeof(deviceInfo.queues);
  deviceInfo.queues.requestCount     = 1u;
  deviceInfo.queues.pRequests        = &queueRequest;

  device = NULL;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "DX12 device creation failed\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (GPUIsFeatureEnabled(device, GPU_FEATURE_COMPUTE) ||
      GPUIsFeatureEnabled(device, GPU_FEATURE_INDIRECT_DRAW) ||
      !GPUIsFeatureEnabled(device, GPU_FEATURE_MULTI_DRAW)) {
    fprintf(stderr, "DX12 enabled wrong feature set\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  queue0 = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  queue1 = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 1u);
  if (!queue0 || !queue1 || queue0 == queue1 ||
      GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 2u) ||
      GPUGetAvailableQueueBits(device) != GPU_QUEUE_GRAPHICS) {
    fprintf(stderr, "DX12 indexed queue lookup failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  memset(&fenceInfo, 0, sizeof(fenceInfo));
  fenceInfo.chain.sType      = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "DX12 fence creation failed\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }

  memset(&probe, 0, sizeof(probe));
  firstCmdb = submit_empty(queue0, fence, &probe);
  if (!firstCmdb || probe.count != 1u || probe.cmdb != firstCmdb) {
    fprintf(stderr, "DX12 first submit failed\n");
    goto fail;
  }

  secondCmdb = submit_empty(queue0, fence, &probe);
  if (!secondCmdb || secondCmdb != firstCmdb ||
      probe.count != 2u || probe.cmdb != secondCmdb) {
    fprintf(stderr, "DX12 command-buffer reuse failed\n");
    goto fail;
  }

  if (!submit_empty(queue1, fence, &probe) || probe.count != 3u) {
    fprintf(stderr, "DX12 second queue submit failed\n");
    goto fail;
  }

  GPUDestroyFence(fence);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  printf("DX12 queue validation passed\n");
  return 0;

fail:
  GPUDestroyFence(fence);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  return 1;
}
