/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* a failed pipeline or shader library, named for the app: which one (label,
 * entry points), which payload (kind, size, the hash GPU_USL_LOG printed when
 * it was made) and what the driver or compiler said. Backends leave the
 * driver's words for this thread (pipelineDriverMessage) and the api layer
 * reports once, through GPUSetDeviceErrorCallback, else on stderr (also with
 * GPU_USL_LOG). Nothing runs unless a creation fails. */

#include "../common.h"
#include "device_internal.h"
#include "library_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER) && !defined(__clang__)
#  define REPORT_THREAD_LOCAL __declspec(thread)
#else
#  define REPORT_THREAD_LOCAL _Thread_local
#endif

static REPORT_THREAD_LOCAL char gpu_driverMessage[512];

GPU_HIDE
void
pipelineDriverMessage(const char *format, ...) {
  va_list args;

  if (!format) {
    gpu_driverMessage[0] = '\0';
    return;
  }

  va_start(args, format);
  vsnprintf(gpu_driverMessage, sizeof(gpu_driverMessage), format, args);
  va_end(args);
}

static const char*
report__resultName(GPUResult result) {
  switch (result) {
    case GPU_OK:                          return "ok";
    case GPU_ERROR_INVALID_ARGUMENT:      return "invalid argument";
    case GPU_ERROR_UNSUPPORTED:           return "unsupported";
    case GPU_ERROR_OUT_OF_MEMORY:         return "out of memory";
    case GPU_ERROR_BACKEND_FAILURE:       return "backend failure";
    case GPU_ERROR_INSUFFICIENT_CAPACITY: return "insufficient capacity";
    case GPU_ERROR_TIMEOUT:               return "timeout";
  }

  return "error";
}

static const char*
report__payloadKind(const GPUShaderLibrary *library) {
  switch (library && library->_api ? library->_api->backend : GPU_BACKEND_DEFAULT) {
    case GPU_BACKEND_METAL:  return library->_payloadBinary ? "metallib" : "MSL";
    case GPU_BACKEND_VULKAN: return "SPIR-V";
    case GPU_BACKEND_DX12:   return library->_payloadBinary ? "DXIL" : "HLSL";
    case GPU_BACKEND_WEBGPU: return "WGSL";
    case GPU_BACKEND_CUDA:   return "PTX";
    default:                 return "shader";
  }
}

static size_t
report__append(char *buffer, size_t used, size_t size, const char *format, ...) {
  va_list args;
  int     n;

  if (used >= size)
    return used;

  va_start(args, format);
  n = vsnprintf(buffer + used, size - used, format, args);
  va_end(args);

  return n < 0 ? used : used + (size_t)n;
}

GPU_HIDE
void
pipelineFailed(GPUDevice              *device,
               GPUResult               result,
               const char             *kind,
               const char             *label,
               const GPUShaderLibrary *library,
               const char *const      *stages,
               const char *const      *entries,
               uint32_t                entryCount) {
  GPUDeviceErrorType type;
  char               message[1024];
  size_t             used;
  uint32_t           i;

  used = report__append(message, 0u, sizeof(message), "%s \"%s\" failed (%s)",
                        kind ? kind : "pipeline", label ? label : "unlabeled", report__resultName(result));

  for (i = 0u; i < entryCount; i++) {
    if (entries && entries[i])
      used = report__append(message, used, sizeof(message), "%s %s %s", i ? "," : ":",
                            stages && stages[i] ? stages[i] : "entry", entries[i]);
  }

  if (library && library->_payloadSize) {
    used = report__append(message, used, sizeof(message), "; %s payload %llu bytes",
                          report__payloadKind(library), (unsigned long long)library->_payloadSize);
    if (library->_payloadHash)
      used = report__append(message, used, sizeof(message), ", %016llx",
                            (unsigned long long)library->_payloadHash);
  }

  if (gpu_driverMessage[0])
    used = report__append(message, used, sizeof(message), "; driver: %s", gpu_driverMessage);

  gpu_driverMessage[0] = '\0';
  (void)used;

  /* without a callback the app would hear nothing: stderr, as backends'
   * own logs used to */
  if (!device || !device->errorCallback || getenv("GPU_USL_LOG"))
    fprintf(stderr, "GPU: %s\n", message);

  if (device && device->errorCallback) {
    type = result == GPU_ERROR_INVALID_ARGUMENT ? GPU_DEVICE_ERROR_VALIDATION
         : result == GPU_ERROR_OUT_OF_MEMORY    ? GPU_DEVICE_ERROR_OUT_OF_MEMORY
                                                : GPU_DEVICE_ERROR_BACKEND;
    deviceReportError(device, type, GPU_DEVICE_LOST_REASON_UNKNOWN, result, message);
  }
}
