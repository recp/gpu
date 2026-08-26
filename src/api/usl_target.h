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
static inline uint32_t
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

static inline uint32_t
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

static inline USLTargetPlatform
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

static inline USLTargetProfile
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

static inline int
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

static inline int
gpu_uslDefaultVulkanTarget(USLTargetSpec *outTarget) {
  return outTarget &&
         us_target_init(outTarget,
                        USL_BACKEND_SPIRV,
                        USL_TARGET_PROFILE_VULKAN_1_0) == USLOk;
}

static inline USLTargetProfile
gpu_uslVulkanProfile(uint32_t major, uint32_t minor) {
  if (major == 0u) {
    return USL_TARGET_PROFILE_NONE;
  }
  if (major > 1u || minor >= 4u) {
    return USL_TARGET_PROFILE_VULKAN_1_4;
  }
  if (minor >= 3u) {
    return USL_TARGET_PROFILE_VULKAN_1_3;
  }
  if (minor >= 2u) {
    return USL_TARGET_PROFILE_VULKAN_1_2;
  }
  if (minor >= 1u) {
    return USL_TARGET_PROFILE_VULKAN_1_1;
  }
  return USL_TARGET_PROFILE_VULKAN_1_0;
}

static inline int
gpu_uslDX12NativeEnabled(void) {
  const char *value = getenv("GPU_DX12_NATIVE_DXIL");

  return value && value[0] && strcmp(value, "0") != 0;
}

static inline int
gpu_uslDefaultDX12Target(USLTargetSpec *outTarget) {
  /* Keep HLSL + DXC as production default until native library parity closes.
   * The opt-in path exists for focused native-DXIL correctness gates. */
  return outTarget &&
         us_target_init(outTarget,
                        gpu_uslDX12NativeEnabled()
                          ? USL_BACKEND_DXIL
                          : USL_BACKEND_HLSL,
                        USL_TARGET_PROFILE_HLSL_SM_6_0) == USLOk;
}

static inline int
gpu_uslDefaultWebGPUTarget(USLTargetSpec *outTarget) {
  return outTarget &&
         us_target_init(outTarget,
                        USL_BACKEND_WGSL,
                        USL_TARGET_PROFILE_NONE) == USLOk;
}

static inline int
gpu_uslDefaultCUDATarget(USLTargetSpec *outTarget) {
  return outTarget &&
         us_target_init(outTarget,
                        USL_BACKEND_PTX,
                        USL_TARGET_PROFILE_NONE) == USLOk;
}

static inline int
gpu_uslCUDAExactSM(uint32_t architecture) {
  switch (architecture) {
    case 90u:
    case 100u:
    case 101u:
    case 103u:
    case 110u:
    case 120u:
    case 121u:
      return 1;
    default:
      return 0;
  }
}

static inline uint32_t
gpu_uslCUDATargetSM(uint32_t architecture) {
  switch (architecture) {
    case 50u:
    case 52u:
    case 53u:
    case 60u:
    case 61u:
    case 62u:
    case 70u:
    case 72u:
    case 75u:
    case 80u:
    case 86u:
    case 87u:
    case 89u:
    case 90u:
    case 100u:
    case 101u:
    case 103u:
    case 110u:
    case 120u:
    case 121u:
      return architecture;
    default:
      return architecture > 121u ? 121u : 0u;
  }
}

static inline int
gpu_uslCUDASMAtom(USLCapabilityAtomDesc *outAtom,
                  uint32_t                architecture) {
  char architectureText[USL_CAPABILITY_ATOM_TEXT_MAX];
  uint32_t targetArchitecture;
  int  architectureLength;

  targetArchitecture = gpu_uslCUDATargetSM(architecture);
  if (!outAtom || targetArchitecture == 0u) {
    return 0;
  }

  architectureLength = snprintf(architectureText,
                                sizeof(architectureText),
                                targetArchitecture == architecture &&
                                  gpu_uslCUDAExactSM(architecture)
                                  ? "sm_%ua"
                                  : "sm_%u",
                                targetArchitecture);
  return architectureLength > 0 &&
         (size_t)architectureLength < sizeof(architectureText) &&
         us_cap_atom_text(outAtom, architectureText) == USLOk;
}

static inline uint32_t
gpu_uslCUDAPTXVersion(int driverVersion) {
  if (driverVersion >= 13030) return 903u;
  if (driverVersion >= 13020) return 902u;
  if (driverVersion >= 13010) return 901u;
  if (driverVersion >= 13000) return 900u;
  if (driverVersion >= 12090) return 808u;
  if (driverVersion >= 12080) return 807u;
  if (driverVersion >= 12070) return 806u;
  if (driverVersion >= 12050) return 805u;
  if (driverVersion >= 12040) return 804u;
  if (driverVersion >= 12030) return 803u;
  if (driverVersion >= 12020) return 802u;
  if (driverVersion >= 12010) return 801u;
  if (driverVersion >= 12000) return 800u;
  if (driverVersion >= 11080) return 708u;
  if (driverVersion >= 11070) return 707u;
  if (driverVersion >= 11060) return 706u;
  if (driverVersion >= 11050) return 705u;
  if (driverVersion >= 11040) return 704u;
  if (driverVersion >= 11030) return 703u;
  if (driverVersion >= 11020) return 702u;
  if (driverVersion >= 11010) return 701u;
  return driverVersion >= 11000 ? 700u : 0u;
}

static inline int
gpu_uslCUDAPTXAtom(USLCapabilityAtomDesc *outAtom, uint32_t version) {
  char versionText[USL_CAPABILITY_ATOM_TEXT_MAX];
  int  versionLength;

  if (!outAtom || version < 100u) {
    return 0;
  }
  versionLength = snprintf(versionText,
                           sizeof(versionText),
                           "ptx_%u_%u",
                           version / 100u,
                           version % 100u);
  return versionLength > 0 && (size_t)versionLength < sizeof(versionText) &&
         us_cap_atom_text(outAtom, versionText) == USLOk;
}

static inline int
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
    case GPU_BACKEND_CUDA:
      return gpu_uslDefaultCUDATarget(outTarget);
    default:
      return 0;
  }
}

#endif /* gpu_api_usl_target_h */
