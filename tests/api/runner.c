#include "test.h"

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
