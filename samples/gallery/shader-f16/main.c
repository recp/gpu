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

#define GPU_COMPUTE_ARTIFACT_PATH          "/shader_f16.us"
#define GPU_COMPUTE_ENTRY_POINT            "fill_f16_vertices"
#define GPU_COMPUTE_REQUIRED_FEATURE       GPU_FEATURE_SHADER_F16
#define GPU_COMPUTE_FALLBACK_ARTIFACT_PATH "/shader_f16_fallback.us"
#define GPU_COMPUTE_FALLBACK_ENTRY_POINT   "fill_f16_vertices_emulated"
#define GPU_COMPUTE_FALLBACK_READY_STATUS  \
  "GPU: shader-f16 emulated with f32 arithmetic"
#define GPU_COMPUTE_UNSUPPORTED_STATUS     \
  "GPU: shader-f16 unsupported by this adapter"
#define GPU_COMPUTE_READY_STATUS           "GPU: USL shader-f16 ready"

#include "../compute/main.c"
