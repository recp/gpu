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

#include "bench.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <time.h>
#endif

static int
bench_compare(const void *left, const void *right) {
  double a;
  double b;

  a = *(const double *)left;
  b = *(const double *)right;
  return (a > b) - (a < b);
}

const char *
bench_backendName(GPUBackend backend) {
  switch (backend) {
    case GPU_BACKEND_METAL:
      return "metal";
    case GPU_BACKEND_VULKAN:
      return "vulkan";
    case GPU_BACKEND_DX12:
      return "dx12";
    case GPU_BACKEND_OPENGL:
      return "opengl";
    default:
      return "default";
  }
}

bool
bench_parseBackend(const char *value, GPUBackend *outBackend) {
  if (!value || !outBackend) {
    return false;
  }
  if (strcmp(value, "default") == 0) {
    *outBackend = GPU_BACKEND_DEFAULT;
  } else if (strcmp(value, "metal") == 0) {
    *outBackend = GPU_BACKEND_METAL;
  } else if (strcmp(value, "vulkan") == 0) {
    *outBackend = GPU_BACKEND_VULKAN;
  } else if (strcmp(value, "dx12") == 0) {
    *outBackend = GPU_BACKEND_DX12;
  } else {
    return false;
  }
  return true;
}

bool
bench_parseU32(const char *value, uint32_t minimum, uint32_t *outValue) {
  unsigned long parsed;
  char         *end;

  if (!value || !outValue) {
    return false;
  }
  errno  = 0;
  parsed = strtoul(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' ||
      parsed < minimum || parsed > UINT32_MAX) {
    return false;
  }
  *outValue = (uint32_t)parsed;
  return true;
}

double
bench_now(void) {
#if defined(_WIN32) || defined(WIN32)
  LARGE_INTEGER counter;
  LARGE_INTEGER frequency;

  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);
  return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
  struct timespec time;

#  if defined(CLOCK_MONOTONIC_RAW)
  clock_gettime(CLOCK_MONOTONIC_RAW, &time);
#  else
  clock_gettime(CLOCK_MONOTONIC, &time);
#  endif
  return (double)time.tv_sec + (double)time.tv_nsec * 1e-9;
#endif
}

double
bench_percentile(double *values, size_t count, double percentile) {
  size_t index;

  if (!values || count == 0u || percentile < 0.0 || percentile > 1.0) {
    return 0.0;
  }

  qsort(values, count, sizeof(*values), bench_compare);
  index = (size_t)(percentile * (double)(count - 1u) + 0.5);
  return values[index];
}

void *
bench_read(const char *path, uint64_t *outSize) {
  unsigned char *bytes;
  long           length;
  FILE          *file;

  if (!path || !outSize) {
    return NULL;
  }

  *outSize = 0u;
  file     = fopen(path, "rb");
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
