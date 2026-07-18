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

#include "../common.h"
#include "pipeline_cache.h"
#include "texture_view_pool.h"

static GPUAdapterMT *
mt_adapter(const GPUAdapter *adapter) {
  return adapter ? adapter->_priv : NULL;
}

static id<MTLDevice>
mt_adapterDevice(const GPUAdapter *adapter) {
  GPUAdapterMT *adapterMT;

  adapterMT = mt_adapter(adapter);
  return adapterMT ? adapterMT->device : nil;
}

static void
mt_initFormatSupport(GPUAdapterMT *adapterMT) {
  id<MTLDevice> device;

  if (!adapterMT || !(device = adapterMT->device)) {
    return;
  }

  adapterMT->storageTier = MTLReadWriteTextureTierNone;
#if TARGET_OS_IOS
  adapterMT->depth24Supported = false;
#else
  adapterMT->depth24Supported = device.depth24Stencil8PixelFormatSupported;
#endif
  if (@available(macOS 10.13, iOS 11.0, *)) {
    adapterMT->storageTier = device.readWriteTextureSupport;
  }
  if (@available(macOS 11.0, iOS 14.0, *)) {
    adapterMT->float32Filterable = device.supports32BitFloatFiltering;
  }
  if (@available(macOS 11.0, iOS 16.4, *)) {
    adapterMT->bcSupported = device.supportsBCTextureCompression;
  }
  if (@available(macOS 10.15, iOS 13.0, *)) {
    adapterMT->appleFamily1 = [device supportsFamily:MTLGPUFamilyApple1];
    adapterMT->appleFamily2 = [device supportsFamily:MTLGPUFamilyApple2];
  }
}

GPU_HIDE
GPUAdapter *
mt_getAvailableAdapters(GPUInstance * __restrict inst,
                        uint32_t                 maxNumberOfItems) {
  NSArray<id<MTLDevice>> *devices;
  GPUAdapterMT           *adapterMT;
  GPUAdapter             *firstAdapter, *lastAdapter, *adapter;
#if TARGET_OS_IOS
  id<MTLDevice>           defaultDevice;
#endif
  uint32_t                i;

  i            = 0;
  firstAdapter = lastAdapter = NULL;
#if TARGET_OS_IOS
  defaultDevice = MTLCreateSystemDefaultDevice();
  devices       = defaultDevice
                    ? [[NSArray alloc] initWithObjects:defaultDevice, nil]
                    : nil;
  [defaultDevice release];
#else
  devices = MTLCopyAllDevices();
#endif

  for (id<MTLDevice> device in devices) {
    adapter   = calloc(1, sizeof(*adapter));
    adapterMT = calloc(1, sizeof(*adapterMT));
    if (!adapter || !adapterMT) {
      free(adapterMT);
      free(adapter);
      break;
    }
    adapterMT->device       = [device retain];
    adapterMT->subgroupLock = OS_UNFAIR_LOCK_INIT;
    mt_initFormatSupport(adapterMT);
    adapter->separatePresentQueue       = 1;
    adapter->supportsDisplayTiming      = 1;
    adapter->supportsIncrementalPresent = 1; /* TODO: */
    adapter->supportsSwapchain          = 1;
    adapter->inst                       = inst;
    adapter->_priv                      = adapterMT;

    if (lastAdapter) { lastAdapter->next = adapter; }
    else             { firstAdapter      = adapter; }
    lastAdapter = adapter;

    if (++i >= maxNumberOfItems) { break; }
  }

  [devices release];

  return firstAdapter;
}

GPU_HIDE
GPUAdapter *
mt_selectAdapter(GPUInstance * __restrict inst,
                 GPUAdapter  * __restrict adapters) {
  id<MTLDevice> preferred;
  GPUAdapter   *adapter;

  GPU__UNUSED(inst);
  preferred = MTLCreateSystemDefaultDevice();
  adapter   = adapters;
  while (preferred && adapter) {
    if (mt_adapterDevice(adapter).registryID == preferred.registryID) {
      [preferred release];
      return adapter;
    }
    adapter = adapter->next;
  }
  [preferred release];
  return adapters;
}

