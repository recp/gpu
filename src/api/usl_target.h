/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef gpu_api_usl_target_h
#define gpu_api_usl_target_h

#include "../common.h"

#include <us/compiler.h>

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#  include <sys/sysctl.h>
#endif

#if defined(__APPLE__)
static uint32_t
gpu_uslParseLeadingU32(const char *text) {
  uint32_t value = 0;

  if (!text || text[0] < '0' || text[0] > '9') {
    return 0;
  }

  while (*text >= '0' && *text <= '9') {
    value = value * 10u + (uint32_t)(*text - '0');
    text++;
  }

  return value;
}

static uint32_t
gpu_uslAppleRuntimePlatformMajor(void) {
  char version[64];
  size_t versionSize = sizeof(version);

  memset(version, 0, sizeof(version));
  if (sysctlbyname("kern.osproductversion",
                   version,
                   &versionSize,
                   NULL,
                   0) == 0 &&
      versionSize > 0) {
    return gpu_uslParseLeadingU32(version);
  }

#  if TARGET_OS_TV && defined(__TV_OS_VERSION_MAX_ALLOWED)
  return (uint32_t)(__TV_OS_VERSION_MAX_ALLOWED / 10000);
#  elif defined(TARGET_OS_VISION) && TARGET_OS_VISION && defined(__VISION_OS_VERSION_MAX_ALLOWED)
  return (uint32_t)(__VISION_OS_VERSION_MAX_ALLOWED / 10000);
#  elif TARGET_OS_IPHONE && defined(__IPHONE_OS_VERSION_MAX_ALLOWED)
  return (uint32_t)(__IPHONE_OS_VERSION_MAX_ALLOWED / 10000);
#  elif defined(__MAC_OS_X_VERSION_MAX_ALLOWED)
  return (uint32_t)(__MAC_OS_X_VERSION_MAX_ALLOWED / 10000);
#  else
  return 0;
#  endif
}

static USLTargetPlatform
gpu_uslAppleTargetPlatform(void) {
#  if TARGET_OS_TV
  return USL_TARGET_PLATFORM_TVOS;
#  elif defined(TARGET_OS_VISION) && TARGET_OS_VISION
  return USL_TARGET_PLATFORM_VISIONOS;
#  elif TARGET_OS_IPHONE
  return USL_TARGET_PLATFORM_IOS;
#  elif TARGET_OS_MAC
  return USL_TARGET_PLATFORM_MACOS;
#  else
  return USL_TARGET_PLATFORM_NONE;
#  endif
}

static USLTargetProfile
gpu_uslAppleMetalProfile(USLTargetPlatform platform, uint32_t platformMajor) {
  if (platform == USL_TARGET_PLATFORM_MACOS) {
    if (platformMajor >= 26u) return USL_TARGET_PROFILE_MSL_4_0;
    if (platformMajor >= 15u) return USL_TARGET_PROFILE_MSL_3_2;
    if (platformMajor >= 14u) return USL_TARGET_PROFILE_MSL_3_1;
    if (platformMajor >= 13u) return USL_TARGET_PROFILE_MSL_3_0;
    if (platformMajor >= 12u) return USL_TARGET_PROFILE_MSL_2_4;
    if (platformMajor >= 11u) return USL_TARGET_PROFILE_MSL_2_3;
  } else if (platform == USL_TARGET_PLATFORM_IOS) {
    if (platformMajor >= 26u) return USL_TARGET_PROFILE_MSL_4_0;
    if (platformMajor >= 18u) return USL_TARGET_PROFILE_MSL_3_2;
    if (platformMajor >= 17u) return USL_TARGET_PROFILE_MSL_3_1;
    if (platformMajor >= 16u) return USL_TARGET_PROFILE_MSL_3_0;
    if (platformMajor >= 15u) return USL_TARGET_PROFILE_MSL_2_4;
    if (platformMajor >= 14u) return USL_TARGET_PROFILE_MSL_2_3;
    if (platformMajor >= 13u) return USL_TARGET_PROFILE_MSL_2_2;
    if (platformMajor >= 12u) return USL_TARGET_PROFILE_MSL_2_1;
  }

  return USL_TARGET_PROFILE_MSL_2_0;
}
#endif

static int
gpu_uslDefaultMetalTarget(USLTargetSpec *outTarget) {
  if (!outTarget) {
    return 0;
  }

#if defined(__APPLE__)
  {
    USLTargetPlatform platform = gpu_uslAppleTargetPlatform();
    uint32_t          platformMajor = gpu_uslAppleRuntimePlatformMajor();

    if (platform != USL_TARGET_PLATFORM_NONE && platformMajor > 0) {
      return us_target_platform(outTarget,
                                USL_BACKEND_METAL,
                                gpu_uslAppleMetalProfile(platform,
                                                        platformMajor),
                                platform,
                                platformMajor) == USLOk;
    }
  }
#endif

  return us_target_init(outTarget,
                        USL_BACKEND_METAL,
                        USL_TARGET_PROFILE_MSL_2_0) == USLOk;
}

static int
gpu_uslDefaultVulkanTarget(USLTargetSpec *outTarget) {
  return outTarget &&
         us_target_init(outTarget,
                        USL_BACKEND_SPIRV,
                        USL_TARGET_PROFILE_VULKAN_1_0) == USLOk;
}

static int
gpu_uslDefaultDX12Target(USLTargetSpec *outTarget) {
  return outTarget &&
         us_target_init(outTarget,
                        USL_BACKEND_HLSL,
                        USL_TARGET_PROFILE_HLSL_SM_6_0) == USLOk;
}

static int
gpu_uslDefaultWebGPUTarget(USLTargetSpec *outTarget) {
  return outTarget &&
         us_target_init(outTarget,
                        USL_BACKEND_WGSL,
                        USL_TARGET_PROFILE_NONE) == USLOk;
}

static int
gpu_uslDefaultTarget(GPUBackend backend, USLTargetSpec *outTarget) {
  switch (backend) {
    case GPU_BACKEND_METAL:
      return gpu_uslDefaultMetalTarget(outTarget);
    case GPU_BACKEND_VULKAN:
      return gpu_uslDefaultVulkanTarget(outTarget);
    case GPU_BACKEND_DX12:
      return gpu_uslDefaultDX12Target(outTarget);
    case GPU_BACKEND_WEBGPU:
      return gpu_uslDefaultWebGPUTarget(outTarget);
    default:
      return 0;
  }
}

#endif /* gpu_api_usl_target_h */
