#ifndef gpu_sample_stats_h
#define gpu_sample_stats_h

#include <gpu/gpu.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define GPU_SAMPLE_STATS_WARMUP_FRAMES 16u

static inline bool
GPUSampleEnvEnabled(const char *name) {
  const char *value;

  value = getenv(name);
  return value && value[0] != '\0' &&
         !(value[0] == '0' && value[1] == '\0');
}

static inline bool
GPUSampleCheckZeroAlloc(GPUDevice  *device,
                        uint32_t    frameIndex,
                        bool        enabled,
                        const char *label) {
  GPUFrameStats stats;

  if (!enabled || frameIndex <= GPU_SAMPLE_STATS_WARMUP_FRAMES) {
    return true;
  }
  if (GPUGetLastFrameStats(device, &stats) != GPU_OK) {
    fprintf(stderr, "%s: failed to read frame stats\n", label);
    return false;
  }
  if (stats.hotPathAllocCount == 0u && stats.hotPathAllocBytes == 0u &&
      stats.hotPathFreeCount == 0u && stats.hotPathFreeBytes == 0u) {
    return true;
  }

  fprintf(stderr,
          "%s: frame %u allocated %llu bytes in %llu calls and freed "
          "%llu bytes in %llu calls\n",
          label,
          frameIndex,
          (unsigned long long)stats.hotPathAllocBytes,
          (unsigned long long)stats.hotPathAllocCount,
          (unsigned long long)stats.hotPathFreeBytes,
          (unsigned long long)stats.hotPathFreeCount);
  return false;
}

#endif