GPU_HIDE
void
mt_destroyAdapter(GPUAdapter * __restrict adapter) {
  GPUAdapterMT *adapterMT;

  if (!adapter) {
    return;
  }

  adapterMT = mt_adapter(adapter);
  if (adapterMT) {
    [adapterMT->device release];
    free(adapterMT);
  }
  free(adapter);
}

GPU_HIDE
GPUResult
mt_getAdapterProperties(const GPUAdapter     * __restrict adapter,
                        GPUAdapterProperties * __restrict outProps) {
  id<MTLDevice> device;

  if (!adapter || !outProps || !adapter->_priv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  device = mt_adapterDevice(adapter);
  memset(outProps, 0, sizeof(*outProps));
  outProps->backend = GPU_BACKEND_METAL;
  outProps->name = device.name.UTF8String;
  outProps->type = device.isLowPower ?
    GPU_ADAPTER_TYPE_INTEGRATED :
    GPU_ADAPTER_TYPE_DISCRETE;

  return GPU_OK;
}

static bool
mt_hasCounterSet(id<MTLDevice> device, MTLCommonCounterSet name) {
  if (!device || !name) {
    return false;
  }

  if (@available(macOS 10.15, iOS 14.0, *)) {
    for (id<MTLCounterSet> counterSet in device.counterSets) {
      if ([counterSet.name isEqualToString:name]) {
        return true;
      }
    }
  }

  return false;
}

static bool
mt_supportsBlitCounterSampling(id<MTLDevice> device) {
  if (!device) {
    return false;
  }

  if (@available(macOS 11.0, iOS 14.0, *)) {
    return [device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary];
  }

  return false;
}

static bool
mt_supportsSubgroupFamily(id<MTLDevice> device) {
  if (@available(macOS 10.15, iOS 13.0, *)) {
    return device &&
           ([device supportsFamily:MTLGPUFamilyApple6] ||
            [device supportsFamily:MTLGPUFamilyMac2]);
  }

  return false;
}

static void
mt_probeSubgroups(GPUAdapterMT *adapterMT) {
  static NSString *source =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "kernel void gpu_subgroup_probe(device uint *output [[buffer(0)]],\n"
     "                               uint tid [[thread_position_in_grid]]) {\n"
     "  uint x = simd_shuffle_xor(tid, 1u);\n"
     "  uint y = simd_shuffle_down(tid, 1u);\n"
     "  uint z = simd_shuffle_up(tid, 1u);\n"
     "  output[tid] = x + y + z;\n"
     "}\n";
  id<MTLComputePipelineState> pipeline;
  id<MTLFunction>             function;
  id<MTLLibrary>              library;

  if (!adapterMT) {
    return;
  }

  os_unfair_lock_lock(&adapterMT->subgroupLock);
  if (adapterMT->subgroupProbed) {
    os_unfair_lock_unlock(&adapterMT->subgroupLock);
    return;
  }
  adapterMT->subgroupProbed = true;

  if (!mt_supportsSubgroupFamily(adapterMT->device)) {
    os_unfair_lock_unlock(&adapterMT->subgroupLock);
    return;
  }

  library = [adapterMT->device newLibraryWithSource:source
                                             options:nil
                                               error:nil];
  function = [library newFunctionWithName:@"gpu_subgroup_probe"];
  pipeline = function
    ? [adapterMT->device newComputePipelineStateWithFunction:function
                                                       error:nil]
    : nil;
  if (pipeline && pipeline.threadExecutionWidth > 0u) {
    adapterMT->subgroupSize = (uint32_t)pipeline.threadExecutionWidth;
    adapterMT->subgroups    = true;
  }

  [pipeline release];
  [function release];
  [library release];
  os_unfair_lock_unlock(&adapterMT->subgroupLock);
}

static bool
mt_supportsSubgroupOperations(
  const GPUAdapter                 * __restrict adapter,
  GPUShaderStageFlags                           stage,
  GPUBackendSubgroupOperationFlags              operations) {
  const GPUShaderStageFlags supportedStages =
    GPU_SHADER_STAGE_VERTEX_BIT |
    GPU_SHADER_STAGE_FRAGMENT_BIT |
    GPU_SHADER_STAGE_COMPUTE_BIT |
    GPU_SHADER_STAGE_TASK_BIT |
    GPU_SHADER_STAGE_MESH_BIT;
  const GPUBackendSubgroupOperationFlags supportedOperations =
    GPU_BACKEND_SUBGROUP_OPERATION_BASIC_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_BIT |
    GPU_BACKEND_SUBGROUP_OPERATION_SHUFFLE_RELATIVE_BIT;
  GPUAdapterMT *adapterMT;

  adapterMT = mt_adapter(adapter);
  mt_probeSubgroups(adapterMT);
  return adapterMT && adapterMT->subgroups &&
         (supportedStages & stage) == stage &&
         (supportedOperations & operations) == operations;
}

enum {
  MT_SUBGROUP_MATRIX_F16_F16_F16 = 1u << 0,
  MT_SUBGROUP_MATRIX_F16_F16_F32 = 1u << 1,
  MT_SUBGROUP_MATRIX_F32_F32_F32 = 1u << 2
};

static bool
mt_probeSubgroupMatrixProfile(id<MTLDevice> device,
                              NSString     *name,
                              NSString     *abType,
                              NSString     *cType) {
  NSString                    *source;
  id<MTLComputePipelineState>  pipeline;
  id<MTLFunction>              function;
  id<MTLLibrary>               library;

  source = [NSString stringWithFormat:
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "kernel void %@(device %@ *ab [[buffer(0)]], "
     "device %@ *out [[buffer(1)]]) {\n"
     "  simdgroup_matrix<%@, 8, 8> a;\n"
     "  simdgroup_matrix<%@, 8, 8> b;\n"
     "  simdgroup_matrix<%@, 8, 8> c = "
     "make_filled_simdgroup_matrix<%@, 8, 8>((%@)0);\n"
     "  simdgroup_matrix<%@, 8, 8> r;\n"
     "  simdgroup_load(a, ab, 8, ulong2(0), false);\n"
     "  simdgroup_load(b, ab + 64, 8, ulong2(0), false);\n"
     "  simdgroup_multiply_accumulate(r, a, b, c);\n"
     "  simdgroup_store(r, out, 8, ulong2(0), false);\n"
     "}\n",
     name,
     abType,
     cType,
     abType,
     abType,
     cType,
     cType,
     cType,
     cType];
  library  = [device newLibraryWithSource:source options:nil error:nil];
  function = [library newFunctionWithName:name];
  pipeline = function
    ? [device newComputePipelineStateWithFunction:function error:nil]
    : nil;

  [pipeline release];
  [function release];
  [library release];
  return pipeline != nil;
}

static void
mt_probeSubgroupMatrices(GPUAdapterMT *adapterMT) {
  id<MTLDevice> device;

  if (!adapterMT) {
    return;
  }

  mt_probeSubgroups(adapterMT);
  os_unfair_lock_lock(&adapterMT->subgroupLock);
  if (adapterMT->subgroupMatrixProbed) {
    os_unfair_lock_unlock(&adapterMT->subgroupLock);
    return;
  }
  adapterMT->subgroupMatrixProbed = true;
  device = adapterMT->device;
  if (!adapterMT->subgroups || !device) {
    os_unfair_lock_unlock(&adapterMT->subgroupLock);
    return;
  }

  if (mt_probeSubgroupMatrixProfile(device,
                                    @"gpu_matrix_hh_h",
                                    @"half",
                                    @"half")) {
    adapterMT->subgroupMatrixProfiles |= MT_SUBGROUP_MATRIX_F16_F16_F16;
  }
  if (mt_probeSubgroupMatrixProfile(device,
                                    @"gpu_matrix_hh_f",
                                    @"half",
                                    @"float")) {
    adapterMT->subgroupMatrixProfiles |= MT_SUBGROUP_MATRIX_F16_F16_F32;
  }
  if (mt_probeSubgroupMatrixProfile(device,
                                    @"gpu_matrix_ff_f",
                                    @"float",
                                    @"float")) {
    adapterMT->subgroupMatrixProfiles |= MT_SUBGROUP_MATRIX_F32_F32_F32;
  }
  os_unfair_lock_unlock(&adapterMT->subgroupLock);
}

static GPUResult
mt_getSubgroupMatrixProperties(
  const GPUAdapter               * __restrict adapter,
  uint32_t                       * __restrict inoutPropertyCount,
  GPUSubgroupMatrixPropertiesEXT * __restrict outProperties) {
  static const struct {
    uint32_t                          bit;
    GPUSubgroupMatrixComponentTypeEXT abType;
    GPUSubgroupMatrixComponentTypeEXT cType;
  } profiles[] = {
    {MT_SUBGROUP_MATRIX_F16_F16_F16,
     GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT,
     GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT},
    {MT_SUBGROUP_MATRIX_F16_F16_F32,
     GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT,
     GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT},
    {MT_SUBGROUP_MATRIX_F32_F32_F32,
     GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT,
     GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT}
  };
  GPUAdapterMT *adapterMT;
  uint32_t      capacity;
  uint32_t      count;
  uint32_t      written;

  if (!adapter || !inoutPropertyCount ||
      !(adapterMT = mt_adapter(adapter))) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  mt_probeSubgroupMatrices(adapterMT);
  capacity = *inoutPropertyCount;
  count    = 0u;
  written  = 0u;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(profiles); i++) {
    GPUSubgroupMatrixPropertiesEXT property;

    if ((adapterMT->subgroupMatrixProfiles & profiles[i].bit) == 0u) {
      continue;
    }
    memset(&property, 0, sizeof(property));
    property.m          = 8u;
    property.n          = 8u;
    property.k          = 8u;
    property.aType      = profiles[i].abType;
    property.bType      = profiles[i].abType;
    property.cType      = profiles[i].cType;
    property.resultType = profiles[i].cType;
    property.stages     = GPU_SHADER_STAGE_COMPUTE_BIT;
    property.scope      = GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT;
    if (outProperties && written < capacity) {
      outProperties[written++] = property;
    }
    count++;
  }

  *inoutPropertyCount = count;
  if (count == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }
  return outProperties && capacity < count
           ? GPU_ERROR_INSUFFICIENT_CAPACITY
           : GPU_OK;
}

