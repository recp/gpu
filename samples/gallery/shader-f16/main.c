#define GPU_COMPUTE_ARTIFACT_PATH "/shader_f16.us"
#define GPU_COMPUTE_ENTRY_POINT "fill_f16_vertices"
#define GPU_COMPUTE_REQUIRED_FEATURE GPU_FEATURE_SHADER_F16
#define GPU_COMPUTE_FALLBACK_ARTIFACT_PATH "/shader_f16_fallback.us"
#define GPU_COMPUTE_FALLBACK_ENTRY_POINT "fill_f16_vertices_emulated"
#define GPU_COMPUTE_FALLBACK_READY_STATUS \
  "GPU: shader-f16 emulated with f32 arithmetic"
#define GPU_COMPUTE_UNSUPPORTED_STATUS \
  "GPU: shader-f16 unsupported by this adapter"
#define GPU_COMPUTE_READY_STATUS "GPU: USL shader-f16 ready"

#include "../compute/main.c"
