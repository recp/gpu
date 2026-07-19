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
#include "../impl.h"

static void
dx12_fillAdapterName(GPUAdapterDX12 *adapterDX12) {
  if (!adapterDX12) {
    return;
  }

  adapterDX12->name[0] = '\0';
  WideCharToMultiByte(CP_UTF8,
                      0,
                      adapterDX12->desc1.Description,
                      -1,
                      adapterDX12->name,
                      (int)sizeof(adapterDX12->name),
                      NULL,
                      NULL);
}

static bool
dx12_isParallels(const GPUAdapterDX12 *adapterDX12) {
  return adapterDX12 &&
         strstr(adapterDX12->name, "Parallels Display Adapter") != NULL;
}

static bool
dx12_queryResultsReliable(const GPUAdapterDX12 *adapterDX12) {
  return adapterDX12 &&
         !dx12_isParallels(adapterDX12);
}

static D3D_SHADER_MODEL
dx12_queryShaderModel(ID3D12Device *device) {
  /* Shader-model enum values encode major/minor as hexadecimal nibbles. */
  static const D3D_SHADER_MODEL models[] = {
    (D3D_SHADER_MODEL)0x6a,
    (D3D_SHADER_MODEL)0x69,
    (D3D_SHADER_MODEL)0x68,
    (D3D_SHADER_MODEL)0x67,
    D3D_SHADER_MODEL_6_6,
    D3D_SHADER_MODEL_6_5,
    (D3D_SHADER_MODEL)0x64,
    (D3D_SHADER_MODEL)0x63,
    D3D_SHADER_MODEL_6_2,
    D3D_SHADER_MODEL_6_1,
    D3D_SHADER_MODEL_6_0
  };
  D3D12_FEATURE_DATA_SHADER_MODEL shaderModel;

  if (!device) {
    return D3D_SHADER_MODEL_5_1;
  }

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(models); i++) {
    shaderModel.HighestShaderModel = models[i];
    if (SUCCEEDED(device->lpVtbl->CheckFeatureSupport(
          device,
          D3D12_FEATURE_SHADER_MODEL,
          &shaderModel,
          sizeof(shaderModel)))) {
      return shaderModel.HighestShaderModel;
    }
  }

  return D3D_SHADER_MODEL_5_1;
}

static bool
dx12_queryMeshShader(ID3D12Device    *device,
                     D3D_SHADER_MODEL shaderModel,
                     ID3D12Device2  **outDevice2) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {0};
  ID3D12Device2                    *device2;
  bool                              supported;

  if (outDevice2) {
    *outDevice2 = NULL;
  }
  if (!device || shaderModel < D3D_SHADER_MODEL_6_5 ||
      FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_D3D12_OPTIONS7,
        &options7,
        sizeof(options7))) ||
      options7.MeshShaderTier == D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
    return false;
  }

  device2 = NULL;
  supported = SUCCEEDED(device->lpVtbl->QueryInterface(
    device,
    &IID_ID3D12Device2,
    (void **)&device2
  )) && device2;
  if (!supported) {
    return false;
  }
  if (outDevice2) {
    *outDevice2 = device2;
  } else {
    device2->lpVtbl->Release(device2);
  }
  return true;
}

static uint32_t
dx12_querySamplerFeedback(ID3D12Device    *device,
                          D3D_SHADER_MODEL shaderModel,
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
                          ID3D12Device8  **outDevice8
#else
                          void           *outDevice8
#endif
                          ) {
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
  D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {0};
  ID3D12Device8                    *device8;

  if (outDevice8) {
    *outDevice8 = NULL;
  }
  if (!device || shaderModel < D3D_SHADER_MODEL_6_5 ||
      FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_D3D12_OPTIONS7,
        &options7,
        sizeof(options7))) ||
      options7.SamplerFeedbackTier ==
        D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED) {
    return 0u;
  }

  device8 = NULL;
  if (FAILED(device->lpVtbl->QueryInterface(device,
                                             &IID_ID3D12Device8,
                                             (void **)&device8)) ||
      !device8) {
    return 0u;
  }
  if (outDevice8) {
    *outDevice8 = device8;
  } else {
    device8->lpVtbl->Release(device8);
  }
  return (uint32_t)options7.SamplerFeedbackTier;
#else
  GPU__UNUSED(device);
  GPU__UNUSED(shaderModel);
  GPU__UNUSED(outDevice8);
  return 0u;
#endif
}

static bool
dx12_queryRayQuery(ID3D12Device    *device,
                   D3D_SHADER_MODEL shaderModel,
                   ID3D12Device5  **outDevice5) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {0};
  ID3D12Device5                    *device5;
  bool                              supported;

  if (outDevice5) {
    *outDevice5 = NULL;
  }
  if (!device || shaderModel < D3D_SHADER_MODEL_6_5 ||
      FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_D3D12_OPTIONS5,
        &options5,
        sizeof(options5))) ||
      options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_1) {
    return false;
  }

  device5 = NULL;
  supported = SUCCEEDED(device->lpVtbl->QueryInterface(
    device,
    &IID_ID3D12Device5,
    (void **)&device5
  )) && device5;
  if (!supported) {
    return false;
  }
  if (outDevice5) {
    *outDevice5 = device5;
  } else {
    device5->lpVtbl->Release(device5);
  }
  return true;
}

static bool
dx12_queryExecutionGraphs(ID3D12Device    *device,
                          D3D_SHADER_MODEL shaderModel,
                          ID3D12Device5  **outDevice5) {
#if GPU_DX12_HAS_EXECUTION_GRAPHS
  D3D12_FEATURE_DATA_D3D12_OPTIONS21 options21 = {0};
  ID3D12Device5                     *device5;
  bool                               supported;

  if (outDevice5) {
    *outDevice5 = NULL;
  }
  if (!device || shaderModel < (D3D_SHADER_MODEL)0x68 ||
      FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_D3D12_OPTIONS21,
        &options21,
        sizeof(options21))) ||
      options21.WorkGraphsTier < D3D12_WORK_GRAPHS_TIER_1_0) {
    return false;
  }

  device5 = NULL;
  supported = SUCCEEDED(device->lpVtbl->QueryInterface(
    device,
    &IID_ID3D12Device5,
    (void **)&device5
  )) && device5;
  if (!supported) {
    return false;
  }
  if (outDevice5) {
    *outDevice5 = device5;
  } else {
    device5->lpVtbl->Release(device5);
  }
  return true;
#else
  GPU__UNUSED(device);
  GPU__UNUSED(shaderModel);
  GPU__UNUSED(outDevice5);
  return false;
#endif
}