GPU_HIDE
bool
mt_supportsFeature(const GPUAdapter * __restrict adapter, GPUFeature feature) {
  GPUAdapterMT *adapterMT;
  id<MTLDevice> device;
  const char   *mode;

  if (!(adapterMT = mt_adapter(adapter))) {
    return false;
  }

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
    case GPU_FEATURE_INDIRECT_DRAW:
    case GPU_FEATURE_SHADER_F16:
    case GPU_FEATURE_DESCRIPTOR_INDEXING:
    case GPU_FEATURE_BINDLESS:
      return true;
    case GPU_FEATURE_BUFFER_DEVICE_ADDRESS:
      if (@available(macOS 13.0, iOS 16.0, *)) {
        return true;
      }
      return false;
    case GPU_FEATURE_PLACED_RESOURCES:
      if (@available(macOS 10.15, iOS 13.0, *)) {
        return true;
      }
      return false;
    case GPU_FEATURE_SPARSE_TEXTURES:
#if TARGET_OS_OSX
      device = adapterMT->device;
      if (@available(macOS 11.0, *)) {
        return [device supportsFamily:MTLGPUFamilyApple6] ||
               [device supportsFamily:MTLGPUFamilyMac2];
      }
#endif
#if MT_HAS_METAL4
      device = adapterMT->device;
      if (@available(macOS 26.0, iOS 26.0, *)) {
        return [device respondsToSelector:@selector(newMTL4CommandQueue)];
      }
#endif
      return false;
    case GPU_FEATURE_SPARSE_BUFFERS:
    case GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT:
#if MT_HAS_METAL4
      mode = getenv("GPU_METAL_MODE");
      if (mode && strcmp(mode, "classic") == 0) {
        return false;
      }
      device = adapterMT->device;
      if (@available(macOS 26.0, iOS 26.0, *)) {
        return [device respondsToSelector:@selector(newMTL4CommandQueue)] &&
               [device respondsToSelector:@selector(newCommandAllocator)];
      }
#endif
      return false;
    case GPU_FEATURE_MESH_SHADER:
      device = adapterMT->device;
      if (@available(macOS 13.0, iOS 16.0, *)) {
        return [device supportsFamily:MTLGPUFamilyApple7] ||
               [device supportsFamily:MTLGPUFamilyMac2];
      }
      return false;
    case GPU_FEATURE_VARIABLE_RATE_SHADING:
      device = adapterMT->device;
      if (@available(macOS 10.15.4, iOS 13.0, *)) {
        return [device supportsRasterizationRateMapWithLayerCount:1u];
      }
      return false;
    case GPU_FEATURE_RAY_QUERY:
      device = adapterMT->device;
      mode   = getenv("GPU_METAL_MODE");
      if (mode && strcmp(mode, "metal4") == 0) {
        if (@available(macOS 14.0, iOS 17.0, *)) {
          return [device supportsFamily:MTLGPUFamilyApple9];
        }
        return false;
      }
      if (@available(macOS 12.0, iOS 15.0, *)) {
        return device.supportsRaytracing &&
               device.supportsRaytracingFromRender;
      }
      return false;
    case GPU_FEATURE_SUBGROUPS:
      mt_probeSubgroups(adapterMT);
      return adapterMT->subgroups;
    case GPU_FEATURE_SUBGROUP_MATRIX:
      mt_probeSubgroupMatrices(adapterMT);
      return adapterMT->subgroupMatrixProfiles != 0u;
    case GPU_FEATURE_TIMESTAMPS:
      device = adapterMT->device;
      return mt_hasCounterSet(device, MTLCommonCounterSetTimestamp) &&
             mt_supportsBlitCounterSampling(device);
    default:
      return false;
  }
}

