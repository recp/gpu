#ifndef GPU_SAMPLE_WEBGPU_H
#define GPU_SAMPLE_WEBGPU_H

#include <stdint.h>

void
set_status(const char *message, int failed);

int
read_file(const char *path, void **outData, uint64_t *outSize);

#endif /* GPU_SAMPLE_WEBGPU_H */
