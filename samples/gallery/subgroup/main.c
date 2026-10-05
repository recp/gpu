/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define GPU_COMPUTE_ARTIFACT_PATH          "/subgroup.us"
#define GPU_COMPUTE_ENTRY_POINT            "fill_subgroup_vertices"
#define GPU_COMPUTE_DISPATCH_X             1u
#define GPU_COMPUTE_VERTEX_CAPACITY        32u
#define GPU_COMPUTE_REQUIRED_FEATURE       GPU_FEATURE_SUBGROUPS
#define GPU_COMPUTE_FALLBACK_ARTIFACT_PATH "/subgroup_fallback.us"
#define GPU_COMPUTE_FALLBACK_ENTRY_POINT   "fill_subgroup_vertices_emulated"
#define GPU_COMPUTE_FALLBACK_READY_STATUS  \
  "GPU: subgroup shuffle emulated with workgroup memory"
#define GPU_COMPUTE_UNSUPPORTED_STATUS     \
  "GPU: subgroups unsupported by this adapter"
#define GPU_COMPUTE_READY_STATUS           "GPU: USL subgroup shuffle ready"

#include "../compute/main.c"