static void
mt_getLimits(const GPUAdapter * __restrict adapter,
             GPULimits       * __restrict outLimits) {
  GPUAdapterMT *adapterMT;
  id<MTLDevice> device;
  MTLSize       threads;

  adapterMT = mt_adapter(adapter);
  device    = adapterMT ? adapterMT->device : nil;
  if (!adapterMT || !device || !outLimits) {
    return;
  }

  mt_probeSubgroups(adapterMT);
  threads = device.maxThreadsPerThreadgroup;
  outLimits->maxComputeWorkgroupSizeX = (uint32_t)threads.width;
  outLimits->maxComputeWorkgroupSizeY = (uint32_t)threads.height;
  outLimits->maxComputeWorkgroupSizeZ = (uint32_t)threads.depth;
  outLimits->minSubgroupSize           = adapterMT->subgroupSize;
  outLimits->maxSubgroupSize           = adapterMT->subgroupSize;
}

static bool
mt_isFloat32Format(GPUFormat format) {
  return format == GPU_FORMAT_R32_FLOAT ||
         format == GPU_FORMAT_RG32_FLOAT ||
         format == GPU_FORMAT_RGBA32_FLOAT;
}

static bool
mt_isTier1StorageFormat(GPUFormat format) {
  return format == GPU_FORMAT_R32_UINT ||
         format == GPU_FORMAT_R32_SINT ||
         format == GPU_FORMAT_R32_FLOAT;
}

