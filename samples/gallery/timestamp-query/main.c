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

#define GPU_COMPUTE_ARTIFACT_PATH             "/timestamp_query.us"
#define GPU_COMPUTE_ENTRY_POINT               "fill_timestamp_vertices"
#define GPU_COMPUTE_USE_TIMESTAMPS            1
#define GPU_COMPUTE_REQUIRED_FEATURE          GPU_FEATURE_TIMESTAMPS
#define GPU_COMPUTE_FALLBACK_ARTIFACT_PATH    "/timestamp_query.us"
#define GPU_COMPUTE_FALLBACK_ENTRY_POINT      "fill_timestamp_vertices_fallback"
#define GPU_COMPUTE_FALLBACK_READY_STATUS     \
  "GPU: native timestamps unavailable; rendering fallback"
#define GPU_COMPUTE_UNSUPPORTED_STATUS        \
  "GPU: timestamps unsupported by this adapter"
#define GPU_COMPUTE_READY_STATUS              "GPU: USL pass timestamps ready"
#define GPU_COMPUTE_TIMESTAMP_RESOLVED_STATUS \
  "GPU: USL compute and render timestamps resolved"

#include "../compute/main.c"
