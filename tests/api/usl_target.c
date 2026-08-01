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

static int
expect_profile(const char       *name,
               uint32_t          major,
               uint32_t          minor,
               USLTargetProfile  expected) {
  USLTargetProfile actual;

  actual = gpu_uslVulkanProfile(major, minor);
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
  ok &= expect_profile("invalid",
                       0u, 0u,
                       USL_TARGET_PROFILE_NONE);
  ok &= expect_profile("Vulkan 1.0",
                       1u, 0u,
                       USL_TARGET_PROFILE_VULKAN_1_0);
  ok &= expect_profile("Vulkan 1.1",
                       1u, 1u,
                       USL_TARGET_PROFILE_VULKAN_1_1);
  ok &= expect_profile("Vulkan 1.2",
                       1u, 2u,
                       USL_TARGET_PROFILE_VULKAN_1_2);
  ok &= expect_profile("Vulkan 1.3",
                       1u, 3u,
                       USL_TARGET_PROFILE_VULKAN_1_3);
  ok &= expect_profile("Vulkan 1.4",
                       1u, 4u,
                       USL_TARGET_PROFILE_VULKAN_1_4);
  ok &= expect_profile("future Vulkan",
                       2u, 0u,
                       USL_TARGET_PROFILE_VULKAN_1_4);
  ok &= gpu_uslDefaultTarget(GPU_BACKEND_CUDA, &target);
  ok &= target.backend == USL_BACKEND_PTX;
  ok &= target.profile == USL_TARGET_PROFILE_NONE;

  return ok ? 0 : 1;
}