static bool
mt_isTier2StorageFormat(GPUFormat format) {
  switch (format) {
    case GPU_FORMAT_R8_UNORM:
    case GPU_FORMAT_R8_UINT:
    case GPU_FORMAT_R8_SINT:
    case GPU_FORMAT_R16_UINT:
    case GPU_FORMAT_R16_SINT:
    case GPU_FORMAT_R16_FLOAT:
    case GPU_FORMAT_RGBA8_UNORM:
    case GPU_FORMAT_RGBA8_UINT:
    case GPU_FORMAT_RGBA8_SINT:
    case GPU_FORMAT_RGBA16_UINT:
    case GPU_FORMAT_RGBA16_SINT:
    case GPU_FORMAT_RGBA16_FLOAT:
    case GPU_FORMAT_RGBA32_UINT:
    case GPU_FORMAT_RGBA32_SINT:
    case GPU_FORMAT_RGBA32_FLOAT:
      return true;
    default:
      return mt_isTier1StorageFormat(format);
  }
}

static bool
mt_isBCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_BC1_RGBA_UNORM &&
         format <= GPU_FORMAT_BC7_RGBA_UNORM_SRGB;
}

static bool
mt_isETCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_EAC_R11_UNORM &&
         format <= GPU_FORMAT_ETC2_RGB8A1_UNORM_SRGB;
}

