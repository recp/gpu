#include "test.h"
#include "../../src/api/frame_internal.h"

static int
check_fence_create_validation(GPUDevice *device) {
  GPUFenceCreateInfo fenceInfo = {0};
  GPUFence *fence;

  if (GPUWaitFence(NULL, 0) != GPU_ERROR_INVALID_ARGUMENT ||
      GPUIsFenceSignaled(NULL)) {
    fprintf(stderr, "null fence query behaved unexpectedly\n");
    return 0;
  }
  GPUResetFence(NULL);
  GPUDestroyFence(NULL);

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);

  fence = (GPUFence *)(uintptr_t)1u;
  if (GPUCreateFence(NULL, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted null device\n");
    return 0;
  }
  if (GPUCreateFence(device, &fenceInfo, NULL) != GPU_ERROR_INVALID_ARGUMENT) {
    fprintf(stderr, "fence create accepted null output\n");
    return 0;
  }

  fence = (GPUFence *)(uintptr_t)1u;
  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted wrong sType\n");
    return 0;
  }

  fence = (GPUFence *)(uintptr_t)1u;
  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = (uint32_t)(sizeof(fenceInfo) - 1u);
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_ERROR_INVALID_ARGUMENT ||
      fence != NULL) {
    fprintf(stderr, "fence create accepted short structSize\n");
    return 0;
  }

  fence = NULL;
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK ||
      !fence ||
      GPUIsFenceSignaled(fence) ||
      GPUWaitFence(fence, 0) != GPU_ERROR_TIMEOUT) {
    fprintf(stderr, "fence create default state failed\n");
    GPUDestroyFence(fence);
    return 0;
  }
  GPUDestroyFence(fence);

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.signaled = true;
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK ||
      !fence ||
      !GPUIsFenceSignaled(fence) ||
      GPUWaitFence(fence, 0) != GPU_OK) {
    fprintf(stderr, "fence create signaled state failed\n");
    GPUDestroyFence(fence);
    return 0;
  }
  GPUDestroyFence(fence);

  return 1;
}

static int
check_queue_selection(GPUDevice *device) {
  GPUCommandQueue *graphics0;
  GPUCommandQueue *compute0;

  if (!device) {
    return 0;
  }

  graphics0 = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  compute0 = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0);
  if (!graphics0 || !compute0) {
    fprintf(stderr, "default device missing index-0 queues\n");
    return 0;
  }
  if (GPUGetCommandQueue(device, GPU_QUEUE_GRAPHICS) != graphics0) {
    fprintf(stderr, "GPUGetCommandQueue is not the index-0 graphics alias\n");
    return 0;
  }
  if (GPUGetQueue(device, GPU_QUEUE_GRAPHICS, UINT32_MAX) ||
      GPUGetQueue(device, GPU_QUEUE_COMPUTE, UINT32_MAX) ||
      GPUGetQueue(device, 0, 0)) {
    fprintf(stderr, "queue lookup accepted invalid request\n");
    return 0;
  }

  return 1;
}

static int
check_device_queue_create_validation(GPUPhysicalDevice *physicalDevice) {
  GPUCommandQueueCreateInfo queues[1] = {0};
  GPUDevice *device;
  int ok;

  if (GPUCreateDevice(NULL, NULL, 0)) {
    fprintf(stderr, "device create accepted null physical device\n");
    return 0;
  }

  queues[0].flags = 0;
  queues[0].count = 1;
  if (GPUCreateDevice(physicalDevice, queues, 1)) {
    fprintf(stderr, "device create accepted queue request with no flags\n");
    return 0;
  }

  queues[0].flags = GPU_QUEUE_GRAPHICS;
  queues[0].count = 0;
  if (GPUCreateDevice(physicalDevice, queues, 1)) {
    fprintf(stderr, "device create accepted zero queue count\n");
    return 0;
  }

  queues[0].flags = GPU_QUEUE_GRAPHICS;
  queues[0].count = 2;
  device = GPUCreateDevice(physicalDevice, queues, 1);
  if (!device) {
    fprintf(stderr, "device create rejected explicit queue count\n");
    return 0;
  }

  ok = (GPUGetAvailableQueueBits(device) & GPU_QUEUE_GRAPHICS) == GPU_QUEUE_GRAPHICS &&
       GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0) &&
       GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 1) &&
       !GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 2) &&
       !GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0) &&
       GPUGetCommandQueue(device, GPU_QUEUE_GRAPHICS) ==
         GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!ok) {
    fprintf(stderr, "explicit queue count lookup failed\n");
  }

  GPUDestroyDevice(device);
  return ok;
}