static bool
dx12_queryVRS(ID3D12Device                     *device,
              D3D12_VARIABLE_SHADING_RATE_TIER *outTier,
              uint32_t                         *outTileSize,
              bool                             *outAdditionalRates) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6 = {0};

  if (outTier) {
    *outTier = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
  }
  if (outTileSize) {
    *outTileSize = 0u;
  }
  if (outAdditionalRates) {
    *outAdditionalRates = false;
  }
  if (!device ||
      FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_D3D12_OPTIONS6,
        &options6,
        sizeof(options6))) ||
      options6.VariableShadingRateTier ==
        D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
    return false;
  }

  if (outTier) {
    *outTier = options6.VariableShadingRateTier;
  }
  if (outTileSize &&
      options6.VariableShadingRateTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2) {
    *outTileSize = options6.ShadingRateImageTileSize;
  }
  if (outAdditionalRates) {
    *outAdditionalRates = options6.AdditionalShadingRatesSupported != FALSE;
  }
  return true;
}

static bool
dx12_supportsShaderF16(ID3D12Device    *device,
                       D3D_SHADER_MODEL shaderModel) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS4 options4 = {0};

  return device && shaderModel >= D3D_SHADER_MODEL_6_2 &&
         SUCCEEDED(device->lpVtbl->CheckFeatureSupport(
           device,
           D3D12_FEATURE_D3D12_OPTIONS4,
           &options4,
           sizeof(options4))) &&
         options4.Native16BitShaderOpsSupported != FALSE;
}

static bool
dx12_supportsAtomic64(ID3D12Device    *device,
                      D3D_SHADER_MODEL shaderModel) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1  = {0};
  D3D12_FEATURE_DATA_D3D12_OPTIONS11 options11 = {0};

  return device && shaderModel >= D3D_SHADER_MODEL_6_6 &&
         SUCCEEDED(device->lpVtbl->CheckFeatureSupport(
           device,
           D3D12_FEATURE_D3D12_OPTIONS1,
           &options1,
           sizeof(options1))) &&
         options1.Int64ShaderOps != FALSE &&
         SUCCEEDED(device->lpVtbl->CheckFeatureSupport(
           device,
           D3D12_FEATURE_D3D12_OPTIONS11,
           &options11,
           sizeof(options11))) &&
         options11.AtomicInt64OnDescriptorHeapResourceSupported != FALSE;
}

static bool
dx12_supportsBindless(ID3D12Device *device) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};

  return device &&
         SUCCEEDED(device->lpVtbl->CheckFeatureSupport(
           device,
           D3D12_FEATURE_D3D12_OPTIONS,
           &options,
           sizeof(options))) &&
         options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2;
}

static bool
dx12_querySubgroups(ID3D12Device *device,
                    uint32_t     *outMinSubgroupSize,
                    uint32_t     *outMaxSubgroupSize) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1 = {0};

  if (outMinSubgroupSize) {
    *outMinSubgroupSize = 0u;
  }
  if (outMaxSubgroupSize) {
    *outMaxSubgroupSize = 0u;
  }
  if (!device || !outMinSubgroupSize || !outMaxSubgroupSize ||
      FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_D3D12_OPTIONS1,
        &options1,
        sizeof(options1))) ||
      options1.WaveOps == FALSE ||
      options1.WaveLaneCountMin == 0u ||
      options1.WaveLaneCountMax < options1.WaveLaneCountMin) {
    return false;
  }

  *outMinSubgroupSize = options1.WaveLaneCountMin;
  *outMaxSubgroupSize = options1.WaveLaneCountMax;
  return true;
}

#if defined(DIRECT3D_LINEAR_ALGEBRA)
typedef struct DX12SubgroupMatrixProfile {
  D3D12_LINEAR_ALGEBRA_DATATYPE         aType;
  D3D12_LINEAR_ALGEBRA_DATATYPE         bType;
  D3D12_LINEAR_ALGEBRA_DATATYPE         cType;
  GPUSubgroupMatrixComponentTypeEXT     gpuAType;
  GPUSubgroupMatrixComponentTypeEXT     gpuBType;
  GPUSubgroupMatrixComponentTypeEXT     gpuCType;
} DX12SubgroupMatrixProfile;

static bool
dx12_supportsMatrixConstruction(ID3D12Device                 *device,
                                D3D12_LINEAR_ALGEBRA_DATATYPE type) {
  D3D12_FEATURE_DATA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT query = {0};

  query.OperationType =
    D3D12_LINEAR_ALGEBRA_OPERATION_TYPE_MATRIX_CONSTRUCTION;
  query.MatrixConstruction.ComponentType = type;
  query.MatrixConstruction.WaveSize       = 0u;
  return SUCCEEDED(device->lpVtbl->CheckFeatureSupport(
           device,
           D3D12_FEATURE_LINEAR_ALGEBRA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT,
           &query,
           sizeof(query)
         )) &&
         query.MatrixConstruction.MinM > 0u &&
         query.MatrixConstruction.MinK > 0u &&
         query.MatrixConstruction.MinN > 0u;
}