static bool
mt_isASTCFormat(GPUFormat format) {
  return format >= GPU_FORMAT_ASTC_4X4_UNORM &&
         format <= GPU_FORMAT_ASTC_12X12_UNORM_SRGB;
}

static void
mt_getFormatCapabilities(
  const GPUAdapter      * __restrict adapter,
  GPUFormat              format,
  GPUFormatCapabilities * __restrict outCaps) {
  GPUAdapterMT *adapterMT;

  adapterMT = mt_adapter(adapter);
  if (!adapterMT || !outCaps) {
    return;
  }

  if (mt_isBCFormat(format)) {
    memset(outCaps, 0, sizeof(*outCaps));
    outCaps->sampled    = adapterMT->bcSupported;
    outCaps->filterable = adapterMT->bcSupported;
    return;
  }
  if (mt_isETCFormat(format)) {
    memset(outCaps, 0, sizeof(*outCaps));
    outCaps->sampled    = adapterMT->appleFamily1;
    outCaps->filterable = adapterMT->appleFamily1;
    return;
  }
  if (mt_isASTCFormat(format)) {
    memset(outCaps, 0, sizeof(*outCaps));
    outCaps->sampled    = adapterMT->appleFamily2;
    outCaps->filterable = adapterMT->appleFamily2;
    return;
  }

  if (outCaps->depthStencil) {
    if (format == GPU_FORMAT_DEPTH24_UNORM_STENCIL8 &&
        !adapterMT->depth24Supported) {
      memset(outCaps, 0, sizeof(*outCaps));
      return;
    }
    outCaps->sampled    = true;
    outCaps->filterable = false;
    return;
  }

  outCaps->storage =
    (adapterMT->storageTier >= MTLReadWriteTextureTier1 &&
     mt_isTier1StorageFormat(format)) ||
    (adapterMT->storageTier >= MTLReadWriteTextureTier2 &&
     mt_isTier2StorageFormat(format));
  if (mt_isFloat32Format(format)) {
    outCaps->filterable = adapterMT->float32Filterable;
    outCaps->blendable  = false;
  }
}

extern
GPU_HIDE
GPUQueue*
mt_newCommandQueue(GPUDevice * __restrict device);

GPU_HIDE
void
mt_destroyCommandQueue(GPUQueue * __restrict queue);

static bool
mt_supportsMetal4(id<MTLDevice> device) {
#if MT_HAS_METAL4
  if (@available(macOS 26.0, iOS 26.0, *)) {
    return device &&
           [device respondsToSelector:@selector(newMTL4CommandQueue)] &&
           [device respondsToSelector:@selector(newCommandAllocator)] &&
           [device respondsToSelector:@selector(newArgumentTableWithDescriptor:error:)] &&
           [device respondsToSelector:@selector(newCompilerWithDescriptor:error:)];
  }

  return false;
#else
  GPU__UNUSED(device);
  return false;
#endif
}

static bool
mt_supportsMetal4RayQuery(id<MTLDevice> device) {
  if (@available(macOS 14.0, iOS 17.0, *)) {
    return device && [device supportsFamily:MTLGPUFamilyApple9];
  }

  return false;
}

static bool
mt_metal4AutoSafe(void) {
#if TARGET_OS_IOS
  NSOperatingSystemVersion version;

  /* Metal 4 screen capture crashes the compositor on iOS 26.5.2. */
  version = NSProcessInfo.processInfo.operatingSystemVersion;
  return version.majorVersion != 26 ||
         version.minorVersion != 5 ||
         version.patchVersion != 2;
#else
  return true;
#endif
}

