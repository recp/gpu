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

#include <us/us.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GPUUSLBackendName {
  const char *name;
  USLBackend  backend;
} GPUUSLBackendName;

static const GPUUSLBackendName gpuUSLBackends[] = {
  {"dxil",   USL_BACKEND_DXIL},
  {"metal",  USL_BACKEND_METAL},
  {"vulkan", USL_BACKEND_SPIRV},
  {"dx12",   USL_BACKEND_HLSL},
  {"webgpu", USL_BACKEND_WGSL},
  {"ptx",    USL_BACKEND_PTX}
};

static int
gpu_usl_read(const char *path, char **outSource, size_t *outSize) {
  char *source;
  FILE *file;
  long  length;

  file = fopen(path, "rb");

  if (!file
      || fseek(file, 0, SEEK_END) != 0
      || (length = ftell(file)) <= 0
      || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return 0;
  }

  if (!(source = malloc((size_t)length + 1u))
      || fread(source, 1u, (size_t)length, file) != (size_t)length) {
    free(source);
    fclose(file);
    return 0;
  }

  fclose(file);
  source[length] = '\0';
  *outSource     = source;
  *outSize       = (size_t)length;

  return 1;
}

static int
gpu_usl_backend(const char *name, USLBackend *outBackend) {
  size_t count = sizeof(gpuUSLBackends) / sizeof(gpuUSLBackends[0]);
  size_t i;

  for (i = 0; i < count; i++) {
    if (strcmp(name, gpuUSLBackends[i].name) == 0) {
      *outBackend = gpuUSLBackends[i].backend;
      return 1;
    }
  }

  return 0;
}

int
main(int argc, char **argv) {
  USLibrary *library    = NULL;
  char      *source     = NULL;
  size_t     sourceSize = 0u;
  USLBackend backend;
  USResult   result;

  if (argc != 3 || !gpu_usl_backend(argv[1], &backend)) {
    fprintf(stderr,
            "usage: %s <dxil|metal|vulkan|dx12|webgpu|ptx> <source.usl>\n",
            argv[0]);
    return 2;
  }

  if (!gpu_usl_read(argv[2], &source, &sourceSize)) {
    fprintf(stderr, "failed to read USL source: %s\n", argv[2]);
    return 1;
  }

  if (!getenv("GPU_USL_FIXTURE_VERBOSE")) {
#ifdef _WIN32
    (void)freopen("NUL", "w", stdout);
#else
    (void)freopen("/dev/null", "w", stdout);
#endif
  }

  us_set_backend(backend);
  result = usl_compile(source, sourceSize, &library, argv[2]);
  free(source);

  if (result != USLOk) {
    fprintf(stderr, "failed to compile USL source: %s\n", argv[2]);
    return 1;
  }

  return 0;
}