static void
dx12_appendSubgroupMatrixProfile(GPUAdapterDX12                 *adapter,
                                 ID3D12Device                   *device,
                                 const DX12SubgroupMatrixProfile *profile) {
  D3D12_FEATURE_DATA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT query = {0};
  D3D12_LINEAR_ALGEBRA_MATRIX_MULTIPLY_SHAPE                 *shapes;
  GPUSubgroupMatrixPropertiesEXT                             *properties;
  uint32_t                                                    validCount;
  uint32_t                                                    oldCount;
  UINT                                                        shapeCount;

  if (!adapter || !device || !profile ||
      !dx12_supportsMatrixConstruction(device, profile->aType) ||
      !dx12_supportsMatrixConstruction(device, profile->bType) ||
      !dx12_supportsMatrixConstruction(device, profile->cType)) {
    return;
  }

  query.OperationType =
    D3D12_LINEAR_ALGEBRA_OPERATION_TYPE_WAVE_MATRIX_MULTIPLY;
  query.WaveMatrixMultiply.Inputs.WaveSize = 0u;
  query.WaveMatrixMultiply.Inputs.MatrixAComponentType = profile->aType;
  query.WaveMatrixMultiply.Inputs.MatrixBComponentType = profile->bType;
  query.WaveMatrixMultiply.Inputs.AccumulatorComponentType = profile->cType;
  if (FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_LINEAR_ALGEBRA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT,
        &query,
        sizeof(query)
      )) ||
      (query.WaveMatrixMultiply.SupportFlags &
       D3D12_LINEAR_ALGEBRA_MULTIPLICATION_SUPPORT_FLAG_SUPPORTED) == 0u ||
      query.WaveMatrixMultiply.NumShapes == 0u) {
    return;
  }

  shapeCount = query.WaveMatrixMultiply.NumShapes;
  if ((size_t)shapeCount > SIZE_MAX / sizeof(*shapes)) {
    return;
  }
  shapes = calloc((size_t)shapeCount, sizeof(*shapes));
  if (!shapes) {
    return;
  }

  query.WaveMatrixMultiply.NumShapes = shapeCount;
  query.WaveMatrixMultiply.Shapes    = shapes;
  if (FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_LINEAR_ALGEBRA_LINEAR_ALGEBRA_MATRIX_OPERATION_SUPPORT,
        &query,
        sizeof(query)
      ))) {
    free(shapes);
    return;
  }

  shapeCount = query.WaveMatrixMultiply.NumShapes < shapeCount
                 ? query.WaveMatrixMultiply.NumShapes
                 : shapeCount;
  validCount = 0u;
  for (UINT i = 0u; i < shapeCount; i++) {
    validCount += shapes[i].M > 0u && shapes[i].N > 0u && shapes[i].K > 0u;
  }
  oldCount = adapter->subgroupMatrixPropertyCount;
  if (validCount == 0u || oldCount > UINT32_MAX - validCount ||
      (size_t)(oldCount + validCount) > SIZE_MAX / sizeof(*properties)) {
    free(shapes);
    return;
  }

  properties = realloc(
    adapter->subgroupMatrixProperties,
    (size_t)(oldCount + validCount) * sizeof(*properties)
  );
  if (!properties) {
    free(shapes);
    return;
  }
  adapter->subgroupMatrixProperties = properties;
  for (UINT i = 0u; i < shapeCount; i++) {
    GPUSubgroupMatrixPropertiesEXT property = {0};

    if (shapes[i].M == 0u || shapes[i].N == 0u || shapes[i].K == 0u) {
      continue;
    }
    property.m          = shapes[i].M;
    property.n          = shapes[i].N;
    property.k          = shapes[i].K;
    property.aType      = profile->gpuAType;
    property.bType      = profile->gpuBType;
    property.cType      = profile->gpuCType;
    property.resultType = profile->gpuCType;
    property.stages     = GPU_SHADER_STAGE_COMPUTE_BIT;
    property.scope      = GPU_SUBGROUP_MATRIX_SCOPE_SUBGROUP_EXT;
    properties[oldCount++] = property;
  }
  adapter->subgroupMatrixPropertyCount = oldCount;
  free(shapes);
}

static void
dx12_querySubgroupMatrices(GPUAdapterDX12 *adapter, ID3D12Device *device) {
  static const DX12SubgroupMatrixProfile profiles[] = {
    {
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16,
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16,
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16,
      GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT,
      GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT,
      GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT
    },
    {
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16,
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT16,
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32,
      GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT,
      GPU_SUBGROUP_MATRIX_COMPONENT_F16_EXT,
      GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT
    },
    {
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32,
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32,
      D3D12_LINEAR_ALGEBRA_DATATYPE_FLOAT32,
      GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT,
      GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT,
      GPU_SUBGROUP_MATRIX_COMPONENT_F32_EXT
    }
  };
  D3D12_FEATURE_DATA_LINEAR_ALGEBRA_SUPPORT support = {0};

  if (!adapter || !device ||
      FAILED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_LINEAR_ALGEBRA_SUPPORT,
        &support,
        sizeof(support)
      )) ||
      support.LinearAlgebraTier < D3D12_LINEAR_ALGEBRA_TIER_1_0) {
    return;
  }

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(profiles); i++) {
    dx12_appendSubgroupMatrixProfile(adapter, device, &profiles[i]);
  }
}
#else
static void
dx12_querySubgroupMatrices(GPUAdapterDX12 *adapter, ID3D12Device *device) {
  GPU__UNUSED(adapter);
  GPU__UNUSED(device);
}
#endif

static HMODULE
dx12_loadDXCompiler(void) {
  HMODULE module;

  module = LoadLibraryW(L"dxcompiler.dll");
  if (module && !GetProcAddress(module, "DxcCreateInstance")) {
    FreeLibrary(module);
    return NULL;
  }
  return module;
}

static bool
dx12_probeAdapter(GPUAdapterDX12 *adapter, bool linearAlgebra) {
  ID3D12Device    *device;
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
  D3D_SHADER_MODEL shaderModel;
  HMODULE          dxcModule;
  HRESULT          result;
  bool             additionalRates;

  if (!adapter || !adapter->dxgiAdapter) {
    return false;
  }

  device = NULL;
  result = D3D12CreateDevice(adapter->dxgiAdapter,
                             D3D_FEATURE_LEVEL_11_0,
                             &IID_ID3D12Device,
                             (void **)&device);
  if (FAILED(result) || !device) {
    return false;
  }

  shaderModel        = dx12_queryShaderModel(device);
  dxcModule          = dx12_loadDXCompiler();
  adapter->subgroups = dxcModule && shaderModel >= D3D_SHADER_MODEL_6_0 &&
                       dx12_querySubgroups(device,
                                           &adapter->minSubgroupSize,
                                           &adapter->maxSubgroupSize);
  adapter->shaderF16 = dxcModule &&
                       dx12_supportsShaderF16(device, shaderModel);
  adapter->atomic64 = dxcModule &&
                      dx12_supportsAtomic64(device, shaderModel);
  adapter->descriptorIndexing =
    dxcModule && shaderModel >= D3D_SHADER_MODEL_6_0;
  adapter->bindless = adapter->descriptorIndexing &&
                      dx12_supportsBindless(device);
  adapter->meshShader = dxcModule &&
                        dx12_queryMeshShader(device, shaderModel, NULL);
  adapter->rayQuery = dxcModule &&
                      dx12_queryRayQuery(device, shaderModel, NULL);
  adapter->rayTracingPipeline = adapter->rayQuery;
  adapter->executionGraph = dxcModule &&
                            dx12_queryExecutionGraphs(device,
                                                      shaderModel,
                                                      NULL);
  adapter->samplerFeedbackTier = dxcModule
    ? dx12_querySamplerFeedback(device, shaderModel, NULL)
    : 0u;
  if (linearAlgebra && dxcModule &&
      dx12_hasLinearAlgebraCompiler(dxcModule) &&
      shaderModel >= (D3D_SHADER_MODEL)0x6a) {
    dx12_querySubgroupMatrices(adapter, device);
  }
  adapter->tiledResourcesTier = D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
  if (SUCCEEDED(device->lpVtbl->CheckFeatureSupport(
        device,
        D3D12_FEATURE_D3D12_OPTIONS,
        &options,
        sizeof(options)))) {
    adapter->tiledResourcesTier = options.TiledResourcesTier;
  }
  additionalRates = false;
  if (dx12_queryVRS(device,
                    &adapter->vrsTier,
                    &adapter->vrsTileSize,
                    &additionalRates)) {
    adapter->vrsRates = GPU_SHADING_RATE_1X1_BIT_EXT |
                        GPU_SHADING_RATE_1X2_BIT_EXT |
                        GPU_SHADING_RATE_2X1_BIT_EXT |
                        GPU_SHADING_RATE_2X2_BIT_EXT;
    if (additionalRates) {
      adapter->vrsRates |= GPU_SHADING_RATE_2X4_BIT_EXT |
                           GPU_SHADING_RATE_4X2_BIT_EXT |
                           GPU_SHADING_RATE_4X4_BIT_EXT;
    }
    adapter->vrsCombiners = GPU_SHADING_RATE_COMBINER_KEEP_BIT_EXT;
    if (adapter->vrsTier >= D3D12_VARIABLE_SHADING_RATE_TIER_2) {
      adapter->vrsCombiners |= GPU_SHADING_RATE_COMBINER_REPLACE_BIT_EXT |
                               GPU_SHADING_RATE_COMBINER_MIN_BIT_EXT |
                               GPU_SHADING_RATE_COMBINER_MAX_BIT_EXT;
    }
  }
  if (dxcModule) {
    FreeLibrary(dxcModule);
  }
  device->lpVtbl->Release(device);
  return true;
}