static bool
mt_selectCommandMode(id<MTLDevice>  device,
                     uint64_t       enabledFeatureMask,
                     MTCommandMode *outMode) {
  const char *mode;
  bool        explicitSparse;
  bool        rayQuery;
  bool        supportsMetal4;

  if (!outMode) {
    return false;
  }

  mode           = getenv("GPU_METAL_MODE");
  explicitSparse = (enabledFeatureMask &
                    (UINT64_C(1) <<
                     GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT)) != 0u;
  rayQuery       = (enabledFeatureMask &
                    (UINT64_C(1) << GPU_FEATURE_RAY_QUERY)) != 0u;
  supportsMetal4 = mt_supportsMetal4(device);
  if (mode && strcmp(mode, "classic") == 0) {
    if (explicitSparse) {
      return false;
    }
    *outMode = MTCommandModeClassic;
    return true;
  }
  if (mode && strcmp(mode, "metal4") == 0) {
    if (!supportsMetal4) {
      NSLog(@"GPU_METAL_MODE=metal4 requested on an unsupported device or OS");
      return false;
    }
    if (rayQuery && !mt_supportsMetal4RayQuery(device)) {
      NSLog(@"GPU_METAL_MODE=metal4 does not support software ray tracing");
      return false;
    }
    *outMode = MTCommandMode4;
    return true;
  }
  if (mode && strcmp(mode, "auto") != 0) {
    NSLog(@"Unknown GPU_METAL_MODE '%s'; expected auto, classic, or metal4", mode);
    return false;
  }

  *outMode = supportsMetal4 && (explicitSparse || mt_metal4AutoSafe()) &&
             (!rayQuery || mt_supportsMetal4RayQuery(device))
               ? MTCommandMode4
               : MTCommandModeClassic;
  return true;
}

GPU_HIDE
GPUDevice *
mt_createDevice(GPUAdapter              * __restrict adapter,
                const GPUQueueCreateInfo queCI[],
                uint32_t                 nQueCI,
                uint64_t                 enabledFeatureMask) {
  GPUAdapterMT *adapterMT;
  GPUDevice     *device;
  GPUDeviceMT   *deviceMT;
  MTCommandMode  commandMode;
  uint32_t       i, j, queueIndex, queueCount;

  GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI)

  adapterMT = mt_adapter(adapter);
  if (!adapterMT ||
      ((enabledFeatureMask &
        (1ull << GPU_FEATURE_SUBGROUP_MATRIX)) != 0u &&
       !mt_supportsFeature(adapter, GPU_FEATURE_SUBGROUP_MATRIX)) ||
      !mt_selectCommandMode(adapterMT->device,
                            enabledFeatureMask,
                            &commandMode)) {
    return NULL;
  }

  device   = calloc(1, sizeof(*device));
  deviceMT = calloc(1, sizeof(*deviceMT));
  if (!device || !deviceMT) {
    free(deviceMT);
    free(device);
    return NULL;
  }

  deviceMT->device      = adapterMT->device;
  deviceMT->commandMode = commandMode;
  deviceMT->textureViewPoolLock = OS_UNFAIR_LOCK_INIT;
  if (mt_initPipelineCompiler(deviceMT) != GPU_OK) {
    free(deviceMT);
    free(device);
    return NULL;
  }
  queueCount            = 0;
  for (i = 0; i < nQueCI; i++) {
    queueCount += queCI[i].count;
  }
  deviceMT->nCreatedQueues = queueCount;

  if (queueCount) {
    deviceMT->createdQueues = calloc(queueCount, sizeof(void*));
    if (!deviceMT->createdQueues) {
      mt_destroyPipelineCompiler(deviceMT);
      free(deviceMT);
      free(device);
      return NULL;
    }
  }

  device->_priv            = deviceMT;
  device->inst             = adapter->inst;
  device->adapter          = adapter;
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MESH_SHADER)) != 0u) {
    MTLSize workgroupSize;

    workgroupSize = adapterMT->device.maxThreadsPerThreadgroup;
    device->meshLimits.taskWorkgroupSize[0] = (uint32_t)workgroupSize.width;
    device->meshLimits.taskWorkgroupSize[1] = (uint32_t)workgroupSize.height;
    device->meshLimits.taskWorkgroupSize[2] = (uint32_t)workgroupSize.depth;
    device->meshLimits.meshWorkgroupSize[0] = (uint32_t)workgroupSize.width;
    device->meshLimits.meshWorkgroupSize[1] = (uint32_t)workgroupSize.height;
    device->meshLimits.meshWorkgroupSize[2] = (uint32_t)workgroupSize.depth;
    device->meshLimits.maxTaskWorkgroupInvocations = 1024u;
    device->meshLimits.maxMeshWorkgroupInvocations = 1024u;
    device->meshLimits.maxPayloadSizeBytes          = 16u * 1024u;
    device->meshLimits.maxOutputVertices            = 256u;
    device->meshLimits.maxOutputPrimitives          = 512u;
  }

  queueIndex = 0;
  for (i = 0; i < nQueCI; i++) {
    for (j = 0; j < queCI[i].count; j++) {
      deviceMT->createdQueues[queueIndex] = mt_newCommandQueue(device);
      if (!deviceMT->createdQueues[queueIndex]) {
        for (uint32_t k = 0; k < queueIndex; k++) {
          mt_destroyCommandQueue(deviceMT->createdQueues[k]);
        }
        free(deviceMT->createdQueues);
        mt_destroyPipelineCompiler(deviceMT);
        free(deviceMT);
        free(device);
        return NULL;
      }
      deviceMT->createdQueues[queueIndex]->bits = queCI[i].flags;
      device->queueFamilies |= queCI[i].flags;
      queueIndex++;
    }
  }

  return device;
}

