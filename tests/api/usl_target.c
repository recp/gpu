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

#include "../../src/api/usl_target.h"

#include <stdio.h>

#define FEATURE_BIT(feature) (UINT64_C(1) << (feature))

static int
expect_profile(const char       *name,
               uint64_t          featureMask,
               bool              untypedPointers,
               USLTargetProfile  expected) {
  USLTargetProfile actual;

  actual = gpu_uslVulkanProfile(featureMask, untypedPointers);
  if (actual == expected) {
    return 1;
  }

  fprintf(stderr,
          "%s: expected profile %d, got %d\n",
          name,
          (int)expected,
          (int)actual);
  return 0;
}

int
main(void) {
  USLTargetSpec target;
  int           ok;

  ok = gpu_uslDefaultTarget(GPU_BACKEND_VULKAN, &target);
  ok &= target.profile == USL_TARGET_PROFILE_VULKAN_1_0;
  ok &= expect_profile("baseline",
                       0u,
                       false,
                       USL_TARGET_PROFILE_VULKAN_1_0);
  ok &= expect_profile("subgroups",
                       FEATURE_BIT(GPU_FEATURE_SUBGROUPS),
                       false,
                       USL_TARGET_PROFILE_VULKAN_1_1);
  ok &= expect_profile("shader f16",
                       FEATURE_BIT(GPU_FEATURE_SHADER_F16),
                       false,
                       USL_TARGET_PROFILE_VULKAN_1_2);
  ok &= expect_profile("atomic64",
                       FEATURE_BIT(GPU_FEATURE_ATOMIC64),
                       false,
                       USL_TARGET_PROFILE_VULKAN_1_2);
  ok &= expect_profile("ray query",
                       FEATURE_BIT(GPU_FEATURE_RAY_QUERY),
                       false,
                       USL_TARGET_PROFILE_VULKAN_1_2);
  ok &= expect_profile("ray pipeline",
                       FEATURE_BIT(GPU_FEATURE_RAY_TRACING_PIPELINE),
                       false,
                       USL_TARGET_PROFILE_VULKAN_1_2);
  ok &= expect_profile("execution graph",
                       FEATURE_BIT(GPU_FEATURE_EXECUTION_GRAPH) |
                         FEATURE_BIT(GPU_FEATURE_SUBGROUPS),
                       false,
                       USL_TARGET_PROFILE_VULKAN_1_3);
  ok &= expect_profile("untyped pointers",
                       FEATURE_BIT(GPU_FEATURE_EXECUTION_GRAPH) |
                         FEATURE_BIT(GPU_FEATURE_SUBGROUPS),
                       true,
                       USL_TARGET_PROFILE_VULKAN_1_4);

  return ok ? 0 : 1;
}