static GPUAdapterType
dx12_adapterType(const GPUAdapterDX12 *adapterDX12) {
  const DXGI_ADAPTER_DESC1 *desc;

  if (!adapterDX12) {
    return GPU_ADAPTER_TYPE_UNKNOWN;
  }
  if (adapterDX12->isWarp) {
    return GPU_ADAPTER_TYPE_SOFTWARE;
  }

  desc = &adapterDX12->desc1;
  if (desc->Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
    return GPU_ADAPTER_TYPE_SOFTWARE;
  }
  if (desc->DedicatedVideoMemory > 1024ull * 1024ull * 1024ull) {
    return GPU_ADAPTER_TYPE_DISCRETE;
  }
  return GPU_ADAPTER_TYPE_INTEGRATED;
}

static void
dx12_queryDeviceCapabilities(GPUDeviceDX12 *device) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS options = {0};
  D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature = {0};
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12 = {0};
  uint32_t minSubgroupSize;
  uint32_t maxSubgroupSize;

  if (!device || !device->d3dDevice) {
    return;
  }

  rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
  if (FAILED(device->d3dDevice->lpVtbl->CheckFeatureSupport(
        device->d3dDevice,
        D3D12_FEATURE_ROOT_SIGNATURE,
        &rootSignature,
        sizeof(rootSignature)))) {
    rootSignature.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
  }
  device->rootSignatureVersion = rootSignature.HighestVersion;

  device->resourceHeapTier = D3D12_RESOURCE_HEAP_TIER_1;
  device->tiledResourcesTier = D3D12_TILED_RESOURCES_TIER_NOT_SUPPORTED;
  if (SUCCEEDED(device->d3dDevice->lpVtbl->CheckFeatureSupport(
        device->d3dDevice,
        D3D12_FEATURE_D3D12_OPTIONS,
        &options,
        sizeof(options)))) {
    device->resourceHeapTier = options.ResourceHeapTier;
    device->tiledResourcesTier = options.TiledResourcesTier;
  }

  device->shaderModel = dx12_queryShaderModel(device->d3dDevice);

  if (SUCCEEDED(device->d3dDevice->lpVtbl->CheckFeatureSupport(
        device->d3dDevice,
        D3D12_FEATURE_D3D12_OPTIONS12,
        &options12,
        sizeof(options12)))) {
    device->enhancedBarriers = options12.EnhancedBarriersSupported != FALSE;
  }

  device->dxcModule    = dx12_loadDXCompiler();
  device->dxcAvailable = device->dxcModule != NULL &&
                         device->shaderModel >= D3D_SHADER_MODEL_6_0;
  device->subgroups    = device->dxcAvailable &&
                         dx12_querySubgroups(device->d3dDevice,
                                             &minSubgroupSize,
                                             &maxSubgroupSize);
  device->shaderF16    = device->dxcAvailable &&
                         dx12_supportsShaderF16(device->d3dDevice,
                                               device->shaderModel);
  device->atomic64     = device->dxcAvailable &&
                         dx12_supportsAtomic64(device->d3dDevice,
                                              device->shaderModel);
  device->descriptorIndexing = device->dxcAvailable &&
                               device->shaderModel >= D3D_SHADER_MODEL_6_0;
  device->bindless = device->descriptorIndexing &&
                     dx12_supportsBindless(device->d3dDevice);
  device->meshShader = device->dxcAvailable &&
                       dx12_queryMeshShader(device->d3dDevice,
                                            device->shaderModel,
                                            &device->d3dDevice2);
  device->rayQuery = device->dxcAvailable &&
                     dx12_queryRayQuery(device->d3dDevice,
                                        device->shaderModel,
                                        &device->d3dDevice5);
  device->rayTracingPipeline = device->rayQuery;
  device->executionGraph = device->dxcAvailable &&
                           dx12_queryExecutionGraphs(
                             device->d3dDevice,
                             device->shaderModel,
                             device->d3dDevice5 ? NULL : &device->d3dDevice5
                           );
  device->samplerFeedbackTier = device->dxcAvailable
    ? dx12_querySamplerFeedback(device->d3dDevice,
                                device->shaderModel,
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
                                &device->d3dDevice8
#else
                                NULL
#endif
                                )
    : 0u;
  (void)dx12_queryVRS(device->d3dDevice,
                      &device->vrsTier,
                      &device->vrsTileSize,
                      NULL);
#if GPU_BUILD_WITH_DEBUG_MARKERS
  device->pixModule = LoadLibraryW(L"WinPixEventRuntime.dll");
  if (device->pixModule) {
    device->pixBeginEvent = (DX12PixBeginEventFn)GetProcAddress(
      device->pixModule,
      "PIXBeginEventOnCommandList"
    );
    device->pixEndEvent = (DX12PixEndEventFn)GetProcAddress(
      device->pixModule,
      "PIXEndEventOnCommandList"
    );
    if (!device->pixBeginEvent || !device->pixEndEvent) {
      FreeLibrary(device->pixModule);
      device->pixModule     = NULL;
      device->pixBeginEvent = NULL;
      device->pixEndEvent   = NULL;
    }
  }
#endif
}