static GPUResult
mt_waitDeviceIdle(GPUDevice * __restrict device) {
  GPUDeviceMT *deviceMT;

  if (!device || !(deviceMT = device->_priv)) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  for (uint32_t i = 0u; i < deviceMT->nCreatedQueues; i++) {
    GPUQueue        *commandQueue;
    MTCommandQueue *queue;

    commandQueue = deviceMT->createdQueues[i];
    queue        = mt_commandQueue(commandQueue);
    if (queue && mt_flushTransfers(commandQueue, true) != GPU_OK) {
      return GPU_ERROR_BACKEND_FAILURE;
    }
    if (queue && queue->inFlightGroup) {
      dispatch_group_wait(queue->inFlightGroup, DISPATCH_TIME_FOREVER);
    }
  }
  return GPU_OK;
}

GPU_HIDE
void
mt_destroyDevice(GPUDevice * __restrict device) {
  GPUDeviceMT *deviceMT;

  if (!device) {
    return;
  }

  deviceMT = device->_priv;
  if (deviceMT) {
    if (deviceMT->createdQueues) {
      for (uint32_t i = 0; i < deviceMT->nCreatedQueues; i++) {
        mt_destroyCommandQueue(deviceMT->createdQueues[i]);
      }
      free(deviceMT->createdQueues);
    }
    mt_destroyTextureViewPools(deviceMT);
    mt_destroyPipelineCompiler(deviceMT);
    free(deviceMT);
  }

  free(device);
}

GPU_HIDE
void
mt_initDevice(GPUApiDevice *apiDevice) {
  apiDevice->getAvailableAdapters        = mt_getAvailableAdapters;
  apiDevice->selectAdapter               = mt_selectAdapter;
  apiDevice->destroyAdapter              = mt_destroyAdapter;
  apiDevice->getAdapterProperties        = mt_getAdapterProperties;
  apiDevice->supportsFeature             = mt_supportsFeature;
  apiDevice->supportsSubgroupOperations  = mt_supportsSubgroupOperations;
  apiDevice->getLimits                   = mt_getLimits;
  apiDevice->getFormatCapabilities       = mt_getFormatCapabilities;
  apiDevice->getSubgroupMatrixProperties = mt_getSubgroupMatrixProperties;
  apiDevice->createDevice                = mt_createDevice;
  apiDevice->waitIdle                    = mt_waitDeviceIdle;
  apiDevice->destroyDevice               = mt_destroyDevice;
}
