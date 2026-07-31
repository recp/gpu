#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 199309L
#endif

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
gpu_test_request_adapter_options(GPUInstance                    *instance,
                                 const GPUAdapterRequestOptions *options,
                                 GPUAdapter                    **outAdapter) {
  GPUApiAdapterRequest request = {0};
  GPUResult             result;

  if (!outAdapter) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outAdapter = NULL;
  atomic_init(&request.done, false);
  result = GPURequestAdapter(instance, options, adapter_ready, &request);
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
gpu_test_request_adapter(GPUInstance *instance, GPUAdapter **outAdapter) {
  return gpu_test_request_adapter_options(instance, NULL, outAdapter);
}

int
gpu_test_adapter_request_options(GPUInstance *instance) {
  GPUAdapterRequestOptions options = {0};
  GPUApiAdapterRequest     request = {0};
  GPUFeature               features[] = { GPU_FEATURE_COMPUTE };
  GPUAdapterProperties     properties;
  GPUAdapter             **adapters;
  GPUAdapter              *adapter;
  GPUResult                result;
  uint32_t                 adapterCount;
  bool                     hasDiscrete;
  bool                     hasIntegrated;

  adapterCount = 0u;
  result = GPUEnumerateAdapters(instance, &adapterCount, NULL);
  if (result != GPU_OK || adapterCount == 0u) {
    return 0;
  }
  adapters = calloc(adapterCount, sizeof(*adapters));
  if (!adapters) {
    return 0;
  }
  result = GPUEnumerateAdapters(instance, &adapterCount, adapters);
  if (result != GPU_OK) {
    free(adapters);
    return 0;
  }

  hasDiscrete   = false;
  hasIntegrated = false;
  for (uint32_t i = 0u; i < adapterCount; i++) {
    if (!GPUIsFeatureSupported(adapters[i], GPU_FEATURE_COMPUTE) ||
        GPUGetAdapterProperties(adapters[i], &properties) != GPU_OK ||
        (properties.executionFlags & GPU_EXECUTION_GRAPHICS_BIT) == 0u ||
        (properties.executionFlags & GPU_EXECUTION_COMPUTE_BIT) == 0u) {
      continue;
    }
    hasDiscrete   |= properties.type == GPU_ADAPTER_TYPE_DISCRETE;
    hasIntegrated |= properties.type == GPU_ADAPTER_TYPE_INTEGRATED;
  }

  options.chain.sType          = GPU_STRUCTURE_TYPE_ADAPTER_REQUEST_OPTIONS;
  options.chain.structSize     = sizeof(options);
  options.pRequiredFeatures    = features;
  options.requiredFeatureCount = (uint32_t)GPU_ARRAY_LEN(features);

  options.powerPreference = GPU_POWER_PREFERENCE_DEFAULT;
  options.workload        = GPU_WORKLOAD_DEFAULT;
  result = gpu_test_request_adapter_options(instance, &options, &adapter);
  if (result != GPU_OK || adapter != adapters[0]) {
    free(adapters);
    return 0;
  }

  options.workload = GPU_WORKLOAD_GRAPHICS;
  result = gpu_test_request_adapter_options(instance, &options, &adapter);
  if (result != GPU_OK || !adapter ||
      GPUGetAdapterProperties(adapter, &properties) != GPU_OK ||
      (properties.executionFlags & GPU_EXECUTION_GRAPHICS_BIT) == 0u) {
    free(adapters);
    return 0;
  }

  options.workload = GPU_WORKLOAD_COMPUTE;
  result = gpu_test_request_adapter_options(instance, &options, &adapter);
  if (result != GPU_OK || !adapter ||
      GPUGetAdapterProperties(adapter, &properties) != GPU_OK ||
      (properties.executionFlags & GPU_EXECUTION_COMPUTE_BIT) == 0u) {
    free(adapters);
    return 0;
  }

  options.workload = GPU_WORKLOAD_HYBRID;
  result = gpu_test_request_adapter_options(instance, &options, &adapter);
  if (result != GPU_OK || !adapter ||
      GPUGetAdapterProperties(adapter, &properties) != GPU_OK ||
      (properties.executionFlags &
       (GPU_EXECUTION_GRAPHICS_BIT | GPU_EXECUTION_COMPUTE_BIT)) !=
        (GPU_EXECUTION_GRAPHICS_BIT | GPU_EXECUTION_COMPUTE_BIT)) {
    free(adapters);
    return 0;
  }

  options.workload = (GPUWorkload)UINT32_MAX;
  atomic_init(&request.done, false);
  result = GPURequestAdapter(instance, &options, adapter_ready, &request);
  if (result != GPU_ERROR_INVALID_ARGUMENT ||
      atomic_load_explicit(&request.done, memory_order_acquire)) {
    free(adapters);
    return 0;
  }

  options.workload        = GPU_WORKLOAD_DEFAULT;
  options.powerPreference = GPU_POWER_PREFERENCE_LOW_POWER;
  result = gpu_test_request_adapter_options(instance, &options, &adapter);
  if (result != GPU_OK || !adapter ||
      GPUGetAdapterProperties(adapter, &properties) != GPU_OK ||
      (hasIntegrated && properties.type != GPU_ADAPTER_TYPE_INTEGRATED)) {
    free(adapters);
    return 0;
  }

  options.powerPreference = GPU_POWER_PREFERENCE_HIGH_PERFORMANCE;
  result = gpu_test_request_adapter_options(instance, &options, &adapter);
  if (result != GPU_OK || !adapter ||
      GPUGetAdapterProperties(adapter, &properties) != GPU_OK ||
      (hasDiscrete && properties.type != GPU_ADAPTER_TYPE_DISCRETE)) {
    free(adapters);
    return 0;
  }

  options.powerPreference = (GPUPowerPreference)UINT32_MAX;
  atomic_init(&request.done, false);
  result = GPURequestAdapter(instance, &options, adapter_ready, &request);
  if (result != GPU_ERROR_INVALID_ARGUMENT ||
      atomic_load_explicit(&request.done, memory_order_acquire)) {
    free(adapters);
    return 0;
  }

  options.powerPreference      = GPU_POWER_PREFERENCE_DEFAULT;
  options.pRequiredFeatures    = NULL;
  options.requiredFeatureCount = 1u;
  result = GPURequestAdapter(instance, &options, adapter_ready, &request);
  free(adapters);
  return result == GPU_ERROR_INVALID_ARGUMENT &&
         !atomic_load_explicit(&request.done, memory_order_acquire);
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

static bool
test_selected(const char *filter, const char *name) {
  const char *cursor;
  size_t      nameLength;

  if (!filter || filter[0] == '\0') {
    return true;
  }

  nameLength = strlen(name);
  cursor     = filter;
  while (*cursor != '\0') {
    const char *end;

    end = strchr(cursor, ',');
    if (!end) {
      end = cursor + strlen(cursor);
    }
    if ((size_t)(end - cursor) == nameLength &&
        memcmp(cursor, name, nameLength) == 0) {
      return true;
    }
    cursor = *end == ',' ? end + 1 : end;
  }

  return false;
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
    if (!test_selected(filter, tests[i].name)) {
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
