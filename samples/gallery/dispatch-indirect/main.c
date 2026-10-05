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

#define GPU_COMPUTE_ARTIFACT_PATH "/dispatch_indirect.us"
#define GPU_COMPUTE_ENTRY_POINT "fill_indirect_vertices"
#define GPU_COMPUTE_USE_INDIRECT 1
#define GPU_COMPUTE_READY_STATUS "GPU: USL indirect dispatch ready"

#include "../compute/main.c"
