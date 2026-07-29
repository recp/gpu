#ifndef gpu_linux_native_samples_h
#define gpu_linux_native_samples_h

#include <stddef.h>

typedef struct GPUNativeSample {
  const char *id;
  const char *executable;
  const char *preview;
} GPUNativeSample;

extern const GPUNativeSample gpuNativeSamples[];
extern const size_t          gpuNativeSampleCount;

#endif
