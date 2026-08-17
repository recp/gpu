#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize);

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) fclose(file);
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
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
  artifact     = read_file(argv[1], &artifactSize);
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    return 1;
  }

  valid = validate_ptx_metadata(artifact, artifactSize);
  free(artifact);
  return valid ? 0 : 1;
}
