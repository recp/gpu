#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 199309L
#endif

#include <gpu/gpu.h>

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <time.h>
#endif

enum {
  COMMANDS_PER_BATCH = 8,
  BATCH_COUNT        = 8,
  WAIT_STEP_MS       = 10,
  WAIT_STEP_COUNT    = 1000
};

typedef struct AdapterRequest {
  GPUAdapter  *adapter;
  GPUResult    result;
  atomic_bool  done;
} AdapterRequest;

typedef struct DeviceRequest {
  GPUDevice   *device;
  GPUResult    result;
  atomic_bool  done;
} DeviceRequest;

typedef struct CompletionProbe {
  atomic_uint count;
} CompletionProbe;

static void
sleep_millis(uint32_t milliseconds) {
#if defined(_WIN32)
  Sleep(milliseconds);
#else
  struct timespec duration;

  duration.tv_sec  = (time_t)(milliseconds / 1000u);
  duration.tv_nsec = (long)(milliseconds % 1000u) * 1000000l;
  nanosleep(&duration, NULL);
#endif
}

static bool
wait_bool(atomic_bool *value) {
  for (uint32_t i = 0u; i < WAIT_STEP_COUNT; i++) {
    if (atomic_load_explicit(value, memory_order_acquire)) {
      return true;
    }
    sleep_millis(WAIT_STEP_MS);
  }
  return false;
}

static bool
wait_count(atomic_uint *value, uint32_t expected) {
  for (uint32_t i = 0u; i < WAIT_STEP_COUNT; i++) {
    if (atomic_load_explicit(value, memory_order_acquire) >= expected) {
      return true;
    }
    sleep_millis(WAIT_STEP_MS);
  }
  return false;
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  AdapterRequest *request;

  request          = userData;
  request->adapter = adapter;
  request->result  = result;
  atomic_store_explicit(&request->done, true, memory_order_release);
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  DeviceRequest *request;

  request         = userData;
  request->device = device;
  request->result = result;
  atomic_store_explicit(&request->done, true, memory_order_release);
}

static void
command_complete(void *sender, GPUCommandBuffer *cmdb) {
  CompletionProbe *probe;

  (void)cmdb;
  probe = sender;
  atomic_fetch_add_explicit(&probe->count, 1u, memory_order_release);
}

static void
discard_commands(GPUCommandBuffer **commands, uint32_t count) {
  for (uint32_t i = 0u; i < count; i++) {
    if (commands[i]) {
      (void)GPUDiscardCommandBuffer(commands[i]);
    }
  }
}

int
main(void) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUQueueSubmitInfo    submitInfo = {0};
  GPUCommandBuffer     *commands[COMMANDS_PER_BATCH] = {0};
  AdapterRequest        adapterRequest = {0};
  DeviceRequest         deviceRequest = {0};
  CompletionProbe       completion = {0};
  GPUInstance          *instance;
  GPUQueue             *queue;
  GPUResult             result;

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.label            = "webgpu-native-queue-test";
  instanceInfo.preferredBackend = GPU_BACKEND_WEBGPU;
  instanceInfo.enableValidation = true;

  instance = NULL;
  result = GPUCreateInstance(&instanceInfo, &instance);
  if (result != GPU_OK || !instance) {
    fprintf(stderr, "failed to create WebGPU instance: %d\n", result);
    return 1;
  }

  result = GPURequestAdapter(instance, adapter_ready, &adapterRequest);
  if (result != GPU_OK || !wait_bool(&adapterRequest.done) ||
      adapterRequest.result != GPU_OK || !adapterRequest.adapter) {
    fprintf(stderr, "failed to request WebGPU adapter: %d\n",
            adapterRequest.result);
    GPUDestroyInstance(instance);
    return 1;
  }

  result = GPURequestDevice(adapterRequest.adapter,
                            NULL,
                            device_ready,
                            &deviceRequest);
  if (result != GPU_OK || !wait_bool(&deviceRequest.done) ||
      deviceRequest.result != GPU_OK || !deviceRequest.device) {
    fprintf(stderr, "failed to request WebGPU device: %d\n",
            deviceRequest.result);
    GPUDestroyInstance(instance);
    return 1;
  }

  queue = GPUGetQueue(deviceRequest.device, GPU_QUEUE_GRAPHICS, 0u);
  if (!queue) {
    fprintf(stderr, "failed to get WebGPU graphics queue\n");
    GPUDestroyDevice(deviceRequest.device);
    GPUDestroyInstance(instance);
    return 1;
  }

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.ppCommandBuffers   = commands;
  submitInfo.commandBufferCount = COMMANDS_PER_BATCH;

  for (uint32_t batch = 0u; batch < BATCH_COUNT; batch++) {
    uint32_t acquired;

    acquired = 0u;
    for (; acquired < COMMANDS_PER_BATCH; acquired++) {
      commands[acquired] = NULL;
      result = GPUAcquireCommandBuffer(queue,
                                       "webgpu-native-queue-batch",
                                       &commands[acquired]);
      if (result != GPU_OK || !commands[acquired]) {
        fprintf(stderr,
                "failed to acquire WebGPU command %u in batch %u: %d\n",
                acquired,
                batch,
                result);
        discard_commands(commands, acquired);
        GPUDestroyDevice(deviceRequest.device);
        GPUDestroyInstance(instance);
        return 1;
      }
      GPUSetCommandBufferCompletionHandler(commands[acquired],
                                           &completion,
                                           command_complete);
    }

    result = GPUQueueSubmit(queue, &submitInfo);
    if (result != GPU_OK ||
        !wait_count(&completion.count,
                    (batch + 1u) * COMMANDS_PER_BATCH)) {
      fprintf(stderr, "WebGPU batch %u did not complete: %d\n", batch, result);
      GPUDestroyDevice(deviceRequest.device);
      GPUDestroyInstance(instance);
      return 1;
    }
  }

  GPUDestroyDevice(deviceRequest.device);
  GPUDestroyInstance(instance);
  printf("WebGPU native queue completion passed\n");
  return 0;
}