static bool
dx12__newSignature(GPUDeviceDX12               *device,
                   D3D12_INDIRECT_ARGUMENT_TYPE type,
                   UINT                         stride,
                   ID3D12CommandSignature     **outSignature) {
  D3D12_INDIRECT_ARGUMENT_DESC argument = {0};
  D3D12_COMMAND_SIGNATURE_DESC desc     = {0};

  if (!device || !device->d3dDevice || !outSignature) {
    return false;
  }

  argument.Type         = type;
  desc.ByteStride       = stride;
  desc.NumArgumentDescs = 1u;
  desc.pArgumentDescs   = &argument;
  return SUCCEEDED(device->d3dDevice->lpVtbl->CreateCommandSignature(
    device->d3dDevice,
    &desc,
    NULL,
    &IID_ID3D12CommandSignature,
    (void **)outSignature
  ));
}

static bool
dx12__newSignatures(GPUDeviceDX12 *device) {
  return dx12__newSignature(device,
                            D3D12_INDIRECT_ARGUMENT_TYPE_DRAW,
                            sizeof(D3D12_DRAW_ARGUMENTS),
                            &device->drawSignature) &&
         dx12__newSignature(device,
                            D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED,
                            sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
                            &device->drawIndexedSignature) &&
         dx12__newSignature(device,
                            D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH,
                            sizeof(D3D12_DISPATCH_ARGUMENTS),
                            &device->dispatchSignature);
}

static void
dx12__freeSignatures(GPUDeviceDX12 *device) {
  if (!device) {
    return;
  }
  if (device->dispatchSignature) {
    device->dispatchSignature->lpVtbl->Release(device->dispatchSignature);
    device->dispatchSignature = NULL;
  }
  if (device->drawIndexedSignature) {
    device->drawIndexedSignature->lpVtbl->Release(
      device->drawIndexedSignature
    );
    device->drawIndexedSignature = NULL;
  }
  if (device->drawSignature) {
    device->drawSignature->lpVtbl->Release(device->drawSignature);
    device->drawSignature = NULL;
  }
}

GPU_HIDE
GPUAdapter *
dx12_getAvailableAdapters(GPUInstance * __restrict inst,
                          uint32_t                 maxNumberOfItems) {
  GPUAdapterDX12        *adapterDX12;
  GPUInstanceDX12       *instDX12;
  IDXGIFactory4         *dxgiFactory;
  GPUAdapter            *firstAdapter, *lastAdapter, *adapter;
  IDXGIAdapter1         *dxgiAdapter;
  IDXGIAdapter          *warpAdapter;
  UINT                   adapterIndex, i;
  HRESULT                hr;
  HRESULT              (*EnumAdapters1)(IDXGIFactory4*, UINT, IDXGIAdapter1**);

  firstAdapter  = lastAdapter = NULL;
  adapterIndex  = i = 0;
  instDX12      = inst->_priv;
  dxgiFactory   = instDX12->dxgiFactory;
  EnumAdapters1 = dxgiFactory->lpVtbl->EnumAdapters1;

  /* loop until we either enumerate all devices or hit the maximum count. */ 
  while (i < maxNumberOfItems
         && SUCCEEDED(EnumAdapters1(dxgiFactory, adapterIndex, &dxgiAdapter))) {
    if (dxgiAdapter) {
      adapterDX12              = calloc(1, sizeof(*adapterDX12));
      adapterDX12->dxgiAdapter = (IUnknown *)dxgiAdapter;
      InitializeSRWLock(&adapterDX12->formatCapsLock);

      dxgiAdapter->lpVtbl->GetDesc1(dxgiAdapter, &adapterDX12->desc1);
      dx12_fillAdapterName(adapterDX12);

      if (adapterDX12->desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
        /* Don't select the Basic Render Driver adapter.*/
        /* Release the current adapter before moving to next */
        dxgiAdapter->lpVtbl->Release(dxgiAdapter);
        free(adapterDX12);
        goto nxt;
      }

      if (!dx12_probeAdapter(adapterDX12, instDX12->linearAlgebra)) {
        dxgiAdapter->lpVtbl->Release(dxgiAdapter);
        free(adapterDX12);
        goto nxt;
      }

      adapter = calloc(1, sizeof(*adapter));
      adapter->_priv                      = adapterDX12;
      adapter->inst                       = inst;
      adapter->separatePresentQueue       = 1; /* builtin */
      adapter->supportsDisplayTiming      = 1; /* TODO */
      adapter->supportsIncrementalPresent = 1; /* TODO */
      adapter->supportsSwapchain          = 1; /* builtin */

      if (lastAdapter) { lastAdapter->next = adapter; }
      else             { firstAdapter      = adapter; }
      lastAdapter = adapter;

      /* move on to the next adapter. */
      /* dxgiAdapter->lpVtbl->Release(dxgiAdapter); */
      dxgiAdapter = NULL;
      i++;
    }
  nxt:
    adapterIndex++;
  }

  /* Use WARP when no hardware adapter is available. */
  if (!firstAdapter) {
    DXCHECK(dxgiFactory->lpVtbl->EnumWarpAdapter(dxgiFactory, 
                                                 &IID_IDXGIAdapter, 
                                                 (void **)&warpAdapter));
    adapter       = calloc(1, sizeof(*adapter));
    adapterDX12   = calloc(1, sizeof(*adapterDX12));

    adapterDX12->dxgiAdapter       = (IUnknown *)warpAdapter;
    adapterDX12->isWarp            = true;
    InitializeSRWLock(&adapterDX12->formatCapsLock);
    snprintf(adapterDX12->name, sizeof(adapterDX12->name), "WARP");
    if (!dx12_probeAdapter(adapterDX12, instDX12->linearAlgebra)) {
      warpAdapter->lpVtbl->Release(warpAdapter);
      free(adapterDX12);
      free(adapter);
      hr = E_FAIL;
      goto err;
    }
    adapter->_priv                      = adapterDX12;
    adapter->inst                       = inst;
    adapter->separatePresentQueue       = 1; /* builtin */
    adapter->supportsDisplayTiming      = 1; /* TODO */
    adapter->supportsIncrementalPresent = 1; /* TODO */
    adapter->supportsSwapchain          = 1; /* builtin */

    firstAdapter = adapter;
  }

  return firstAdapter;
err:
  dxThrowIfFailed(hr);
  return NULL;
}

