#include "test.h"

#include <stdatomic.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <time.h>
#endif

enum {
  GPU_API_WAIT_STEP_MS    = 10,
  GPU_API_WAIT_STEP_COUNT = 1000
};

typedef struct GPUApiAdapterRequest {
  GPUAdapter  *adapter;
  GPUResult    result;
  atomic_bool  done;
} GPUApiAdapterRequest;

typedef struct GPUApiDeviceRequest {
  GPUDevice   *device;
  GPUResult    result;
  atomic_bool  done;
} GPUApiDeviceRequest;

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
  for (uint32_t i = 0u; i < GPU_API_WAIT_STEP_COUNT; i++) {
    if (atomic_load_explicit(value, memory_order_acquire)) {
      return true;
    }
    sleep_millis(GPU_API_WAIT_STEP_MS);
  }
  return false;
}

static void
adapter_ready(GPUResult result, GPUAdapter *adapter, void *userData) {
  GPUApiAdapterRequest *request;

  request          = userData;
  request->adapter = adapter;
  request->result  = result;
  atomic_store_explicit(&request->done, true, memory_order_release);
}

static void
device_ready(GPUResult result, GPUDevice *device, void *userData) {
  GPUApiDeviceRequest *request;

  request         = userData;
  request->device = device;
  request->result = result;
  atomic_store_explicit(&request->done, true, memory_order_release);
}

GPUResult
gpu_test_request_adapter(GPUInstance *instance, GPUAdapter **outAdapter) {
  GPUApiAdapterRequest request = {0};
  GPUResult             result;

  if (!outAdapter) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outAdapter = NULL;
  result = GPURequestAdapter(instance, adapter_ready, &request);
  if (result != GPU_OK) {
    return result;
  }
  if (!wait_bool(&request.done)) {
    return GPU_ERROR_TIMEOUT;
  }
  *outAdapter = request.adapter;
  return request.result;
}

GPUResult
gpu_test_create_device(GPUAdapter                *adapter,
                       const GPUDeviceCreateInfo *info,
                       GPUDevice                **outDevice) {
  GPUApiDeviceRequest request = {0};
  GPUResult            result;

  if (!outDevice) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outDevice = NULL;
  result = GPURequestDevice(adapter, info, device_ready, &request);
  if (result != GPU_OK) {
    return result;
  }
  if (!wait_bool(&request.done)) {
    return GPU_ERROR_TIMEOUT;
  }
  *outDevice = request.device;
  return request.result;
}

void *
gpu_test_read_file(const char *path, uint64_t *outSize) {
  unsigned char *bytes;
  long length;
  FILE *file;

  if (!path || !outSize) {
    return NULL;
  }

  *outSize = 0u;
  file = fopen(path, "rb");
  if (!file) {
    return NULL;
  }
  if (fseek(file, 0, SEEK_END) != 0 ||
      (length = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    fclose(file);
    return NULL;
  }

  bytes = malloc((size_t)length);
  if (!bytes || fread(bytes, 1u, (size_t)length, file) != (size_t)length) {
    free(bytes);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *outSize = (uint64_t)length;
  return bytes;
}

int
gpu_run_api_tests(const GPUApiTest *tests, uint32_t count) {
  const char *filter = getenv("GPU_API_TEST");
  uint32_t    runCount = 0u;

  if (!tests && count > 0u) {
    fprintf(stderr, "api test runner missing test table\n");
    return 0;
  }

  for (uint32_t i = 0; i < count; i++) {
    if (!tests[i].name || !tests[i].run) {
      fprintf(stderr, "api test runner has invalid test at index %u\n", i);
      return 0;
    }
    if (filter && strcmp(tests[i].name, filter) != 0) {
      continue;
    }

    printf("api:%s\n", tests[i].name);
    fflush(stdout);
    runCount++;
    if (!tests[i].run(tests[i].ctx)) {
      fprintf(stderr, "api test failed: %s\n", tests[i].name);
      return 0;
    }
  }

  if (filter && runCount == 0u) {
    fprintf(stderr, "api test filter matched nothing: %s\n", filter);
    return 0;
  }

  return 1;
}
