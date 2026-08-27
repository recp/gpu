#ifndef gpu_win32_sample_h
#define gpu_win32_sample_h

#include <windows.h>

#include <gpu/gpu.h>

#include <stdbool.h>
#include <stdlib.h>

#define GPU_SAMPLE_SKIP_RETURN_CODE 77

typedef struct GPUSampleAdapterRequest {
  GPUAdapter *adapter;
  HANDLE      completed;
  GPUResult   result;
} GPUSampleAdapterRequest;

static inline void
GPUSampleAdapterReady(GPUResult  result,
                      GPUAdapter *adapter,
                      void       *userData) {
  GPUSampleAdapterRequest *request;

  request          = userData;
  request->adapter = adapter;
  request->result  = result;
  SetEvent(request->completed);
}

static inline GPUResult
GPUSampleRequestFeatureAdapter(GPUInstance *instance,
                               GPUFeature   feature,
                               GPUAdapter **outAdapter) {
  GPUAdapterRequestOptions options = {0};
  GPUSampleAdapterRequest  request = {0};
  GPUResult                result;
  DWORD                    waitResult;

  if (!instance || !outAdapter) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  *outAdapter = NULL;
  request.completed = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (!request.completed) {
    return GPU_ERROR_BACKEND_FAILURE;
  }

  options.chain.sType          = GPU_STRUCTURE_TYPE_ADAPTER_REQUEST_OPTIONS;
  options.chain.structSize     = sizeof(options);
  options.pRequiredFeatures    = &feature;
  options.requiredFeatureCount = 1u;
  options.powerPreference      = GPU_POWER_PREFERENCE_HIGH_PERFORMANCE;
  options.workload             = GPU_WORKLOAD_GRAPHICS;
  result = GPURequestAdapter(instance,
                             &options,
                             GPUSampleAdapterReady,
                             &request);
  if (result == GPU_OK) {
    waitResult = WaitForSingleObject(request.completed, INFINITE);
    if (waitResult == WAIT_OBJECT_0) {
      result = request.result;
      if (result == GPU_OK) {
        *outAdapter = request.adapter;
      }
    } else {
      result = GPU_ERROR_BACKEND_FAILURE;
    }
  }
  CloseHandle(request.completed);
  return result;
}

static inline bool
GPUSampleShouldSkipNonInteractive(void) {
  const char     *enabled;
  HWINSTA         station;
  USEROBJECTFLAGS flags;
  DWORD           size;

  enabled = getenv("GPU_SAMPLE_SKIP_NONINTERACTIVE");
  if (!enabled || enabled[0] == '\0' ||
      (enabled[0] == '0' && enabled[1] == '\0')) {
    return false;
  }

  station = GetProcessWindowStation();
  size    = 0u;
  return !station ||
         !GetUserObjectInformationW(station,
                                    UOI_FLAGS,
                                    &flags,
                                    sizeof(flags),
                                    &size) ||
         (flags.dwFlags & WSF_VISIBLE) == 0u;
}

#endif