GPU_HIDE
GPUAdapter *
dx12_selectAdapter(GPUInstance * __restrict inst,
                   GPUAdapter  * __restrict adapters) {
  GPUAdapter         *adapter;
  GPUAdapterDX12     *adapterDX12;
  GPUAdapter         *adaptersByType[4] = {0};
  GPUAdapter         *priorityList[4];
  DXGI_ADAPTER_DESC1 *desc;
  uint32_t            i;

  GPU__UNUSED(inst);
  adapter = adapters;

  /* Classify devices into different categories based on criteria */
  while (adapter) {
    adapterDX12 = adapter->_priv;
    desc        = &adapterDX12->desc1;

    if (adapterDX12->isWarp) {
      adaptersByType[3] = adapter; /* WARP */
    } else if (desc->Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      adaptersByType[2] = adapter; /* Other software adapter */
    } else if (desc->DedicatedVideoMemory > 1 * 1024 * 1024 * 1024) {
      adaptersByType[0] = adapter; /* Discrete GPU */
    } else if (desc->DedicatedVideoMemory > 0) {
      adaptersByType[1] = adapter; /* Integrated GPU */
    }

    adapter = adapter->next;
  }

  priorityList[0] = adaptersByType[0]; /* Discrete GPU   */
  priorityList[1] = adaptersByType[1]; /* Integrated GPU */
  priorityList[2] = adaptersByType[2]; /* Other          */
  priorityList[3] = adaptersByType[3]; /* WARP           */

  for (i = 0u; i < GPU_ARRAY_LEN(priorityList) &&
                  !(adapter = priorityList[i]); i++);

  if (!adapter) { goto err; }

#ifdef DEBUG
  desc = &((GPUAdapterDX12 *)adapter->_priv)->desc1;
  fprintf(stderr,
          "Selected GPU: %S, type: %d\n",
          desc->Description,
          desc->VendorId);
#endif

  return adapter;

err:
  return NULL;
}

GPU_HIDE
void
dx12_destroyAdapter(GPUAdapter * __restrict adapter) {
  GPUAdapterDX12 *adapterDX12;

  if (!adapter) {
    return;
  }

  adapterDX12 = adapter->_priv;
  if (adapterDX12) {
    if (adapterDX12->dxgiAdapter) {
      adapterDX12->dxgiAdapter->lpVtbl->Release(adapterDX12->dxgiAdapter);
    }
    free(adapterDX12->subgroupMatrixProperties);
    free(adapterDX12);
  }
  free(adapter);
}

GPU_HIDE
GPUResult
dx12_getAdapterProperties(const GPUAdapter     * __restrict adapter,
                          GPUAdapterProperties * __restrict outProps) {
  GPUAdapterDX12 *adapterDX12;

  if (!adapter || !outProps || !adapter->_priv) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  adapterDX12 = adapter->_priv;
  memset(outProps, 0, sizeof(*outProps));
  outProps->backend = GPU_BACKEND_DX12;
  outProps->name = adapterDX12->name[0] ?
    adapterDX12->name :
    "Direct3D 12";
  outProps->type = dx12_adapterType(adapterDX12);

  return GPU_OK;
}

GPU_HIDE
bool
dx12_supportsFeature(const GPUAdapter * __restrict adapter,
                     GPUFeature feature) {
  GPUAdapterDX12 *adapterDX12;

  if (!adapter || !adapter->_priv) {
    return false;
  }
  adapterDX12 = adapter->_priv;

  switch (feature) {
    case GPU_FEATURE_COMPUTE:
    case GPU_FEATURE_INDIRECT_DRAW:
    case GPU_FEATURE_MULTI_DRAW:
    case GPU_FEATURE_PLACED_RESOURCES:
    case GPU_FEATURE_BUFFER_DEVICE_ADDRESS:
      return true;
    case GPU_FEATURE_SPARSE_TEXTURES:
    case GPU_FEATURE_SPARSE_BUFFERS:
    case GPU_FEATURE_SPARSE_EXPLICIT_PLACEMENT:
      return adapterDX12->tiledResourcesTier >=
             D3D12_TILED_RESOURCES_TIER_1;
    case GPU_FEATURE_SHADER_F16:
      return adapterDX12->shaderF16;
    case GPU_FEATURE_ATOMIC64:
      return adapterDX12->atomic64;
    case GPU_FEATURE_DESCRIPTOR_INDEXING:
      return adapterDX12->descriptorIndexing;
    case GPU_FEATURE_BINDLESS:
      return adapterDX12->bindless;
    case GPU_FEATURE_MESH_SHADER:
      return adapterDX12->meshShader;
    case GPU_FEATURE_RAY_QUERY:
      return adapterDX12->rayQuery;
    case GPU_FEATURE_RAY_TRACING_PIPELINE:
      return adapterDX12->rayTracingPipeline;
    case GPU_FEATURE_EXECUTION_GRAPH:
      return adapterDX12->executionGraph;
    case GPU_FEATURE_SAMPLER_FEEDBACK:
      return adapterDX12->samplerFeedbackTier != 0u;
    case GPU_FEATURE_VARIABLE_RATE_SHADING:
      return adapterDX12->vrsTier !=
             D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
    case GPU_FEATURE_SUBGROUPS:
      return adapterDX12->subgroups;
    case GPU_FEATURE_SUBGROUP_MATRIX:
      return adapterDX12->subgroupMatrixPropertyCount > 0u;
    case GPU_FEATURE_TIMESTAMPS:
    case GPU_FEATURE_PIPELINE_STATISTICS:
      return dx12_queryResultsReliable(adapterDX12);
    default:
      return false;
  }
}

static bool
dx12_supportsSubgroupOperations(
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
  GPUAdapterDX12 *adapterDX12;

  adapterDX12 = adapter ? adapter->_priv : NULL;
  return adapterDX12 && adapterDX12->subgroups &&
         (supportedStages & stage) == stage &&
         (supportedOperations & operations) == operations;
}

static GPUResult
dx12_getSubgroupMatrixProperties(
  const GPUAdapter               * __restrict adapter,
  uint32_t                       * __restrict inoutPropertyCount,
  GPUSubgroupMatrixPropertiesEXT * __restrict outProperties) {
  GPUAdapterDX12 *adapterDX12;
  uint32_t        capacity;
  uint32_t        count;

  adapterDX12 = adapter ? adapter->_priv : NULL;
  if (!adapterDX12 || !inoutPropertyCount) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  capacity = *inoutPropertyCount;
  count    = adapterDX12->subgroupMatrixPropertyCount;
  *inoutPropertyCount = count;
  if (count == 0u || !adapterDX12->subgroupMatrixProperties) {
    return GPU_ERROR_UNSUPPORTED;
  }
  if (outProperties && capacity > 0u) {
    uint32_t copyCount = capacity < count ? capacity : count;

    memcpy(outProperties,
           adapterDX12->subgroupMatrixProperties,
           (size_t)copyCount * sizeof(*outProperties));
  }
  return outProperties && capacity < count
           ? GPU_ERROR_INSUFFICIENT_CAPACITY
           : GPU_OK;
}

