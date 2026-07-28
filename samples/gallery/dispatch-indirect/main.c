#define GPU_COMPUTE_ARTIFACT_PATH "/dispatch_indirect.us"
#define GPU_COMPUTE_ENTRY_POINT "fill_indirect_vertices"
#define GPU_COMPUTE_USE_INDIRECT 1
#define GPU_COMPUTE_READY_STATUS "GPU: USL indirect dispatch ready"

#include "../compute/main.c"