static int
check_queue_submit_fence(GPUDevice *device) {
  GPUCommandQueue *queue;
  GPUCommandBuffer submittedCmdb = {0};
  GPUCommandBuffer noDrawableCmdb = {0};
  GPUCommandBuffer foreignCmdb = {0};
  GPUCommandQueue foreignQueue = {0};
  GPUFrame fakeFrame = {0};
  GPUFrame noDrawableFrame = {0};
  GPUCommandBuffer *cmdb;
  GPUCommandBuffer *buffers[1];
  GPUCommandBuffer *nullBuffers[1];
  GPUCommandBuffer *duplicateBuffers[2];
  GPUFence *fence;
  GPUFenceCreateInfo fenceInfo = {0};
  GPUQueueSubmitInfo submitInfo = {0};
  int ok;

  queue = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0);
  if (!queue) {
    fprintf(stderr, "failed to get graphics queue for fence test\n");
    return 0;
  }
  if (!check_fence_create_validation(device)) {
    return 0;
  }

  fenceInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.chain.structSize = sizeof(fenceInfo);
  fenceInfo.label = "reflection-submit-fence";
  fenceInfo.signaled = true;
  fence = NULL;
  if (GPUCreateFence(device, &fenceInfo, &fence) != GPU_OK || !fence) {
    fprintf(stderr, "failed to create fence\n");
    return 0;
  }

  ok = GPUIsFenceSignaled(fence);
  GPUResetFence(fence);
  ok = ok && !GPUIsFenceSignaled(fence);
  ok = ok && GPUWaitFence(fence, 0) == GPU_ERROR_TIMEOUT;

  submittedCmdb._submitted = true;
  submittedCmdb._priv = (void *)(uintptr_t)0xdeadbeefu;
  fakeFrame.drawable = (void *)(uintptr_t)0xdeadbeefu;
  GPUSchedulePresent(&submittedCmdb, &fakeFrame);
  GPUPresent(&submittedCmdb, &fakeFrame);

  noDrawableCmdb._priv = (void *)(uintptr_t)0xdeadbeefu;
  GPUSchedulePresent(&noDrawableCmdb, &noDrawableFrame);
  GPUPresent(&noDrawableCmdb, &noDrawableFrame);

  ok = ok && GPUQueueSubmit(queue, NULL) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUQueueSubmit(NULL, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUQueueSubmit(queue, &submitInfo) == GPU_ERROR_INVALID_ARGUMENT;
  ok = ok && GPUFinishFrame(NULL, NULL, NULL) == GPU_ERROR_INVALID_ARGUMENT;

  cmdb = NULL;
  if (ok &&
      (GPUAcquireCommandBuffer(queue, "reflection-fence-submit", &cmdb) != GPU_OK ||
       !cmdb)) {
    fprintf(stderr, "failed to acquire command buffer for fence test\n");
    ok = 0;
  }

  if (ok) {
    buffers[0] = cmdb;
    nullBuffers[0] = NULL;

    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted wrong sType\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = (uint32_t)(sizeof(submitInfo) - 1u);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted short structSize\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = nullBuffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted null command buffer\n");
      ok = 0;
    }
  }

  if (ok) {
    foreignCmdb._queue = &foreignQueue;
    buffers[0] = &foreignCmdb;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted command buffer from another queue\n");
      ok = 0;
    }
  }

  if (ok) {
    duplicateBuffers[0] = cmdb;
    duplicateBuffers[1] = cmdb;
    memset(&submitInfo, 0, sizeof(submitInfo));
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 2u;
    submitInfo.ppCommandBuffers = duplicateBuffers;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_ERROR_INVALID_ARGUMENT) {
      fprintf(stderr, "queue submit accepted duplicate command buffer\n");
      ok = 0;
    }
  }

  if (ok) {
    memset(&submitInfo, 0, sizeof(submitInfo));
    buffers[0] = cmdb;
    submitInfo.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
    submitInfo.chain.structSize = sizeof(submitInfo);
    submitInfo.commandBufferCount = 1u;
    submitInfo.ppCommandBuffers = buffers;
    submitInfo.fence = fence;
    if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK ||
        GPUWaitFence(fence, UINT64_MAX) != GPU_OK ||
        !GPUIsFenceSignaled(fence)) {
      fprintf(stderr, "queue submit fence did not signal\n");
      ok = 0;
    }
  }

  GPUDestroyFence(fence);
  return ok;
}

int
gpu_test_queue(GPUPhysicalDevice *physicalDevice, GPUDevice *device) {
  return check_device_queue_create_validation(physicalDevice) &&
         check_queue_selection(device) &&
         check_queue_submit_fence(device);
}