static void
dx12_getLimits(const GPUAdapter * __restrict adapter,
               GPULimits       * __restrict outLimits) {
  GPUAdapterDX12 *adapterDX12;

  adapterDX12 = adapter ? adapter->_priv : NULL;
  if (!adapterDX12 || !outLimits) {
    return;
  }

  outLimits->maxColorAttachments      = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
  outLimits->maxComputeWorkgroupSizeX = D3D12_CS_THREAD_GROUP_MAX_X;
  outLimits->maxComputeWorkgroupSizeY = D3D12_CS_THREAD_GROUP_MAX_Y;
  outLimits->maxComputeWorkgroupSizeZ = D3D12_CS_THREAD_GROUP_MAX_Z;
  outLimits->minSubgroupSize           = adapterDX12->minSubgroupSize;
  outLimits->maxSubgroupSize           = adapterDX12->maxSubgroupSize;
}

GPU_HIDE
GPUDevice *
dx12_createDevice(GPUAdapter              * __restrict adapter,
                  const GPUQueueCreateInfo queCI[],
                  uint32_t                 nQueCI,
                  uint64_t                 enabledFeatureMask) {
  GPUInstance           *inst;
  GPUAdapterDX12        *adapterDX12;
  GPUDevice             *device;
  GPUDeviceDX12         *deviceDX12;
  HRESULT                hr;
  uint32_t               queueCount;
  uint32_t               queueIndex;
  uint32_t               i, j;

  device     = NULL;
  deviceDX12 = NULL;
  if (!adapter ||
      !(inst = adapter->inst) ||
      !(adapterDX12 = adapter->_priv)) {
    goto err;
  }

  GPU__DEFINE_DEFAULT_QUEUES_IF_NEEDED(nQueCI, queCI);

  device     = calloc(1, sizeof(*device));
  deviceDX12 = calloc(1, sizeof(*deviceDX12));
  if (!device || !deviceDX12) {
    goto err;
  }

  hr = D3D12CreateDevice(adapterDX12->dxgiAdapter,
                         D3D_FEATURE_LEVEL_11_0,
                         &IID_ID3D12Device,
                         (void **)&deviceDX12->d3dDevice);
  if (FAILED(hr)) {
    goto err;
  }
  /* Parallels removes or corrupts devices on combined stencil-plane copies. */
  deviceDX12->stencilPlaneCopies = !dx12_isParallels(adapterDX12);
  /* Parallels accepts query commands but never writes resolved data. */
  deviceDX12->queryResultsReliable = dx12_queryResultsReliable(adapterDX12);
  dx12_queryDeviceCapabilities(deviceDX12);
  deviceDX12->subgroupMatrix =
    adapterDX12->subgroupMatrixPropertyCount > 0u &&
    deviceDX12->dxcAvailable &&
    deviceDX12->shaderModel >= (D3D_SHADER_MODEL)0x6a;
  /* Parallels advertises enhanced barriers but fails on Barrier calls. */
  if (dx12_isParallels(adapterDX12)) {
    deviceDX12->enhancedBarriers = false;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SUBGROUPS)) != 0u &&
      !deviceDX12->subgroups) {
    goto err;
  }
  deviceDX12->subgroupMatrixEnabled =
    (enabledFeatureMask & (1ull << GPU_FEATURE_SUBGROUP_MATRIX)) != 0u;
  if (deviceDX12->subgroupMatrixEnabled && !deviceDX12->subgroupMatrix) {
    goto err;
  }
  deviceDX12->shaderF16Enabled =
    (enabledFeatureMask & (1ull << GPU_FEATURE_SHADER_F16)) != 0u;
  if (deviceDX12->shaderF16Enabled && !deviceDX12->shaderF16) {
    goto err;
  }
  deviceDX12->atomic64Enabled =
    (enabledFeatureMask & (1ull << GPU_FEATURE_ATOMIC64)) != 0u;
  if (deviceDX12->atomic64Enabled && !deviceDX12->atomic64) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_DESCRIPTOR_INDEXING)) != 0u &&
      !deviceDX12->descriptorIndexing) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_BINDLESS)) != 0u &&
      !deviceDX12->bindless) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MESH_SHADER)) != 0u &&
      !deviceDX12->meshShader) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_RAY_QUERY)) != 0u &&
      !deviceDX12->rayQuery) {
    goto err;
  }
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_RAY_TRACING_PIPELINE)) != 0u &&
      !deviceDX12->rayTracingPipeline) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_EXECUTION_GRAPH)) != 0u &&
      !deviceDX12->executionGraph) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SAMPLER_FEEDBACK)) != 0u &&
      deviceDX12->samplerFeedbackTier == 0u) {
    goto err;
  }
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_VARIABLE_RATE_SHADING)) != 0u &&
      deviceDX12->vrsTier ==
        D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED) {
    goto err;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_MESH_SHADER)) == 0u) {
    deviceDX12->meshShader = false;
    if (deviceDX12->d3dDevice2) {
      deviceDX12->d3dDevice2->lpVtbl->Release(deviceDX12->d3dDevice2);
      deviceDX12->d3dDevice2 = NULL;
    }
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_RAY_QUERY)) == 0u &&
      (enabledFeatureMask &
       (1ull << GPU_FEATURE_RAY_TRACING_PIPELINE)) == 0u &&
      (enabledFeatureMask & (1ull << GPU_FEATURE_EXECUTION_GRAPH)) == 0u) {
    deviceDX12->rayQuery = false;
    deviceDX12->rayTracingPipeline = false;
    deviceDX12->executionGraph = false;
    if (deviceDX12->d3dDevice5) {
      deviceDX12->d3dDevice5->lpVtbl->Release(deviceDX12->d3dDevice5);
      deviceDX12->d3dDevice5 = NULL;
    }
  }
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_RAY_TRACING_PIPELINE)) == 0u) {
    deviceDX12->rayTracingPipeline = false;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_EXECUTION_GRAPH)) == 0u) {
    deviceDX12->executionGraph = false;
  }
  if ((enabledFeatureMask & (1ull << GPU_FEATURE_SAMPLER_FEEDBACK)) == 0u) {
    deviceDX12->samplerFeedbackTier = 0u;
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
    if (deviceDX12->d3dDevice8) {
      deviceDX12->d3dDevice8->lpVtbl->Release(deviceDX12->d3dDevice8);
      deviceDX12->d3dDevice8 = NULL;
    }
#endif
  }
  if ((enabledFeatureMask &
       (1ull << GPU_FEATURE_VARIABLE_RATE_SHADING)) == 0u) {
    deviceDX12->vrsTier = D3D12_VARIABLE_SHADING_RATE_TIER_NOT_SUPPORTED;
    deviceDX12->vrsTileSize = 0u;
  }
  InitializeSRWLock(&deviceDX12->descriptorLock);
  if (!dx12__newSignatures(deviceDX12)) {
    goto err;
  }

  device->inst      = inst;
  device->_priv     = deviceDX12;
  device->adapter   = adapter;
  if (deviceDX12->meshShader) {
    device->meshLimits.taskWorkgroupSize[0] = 128u;
    device->meshLimits.taskWorkgroupSize[1] = 128u;
    device->meshLimits.taskWorkgroupSize[2] = 64u;
    device->meshLimits.meshWorkgroupSize[0] = 128u;
    device->meshLimits.meshWorkgroupSize[1] = 128u;
    device->meshLimits.meshWorkgroupSize[2] = 64u;
    device->meshLimits.maxTaskWorkgroupInvocations = 128u;
    device->meshLimits.maxMeshWorkgroupInvocations = 128u;
    device->meshLimits.maxPayloadSizeBytes          = 16u * 1024u;
    device->meshLimits.maxOutputVertices            = 256u;
    device->meshLimits.maxOutputPrimitives          = 256u;
  }
  if (deviceDX12->rayTracingPipeline) {
    device->rayTracingLimits.maxDispatchCount             = 1ull << 30u;
    device->rayTracingLimits.maxDispatchSize[0]           = UINT32_MAX;
    device->rayTracingLimits.maxDispatchSize[1]           = UINT32_MAX;
    device->rayTracingLimits.maxDispatchSize[2]           = UINT32_MAX;
    device->rayTracingLimits.maxRecursionDepth            =
      D3D12_RAYTRACING_MAX_DECLARABLE_TRACE_RECURSION_DEPTH;
    device->rayTracingLimits.maxHitAttributeSizeBytes     =
      D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
  }

  queueCount = 0u;
  for (i = 0u; i < nQueCI; i++) {
    queueCount += queCI[i].count;
  }
  if (queueCount > 0u) {
    deviceDX12->createdQueues = calloc(queueCount,
                                       sizeof(*deviceDX12->createdQueues));
    if (!deviceDX12->createdQueues) {
      goto err;
    }

    queueIndex = 0u;
    for (i = 0u; i < nQueCI; i++) {
      for (j = 0u; j < queCI[i].count; j++) {
        GPUQueue        *queue;

        queue = dx12_createCommandQueue(device, queCI[i].flags);
        if (!queue) {
          goto err;
        }
        deviceDX12->createdQueues[queueIndex++] = queue;
        deviceDX12->nCreatedQueues              = queueIndex;
        device->queueFamilies                  |= queue->bits;
      }
    }
  }

  return device;

