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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize);

static void*
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

#if defined(_MSC_VER)
  file = NULL;

  if (path && fopen_s(&file, path, "rb") != 0)
    file = NULL;
#else
  file = path ? fopen(path, "rb") : NULL;
#endif
  if (!file || fseek(file, 0, SEEK_END) != 0
      || (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file)
      fclose(file);
    return NULL;
  }

  if (!(data = malloc((size_t)size)) || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *outSize = (uint64_t)size;

  return data;
}

int
main(int argc, char **argv) {
  void    *artifact;
  uint64_t artifactSize;
  int      valid;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-cuda-ptx-metadata artifact.us\n");
    return 1;
  }

  artifactSize = 0u;

  if (!(artifact = read_file(argv[1], &artifactSize))) {
    fprintf(stderr, "USL artifact read failed\n");
    return 1;
  }

  valid = validate_ptx_metadata(artifact, artifactSize);
  free(artifact);

  return valid ? 0 : 1;
}
