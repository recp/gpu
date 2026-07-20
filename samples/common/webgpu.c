#include "webgpu.h"

#include <emscripten/emscripten.h>

#include <stdio.h>
#include <stdlib.h>

EM_JS(void, set_status_js, (const char *message, int failed), {
  const status = document.getElementById("status");
  status.textContent = UTF8ToString(message);
  status.dataset.failed = failed ? "true" : "false";
});

void
set_status(const char *message, int failed) {
  set_status_js(message, failed);
  if (failed) {
    fprintf(stderr, "%s\n", message);
  } else {
    puts(message);
  }
}

int
read_file(const char *path, void **outData, uint64_t *outSize) {
  void *data;
  long  size;
  FILE *file;

  file = fopen(path, "rb");
  if (!file ||
      fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 ||
      fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
    return 0;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return 0;
  }

  fclose(file);
  *outData = data;
  *outSize = (uint64_t)size;
  return 1;
}