err:
  if (deviceDX12) {
    if (deviceDX12->createdQueues) {
      for (i = 0u; i < deviceDX12->nCreatedQueues; i++) {
        dx12_destroyCommandQueue(deviceDX12->createdQueues[i]);
      }
      free(deviceDX12->createdQueues);
    }
    if (deviceDX12->d3dDevice) {
      dx12__freeSignatures(deviceDX12);
      dx12_destroyDescriptorHeaps(deviceDX12);
      if (deviceDX12->d3dDevice2) {
        deviceDX12->d3dDevice2->lpVtbl->Release(deviceDX12->d3dDevice2);
      }
      if (deviceDX12->d3dDevice5) {
        deviceDX12->d3dDevice5->lpVtbl->Release(deviceDX12->d3dDevice5);
      }
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
      if (deviceDX12->d3dDevice8) {
        deviceDX12->d3dDevice8->lpVtbl->Release(deviceDX12->d3dDevice8);
      }
#endif
      deviceDX12->d3dDevice->lpVtbl->Release(deviceDX12->d3dDevice);
    }
    if (deviceDX12->dxcModule) {
      FreeLibrary(deviceDX12->dxcModule);
    }
#if GPU_BUILD_WITH_DEBUG_MARKERS
    if (deviceDX12->pixModule) {
      FreeLibrary(deviceDX12->pixModule);
    }
#endif
    free(deviceDX12);
  }
  free(device);
  return NULL;
}

GPU_HIDE
void
dx12_destroyDevice(GPUDevice * __restrict device) {
  GPUDeviceDX12 *deviceDX12;

  if (!device) {
    return;
  }

  deviceDX12 = device->_priv;
  if (deviceDX12) {
    if (deviceDX12->createdQueues) {
      for (uint32_t i = 0u; i < deviceDX12->nCreatedQueues; i++) {
        dx12_destroyCommandQueue(deviceDX12->createdQueues[i]);
      }
      free(deviceDX12->createdQueues);
    }
    if (deviceDX12->d3dDevice) {
      dx12__freeSignatures(deviceDX12);
      dx12_destroyDescriptorHeaps(deviceDX12);
      if (deviceDX12->d3dDevice2) {
        deviceDX12->d3dDevice2->lpVtbl->Release(deviceDX12->d3dDevice2);
      }
      if (deviceDX12->d3dDevice5) {
        deviceDX12->d3dDevice5->lpVtbl->Release(deviceDX12->d3dDevice5);
      }
#if GPU_DX12_HAS_SAMPLER_FEEDBACK
      if (deviceDX12->d3dDevice8) {
        deviceDX12->d3dDevice8->lpVtbl->Release(deviceDX12->d3dDevice8);
      }
#endif
      deviceDX12->d3dDevice->lpVtbl->Release(deviceDX12->d3dDevice);
    }
    if (deviceDX12->dxcModule) {
      FreeLibrary(deviceDX12->dxcModule);
    }
#if GPU_BUILD_WITH_DEBUG_MARKERS
    if (deviceDX12->pixModule) {
      FreeLibrary(deviceDX12->pixModule);
    }
#endif
    free(deviceDX12);
  }
  free(device);
}

GPU_HIDE
void
dx12_initDevice(GPUApiDevice* apiDevice) {
  apiDevice->getAvailableAdapters       = dx12_getAvailableAdapters;
  apiDevice->selectAdapter              = dx12_selectAdapter;
  apiDevice->destroyAdapter             = dx12_destroyAdapter;
  apiDevice->getAdapterProperties       = dx12_getAdapterProperties;
  apiDevice->supportsFeature            = dx12_supportsFeature;
  apiDevice->supportsSubgroupOperations = dx12_supportsSubgroupOperations;
  apiDevice->getLimits                  = dx12_getLimits;
  apiDevice->getFormatCapabilities      = dx12_getFormatCapabilities;
  apiDevice->getSubgroupMatrixProperties =
    dx12_getSubgroupMatrixProperties;
  apiDevice->createDevice               = dx12_createDevice;
  apiDevice->waitIdle                   = dx12_waitDeviceIdle;
  apiDevice->destroyDevice              = dx12_destroyDevice;
}
