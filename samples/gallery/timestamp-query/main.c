#define GPU_COMPUTE_ARTIFACT_PATH "/timestamp_query.us"
#define GPU_COMPUTE_ENTRY_POINT "fill_timestamp_vertices"
#define GPU_COMPUTE_USE_TIMESTAMPS 1
#define GPU_COMPUTE_REQUIRED_FEATURE GPU_FEATURE_TIMESTAMPS
#define GPU_COMPUTE_UNSUPPORTED_STATUS \
  "GPU: timestamps unsupported by this adapter"
#define GPU_COMPUTE_READY_STATUS "GPU: USL pass timestamps ready"
#define GPU_COMPUTE_TIMESTAMP_RESOLVED_STATUS \
  "GPU: USL compute and render timestamps resolved"

#include "../compute/main.c"
