#define GPU_COMPUTE_ARTIFACT_PATH "/subgroup.us"
#define GPU_COMPUTE_ENTRY_POINT "fill_subgroup_vertices"
#define GPU_COMPUTE_DISPATCH_X 1u
#define GPU_COMPUTE_VERTEX_CAPACITY 32u
#define GPU_COMPUTE_REQUIRED_FEATURE GPU_FEATURE_SUBGROUPS
#define GPU_COMPUTE_FALLBACK_ARTIFACT_PATH "/subgroup_fallback.us"
#define GPU_COMPUTE_FALLBACK_ENTRY_POINT "fill_subgroup_vertices_emulated"
#define GPU_COMPUTE_FALLBACK_READY_STATUS \
  "GPU: subgroup shuffle emulated with workgroup memory"
#define GPU_COMPUTE_UNSUPPORTED_STATUS \
  "GPU: subgroups unsupported by this adapter"
#define GPU_COMPUTE_READY_STATUS "GPU: USL subgroup shuffle ready"

#include "../compute/main.c"
