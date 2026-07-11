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

#include "../common.h"
#include "../impl.h"

#include <d3dcompiler.h>
#include <limits.h>

typedef struct DXCBuffer {
  const void *ptr;
  SIZE_T      size;
  UINT        encoding;
} DXCBuffer;

typedef struct DXCBlob       DXCBlob;
typedef struct DXCCompiler3  DXCCompiler3;
typedef struct DXCResult     DXCResult;

typedef struct DXCBlobVtbl {
  HRESULT (STDMETHODCALLTYPE *QueryInterface)(DXCBlob *, REFIID, void **);
  ULONG   (STDMETHODCALLTYPE *AddRef)(DXCBlob *);
  ULONG   (STDMETHODCALLTYPE *Release)(DXCBlob *);
  void   *(STDMETHODCALLTYPE *GetBufferPointer)(DXCBlob *);
  SIZE_T  (STDMETHODCALLTYPE *GetBufferSize)(DXCBlob *);
} DXCBlobVtbl;

struct DXCBlob {
  DXCBlobVtbl *lpVtbl;
};

typedef struct DXCCompiler3Vtbl {
  HRESULT (STDMETHODCALLTYPE *QueryInterface)(DXCCompiler3 *, REFIID, void **);
  ULONG   (STDMETHODCALLTYPE *AddRef)(DXCCompiler3 *);
  ULONG   (STDMETHODCALLTYPE *Release)(DXCCompiler3 *);
  HRESULT (STDMETHODCALLTYPE *Compile)(DXCCompiler3 *,
                                        const DXCBuffer *,
                                        LPCWSTR *,
                                        UINT32,
                                        void *,
                                        REFIID,
                                        void **);
  HRESULT (STDMETHODCALLTYPE *Disassemble)(DXCCompiler3 *,
                                            const DXCBuffer *,
                                            REFIID,
                                            void **);
} DXCCompiler3Vtbl;

struct DXCCompiler3 {
  DXCCompiler3Vtbl *lpVtbl;
};

typedef struct DXCResultVtbl {
  HRESULT (STDMETHODCALLTYPE *QueryInterface)(DXCResult *, REFIID, void **);
  ULONG   (STDMETHODCALLTYPE *AddRef)(DXCResult *);
  ULONG   (STDMETHODCALLTYPE *Release)(DXCResult *);
  HRESULT (STDMETHODCALLTYPE *GetStatus)(DXCResult *, HRESULT *);
  HRESULT (STDMETHODCALLTYPE *GetResult)(DXCResult *, DXCBlob **);
  HRESULT (STDMETHODCALLTYPE *GetErrorBuffer)(DXCResult *, DXCBlob **);
} DXCResultVtbl;

struct DXCResult {
  DXCResultVtbl *lpVtbl;
};

typedef HRESULT (WINAPI *DXCCreateInstanceFn)(REFCLSID, REFIID, void **);

static const CLSID dx12_clsidDxcCompiler = {
  0x73e22d93,
  0xe6ce,
  0x47f3,
  {0xb5, 0xbf, 0xf0, 0x66, 0x4f, 0x39, 0xc1, 0xb0}
};

static const IID dx12_iidDxcCompiler3 = {
  0x228b4687,
  0x5a6a,
  0x4730,
  {0x90, 0x0c, 0x97, 0x02, 0xb2, 0x20, 0x3f, 0x54}
};

static const IID dx12_iidDxcResult = {
  0x58346cda,
  0xdde7,
  0x4497,
  {0x94, 0x61, 0x6f, 0x87, 0xaf, 0x5e, 0x06, 0x59}
};

static const DXGI_FORMAT dx12_vertexFormats[GPUVertexFormatHalf + 1u] = {
  [GPUUChar2]                  = DXGI_FORMAT_R8G8_UINT,
  [GPUUChar4]                  = DXGI_FORMAT_R8G8B8A8_UINT,
  [GPUChar2]                   = DXGI_FORMAT_R8G8_SINT,
  [GPUChar4]                   = DXGI_FORMAT_R8G8B8A8_SINT,
  [GPUUChar2Normalized]        = DXGI_FORMAT_R8G8_UNORM,
  [GPUUChar4Normalized]        = DXGI_FORMAT_R8G8B8A8_UNORM,
  [GPUChar2Normalized]         = DXGI_FORMAT_R8G8_SNORM,
  [GPUChar4Normalized]         = DXGI_FORMAT_R8G8B8A8_SNORM,
  [GPUUShort2]                 = DXGI_FORMAT_R16G16_UINT,
  [GPUUShort4]                 = DXGI_FORMAT_R16G16B16A16_UINT,
  [GPUShort2]                  = DXGI_FORMAT_R16G16_SINT,
  [GPUShort4]                  = DXGI_FORMAT_R16G16B16A16_SINT,
  [GPUUShort2Normalized]       = DXGI_FORMAT_R16G16_UNORM,
  [GPUUShort4Normalized]       = DXGI_FORMAT_R16G16B16A16_UNORM,
  [GPUShort2Normalized]        = DXGI_FORMAT_R16G16_SNORM,
  [GPUShort4Normalized]        = DXGI_FORMAT_R16G16B16A16_SNORM,
  [GPUHalf2]                   = DXGI_FORMAT_R16G16_FLOAT,
  [GPUHalf4]                   = DXGI_FORMAT_R16G16B16A16_FLOAT,
  [GPUFloat]                   = DXGI_FORMAT_R32_FLOAT,
  [GPUFloat2]                  = DXGI_FORMAT_R32G32_FLOAT,
  [GPUFloat3]                  = DXGI_FORMAT_R32G32B32_FLOAT,
  [GPUFloat4]                  = DXGI_FORMAT_R32G32B32A32_FLOAT,
  [GPUInt]                     = DXGI_FORMAT_R32_SINT,
  [GPUInt2]                    = DXGI_FORMAT_R32G32_SINT,
  [GPUInt3]                    = DXGI_FORMAT_R32G32B32_SINT,
  [GPUInt4]                    = DXGI_FORMAT_R32G32B32A32_SINT,
  [GPUUInt]                    = DXGI_FORMAT_R32_UINT,
  [GPUUInt2]                   = DXGI_FORMAT_R32G32_UINT,
  [GPUUInt3]                   = DXGI_FORMAT_R32G32B32_UINT,
  [GPUUInt4]                   = DXGI_FORMAT_R32G32B32A32_UINT,
  [GPUUInt1010102Normalized]   = DXGI_FORMAT_R10G10B10A2_UNORM,
  [GPUUChar4Normalized_BGRA]   = DXGI_FORMAT_B8G8R8A8_UNORM,
  [GPUUChar]                   = DXGI_FORMAT_R8_UINT,
  [GPUChar]                    = DXGI_FORMAT_R8_SINT,
  [GPUUCharNormalized]         = DXGI_FORMAT_R8_UNORM,
  [GPUCharNormalized]          = DXGI_FORMAT_R8_SNORM,
  [GPUUShort]                  = DXGI_FORMAT_R16_UINT,
  [GPUShort]                   = DXGI_FORMAT_R16_SINT,
  [GPUUShortNormalized]        = DXGI_FORMAT_R16_UNORM,
  [GPUShortNormalized]         = DXGI_FORMAT_R16_SNORM,
  [GPUVertexFormatHalf]        = DXGI_FORMAT_R16_FLOAT
};

GPU_HIDE
void
dx12_freeShaderCode(DX12ShaderCode *code) {
  if (!code) {
    return;
  }

  free(code->data);
  memset(code, 0, sizeof(*code));
}

static bool
dx12__copyShaderBlob(const void       *data,
                     SIZE_T            size,
                     DX12ShaderCode   *outCode) {
  if (!data || size == 0u || !outCode) {
    return false;
  }

  outCode->data = malloc(size);
  if (!outCode->data) {
    return false;
  }

  memcpy(outCode->data, data, size);
  outCode->size = size;
  return true;
}

static void
dx12__logShaderError(const char *entry, const void *data, SIZE_T size) {
  if (!data || size == 0u) {
    return;
  }

  fprintf(stderr,
          "GPU Direct3D 12 shader '%s' failed:\n%.*s\n",
          entry ? entry : "",
          (int)(size > INT_MAX ? INT_MAX : size),
          (const char *)data);
}

static bool
dx12__wideEntry(const char *entry, wchar_t outEntry[256]) {
  int count;

  if (!entry || !outEntry) {
    return false;
  }

  count = MultiByteToWideChar(CP_UTF8,
                              MB_ERR_INVALID_CHARS,
                              entry,
                              -1,
                              outEntry,
                              256);
  return count > 0;
}

static bool
dx12__compileDXC(GPUDeviceDX12   *device,
                 const char     *source,
                 uint64_t        sourceSize,
                 const char     *entry,
                 const wchar_t  *profile,
                 DX12ShaderCode *outCode) {
  DXCCreateInstanceFn createInstance;
  DXCCompiler3       *compiler;
  DXCResult          *result;
  DXCBlob            *blob;
  DXCBlob            *errors;
  DXCBuffer           sourceBuffer;
  wchar_t             entryWide[256];
  LPCWSTR             args[8];
  HRESULT             compileStatus;
  HRESULT             rc;

  if (!device || !device->dxcAvailable || !device->dxcModule ||
      !source || sourceSize == 0u || sourceSize > (uint64_t)SIZE_MAX ||
      !profile || !outCode || !dx12__wideEntry(entry, entryWide)) {
    return false;
  }

  createInstance = (DXCCreateInstanceFn)GetProcAddress(device->dxcModule,
                                                        "DxcCreateInstance");
  if (!createInstance) {
    return false;
  }

  compiler = NULL;
  result   = NULL;
  blob     = NULL;
  errors   = NULL;
  rc = createInstance(&dx12_clsidDxcCompiler,
                      &dx12_iidDxcCompiler3,
                      (void **)&compiler);
  if (FAILED(rc) || !compiler) {
    return false;
  }

  sourceBuffer.ptr      = source;
  sourceBuffer.size     = (SIZE_T)sourceSize;
  sourceBuffer.encoding = CP_UTF8;
  args[0]               = L"-E";
  args[1]               = entryWide;
  args[2]               = L"-T";
  args[3]               = profile;
  args[4]               = L"-O3";
  args[5]               = L"-Ges";
  args[6]               = L"-Qstrip_debug";
  args[7]               = L"-Qstrip_reflect";
  rc = compiler->lpVtbl->Compile(compiler,
                                  &sourceBuffer,
                                  args,
                                  (UINT32)GPU_ARRAY_LEN(args),
                                  NULL,
                                  &dx12_iidDxcResult,
                                  (void **)&result);
  compiler->lpVtbl->Release(compiler);
  if (FAILED(rc) || !result) {
    return false;
  }

  compileStatus = E_FAIL;
  (void)result->lpVtbl->GetStatus(result, &compileStatus);
  (void)result->lpVtbl->GetErrorBuffer(result, &errors);
  if (errors && errors->lpVtbl->GetBufferSize(errors) > 1u) {
    dx12__logShaderError(entry,
                         errors->lpVtbl->GetBufferPointer(errors),
                         errors->lpVtbl->GetBufferSize(errors));
  }

  if (SUCCEEDED(compileStatus) &&
      SUCCEEDED(result->lpVtbl->GetResult(result, &blob)) &&
      blob) {
    rc = dx12__copyShaderBlob(blob->lpVtbl->GetBufferPointer(blob),
                              blob->lpVtbl->GetBufferSize(blob),
                              outCode) ? S_OK : E_OUTOFMEMORY;
  } else {
    rc = E_FAIL;
  }

  if (blob) {
    blob->lpVtbl->Release(blob);
  }
  if (errors) {
    errors->lpVtbl->Release(errors);
  }
  result->lpVtbl->Release(result);
  return SUCCEEDED(rc);
}

static bool
dx12__compileLegacy(const char     *source,
                    uint64_t        sourceSize,
                    const char     *entry,
                    const char     *profile,
                    DX12ShaderCode *outCode) {
  ID3DBlob *blob;
  ID3DBlob *errors;
  UINT      flags;
  HRESULT   result;

  if (!source || sourceSize == 0u || sourceSize > (uint64_t)SIZE_MAX ||
      !entry || !profile || !outCode) {
    return false;
  }

  blob   = NULL;
  errors = NULL;
  flags  = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
  result = D3DCompile(source,
                      (SIZE_T)sourceSize,
                      NULL,
                      NULL,
                      NULL,
                      entry,
                      profile,
                      flags,
                      0u,
                      &blob,
                      &errors);
  if (errors && errors->lpVtbl->GetBufferSize(errors) > 1u) {
    dx12__logShaderError(entry,
                         errors->lpVtbl->GetBufferPointer(errors),
                         errors->lpVtbl->GetBufferSize(errors));
  }
  if (SUCCEEDED(result) && blob) {
    result = dx12__copyShaderBlob(blob->lpVtbl->GetBufferPointer(blob),
                                  blob->lpVtbl->GetBufferSize(blob),
                                  outCode) ? S_OK : E_OUTOFMEMORY;
  }

  if (blob) {
    blob->lpVtbl->Release(blob);
  }
  if (errors) {
    errors->lpVtbl->Release(errors);
  }
  return SUCCEEDED(result);
}

GPU_HIDE
bool
dx12_compileShader(GPUDeviceDX12      *device,
                   GPULibraryDX12     *library,
                   const char         *entry,
                   GPUShaderStageFlags stage,
                   DX12ShaderCode     *outCode) {
  static const wchar_t *dxcProfiles[GPU_SHADER_STAGE_COMPUTE_BIT + 1u] = {
    [GPU_SHADER_STAGE_VERTEX_BIT]   = L"vs_6_0",
    [GPU_SHADER_STAGE_FRAGMENT_BIT] = L"ps_6_0",
    [GPU_SHADER_STAGE_COMPUTE_BIT]  = L"cs_6_0"
  };
  static const char *legacyProfiles[GPU_SHADER_STAGE_COMPUTE_BIT + 1u] = {
    [GPU_SHADER_STAGE_VERTEX_BIT]   = "vs_5_1",
    [GPU_SHADER_STAGE_FRAGMENT_BIT] = "ps_5_1",
    [GPU_SHADER_STAGE_COMPUTE_BIT]  = "cs_5_1"
  };

  if (!device || !library || !entry || !outCode ||
      stage >= GPU_ARRAY_LEN(dxcProfiles) || !dxcProfiles[stage] ||
      !legacyProfiles[stage]) {
    return false;
  }

  if (device->dxcAvailable) {
    return dx12__compileDXC(device,
                            library->source,
                            library->sourceSize,
                            entry,
                            dxcProfiles[stage],
                            outCode);
  }

  return dx12__compileLegacy(library->source,
                             library->sourceSize,
                             entry,
                             legacyProfiles[stage],
                             outCode);
}

static bool
dx12__topology(GPUPrimitiveTopology             topology,
               D3D12_PRIMITIVE_TOPOLOGY_TYPE *outType,
               D3D_PRIMITIVE_TOPOLOGY        *outTopology) {
  if (!outType || !outTopology) {
    return false;
  }

  switch (topology) {
    case GPU_PRIMITIVE_TOPOLOGY_POINT_LIST:
      *outType     = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
      *outTopology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
      return true;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_LIST:
      *outType     = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      *outTopology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
      return true;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP:
      *outType     = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
      *outTopology = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
      return true;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
      *outType     = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      *outTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
      return true;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
      *outType     = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
      *outTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      return true;
    default:
      return false;
  }
}

static D3D12_CULL_MODE
dx12__cullMode(GPUCullMode mode) {
  static const D3D12_CULL_MODE modes[] = {
    [GPU_CULL_MODE_NONE]  = D3D12_CULL_MODE_NONE,
    [GPU_CULL_MODE_FRONT] = D3D12_CULL_MODE_FRONT,
    [GPU_CULL_MODE_BACK]  = D3D12_CULL_MODE_BACK
  };

  return (uint32_t)mode < GPU_ARRAY_LEN(modes)
           ? modes[mode]
           : D3D12_CULL_MODE_NONE;
}

static D3D12_COMPARISON_FUNC
dx12__compareFunction(GPUCompareOp op) {
  static const D3D12_COMPARISON_FUNC functions[] = {
    [GPU_COMPARE_NEVER]         = D3D12_COMPARISON_FUNC_NEVER,
    [GPU_COMPARE_LESS]          = D3D12_COMPARISON_FUNC_LESS,
    [GPU_COMPARE_EQUAL]         = D3D12_COMPARISON_FUNC_EQUAL,
    [GPU_COMPARE_LESS_EQUAL]    = D3D12_COMPARISON_FUNC_LESS_EQUAL,
    [GPU_COMPARE_GREATER]       = D3D12_COMPARISON_FUNC_GREATER,
    [GPU_COMPARE_NOT_EQUAL]     = D3D12_COMPARISON_FUNC_NOT_EQUAL,
    [GPU_COMPARE_GREATER_EQUAL] = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
    [GPU_COMPARE_ALWAYS]        = D3D12_COMPARISON_FUNC_ALWAYS
  };

  return (uint32_t)op < GPU_ARRAY_LEN(functions)
           ? functions[op]
           : D3D12_COMPARISON_FUNC_NEVER;
}

static D3D12_STENCIL_OP
dx12__stencilOperation(GPUStencilOp op) {
  static const D3D12_STENCIL_OP operations[] = {
    [GPU_STENCIL_OP_KEEP]            = D3D12_STENCIL_OP_KEEP,
    [GPU_STENCIL_OP_ZERO]            = D3D12_STENCIL_OP_ZERO,
    [GPU_STENCIL_OP_REPLACE]         = D3D12_STENCIL_OP_REPLACE,
    [GPU_STENCIL_OP_INCREMENT_CLAMP] = D3D12_STENCIL_OP_INCR_SAT,
    [GPU_STENCIL_OP_DECREMENT_CLAMP] = D3D12_STENCIL_OP_DECR_SAT,
    [GPU_STENCIL_OP_INVERT]          = D3D12_STENCIL_OP_INVERT,
    [GPU_STENCIL_OP_INCREMENT_WRAP]  = D3D12_STENCIL_OP_INCR,
    [GPU_STENCIL_OP_DECREMENT_WRAP]  = D3D12_STENCIL_OP_DECR
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : D3D12_STENCIL_OP_KEEP;
}

static void
dx12__fillStencilFace(D3D12_DEPTH_STENCILOP_DESC  *desc,
                      const GPUStencilFaceState   *state) {
  desc->StencilFailOp      = dx12__stencilOperation(state->failOp);
  desc->StencilDepthFailOp = dx12__stencilOperation(state->depthFailOp);
  desc->StencilPassOp      = dx12__stencilOperation(state->passOp);
  desc->StencilFunc        = dx12__compareFunction(state->compare);
}

static bool
dx12__inputLayout(const GPUVertexState       *state,
                  D3D12_INPUT_ELEMENT_DESC **outElements,
                  uint32_t                   *outCount) {
  D3D12_INPUT_ELEMENT_DESC *elements;
  uint32_t                  count;
  uint32_t                  cursor;

  if (!state || !outElements || !outCount) {
    return false;
  }

  *outElements = NULL;
  *outCount    = 0u;
  count        = 0u;
  for (uint32_t i = 0u; i < state->bufferLayoutCount; i++) {
    if (state->pBufferLayouts[i].attributeCount > UINT32_MAX - count) {
      return false;
    }
    count += state->pBufferLayouts[i].attributeCount;
  }
  if (count == 0u) {
    return true;
  }

  elements = calloc(count, sizeof(*elements));
  if (!elements) {
    return false;
  }

  cursor = 0u;
  for (uint32_t i = 0u; i < state->bufferLayoutCount; i++) {
    const GPUVertexBufferLayout *layout;

    layout = &state->pBufferLayouts[i];
    for (uint32_t j = 0u; j < layout->attributeCount; j++) {
      const GPUVertexAttribute *attribute;
      DXGI_FORMAT               format;

      attribute = &layout->pAttributes[j];
      format = (uint32_t)attribute->format < GPU_ARRAY_LEN(dx12_vertexFormats)
                 ? dx12_vertexFormats[attribute->format]
                 : DXGI_FORMAT_UNKNOWN;
      if (format == DXGI_FORMAT_UNKNOWN) {
        free(elements);
        return false;
      }

      elements[cursor].SemanticName         = "ATTRIBUTE";
      elements[cursor].SemanticIndex        = attribute->shaderLocation;
      elements[cursor].Format               = format;
      elements[cursor].InputSlot            = i;
      elements[cursor].AlignedByteOffset    = attribute->offset;
      elements[cursor].InputSlotClass       =
        layout->stepMode == GPU_VERTEX_STEP_MODE_INSTANCE
          ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
          : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
      elements[cursor].InstanceDataStepRate =
        layout->stepMode == GPU_VERTEX_STEP_MODE_INSTANCE ? 1u : 0u;
      cursor++;
    }
  }

  *outElements = elements;
  *outCount    = count;
  return true;
}

GPU_HIDE
GPUResult
dx12_createRenderPipeline(GPUDevice                         * __restrict device,
                          const GPURenderPipelineCreateInfo * __restrict info,
                          uint32_t                                       requiredBindGroupMask,
                          GPURenderPipeline                 * __restrict pipeline) {
  GPUDeviceDX12                  *deviceDX12;
  GPULibraryDX12                 *library;
  GPUPipelineLayoutDX12          *layout;
  GPURenderPipelineDX12          *native;
  const GPUDepthStencilState     *depthStencil;
  D3D12_INPUT_ELEMENT_DESC       *elements;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {0};
  DX12ShaderCode                 vertexCode = {0};
  DX12ShaderCode                 fragmentCode = {0};
  uint32_t                       elementCount;
  HRESULT                        result;

  if (!device || !device->_priv || !info || !info->library ||
      !info->layout || !pipeline) {
    return GPU_ERROR_UNSUPPORTED;
  }

  GPU__UNUSED(requiredBindGroupMask);

  elements     = NULL;
  elementCount = 0u;
  deviceDX12 = device->_priv;
  library    = info->library->_priv;
  layout     = info->layout->_native;
  depthStencil = info->pDepthStencilState;
  if (!library || !library->source || !layout || !layout->rootSignature ||
      info->vertex.bufferLayoutCount >
        D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT ||
      !dx12__inputLayout(&info->vertex, &elements, &elementCount)) {
    free(elements);
    return GPU_ERROR_UNSUPPORTED;
  }

  native = calloc(1, sizeof(*native));
  if (!native) {
    free(elements);
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  native->vertexBufferCount = info->vertex.bufferLayoutCount;
  for (uint32_t i = 0u; i < native->vertexBufferCount; i++) {
    native->vertexStrides[i] = info->vertex.pBufferLayouts[i].strideBytes;
  }
  if (!dx12__topology(info->primitiveTopology,
                      &desc.PrimitiveTopologyType,
                      &native->topology) ||
      !dx12_compileShader(deviceDX12,
                          library,
                          info->vertexEntry,
                          GPU_SHADER_STAGE_VERTEX_BIT,
                          &vertexCode) ||
      !dx12_compileShader(deviceDX12,
                          library,
                          info->fragmentEntry,
                          GPU_SHADER_STAGE_FRAGMENT_BIT,
                          &fragmentCode)) {
    dx12_freeShaderCode(&vertexCode);
    dx12_freeShaderCode(&fragmentCode);
    free(elements);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  desc.pRootSignature        = layout->rootSignature;
  desc.VS.pShaderBytecode    = vertexCode.data;
  desc.VS.BytecodeLength     = vertexCode.size;
  desc.PS.pShaderBytecode    = fragmentCode.data;
  desc.PS.BytecodeLength     = fragmentCode.size;
  desc.BlendState.AlphaToCoverageEnable  =
    info->multisample.alphaToCoverageEnable;
  desc.BlendState.IndependentBlendEnable = info->colorTargetCount > 1u;
  desc.SampleMask                              =
    info->multisample.sampleMask ? info->multisample.sampleMask : UINT_MAX;
  desc.RasterizerState.FillMode                = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode                = dx12__cullMode(info->cullMode);
  desc.RasterizerState.FrontCounterClockwise   =
    info->frontFace == GPU_FRONT_FACE_CCW;
  desc.RasterizerState.DepthBias               = D3D12_DEFAULT_DEPTH_BIAS;
  desc.RasterizerState.DepthBiasClamp          = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
  desc.RasterizerState.SlopeScaledDepthBias    =
    D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
  desc.RasterizerState.DepthClipEnable         = TRUE;
  desc.RasterizerState.MultisampleEnable       =
    info->multisample.sampleCount > 1u;
  desc.RasterizerState.AntialiasedLineEnable   = FALSE;
  desc.RasterizerState.ForcedSampleCount       = 0u;
  desc.RasterizerState.ConservativeRaster      =
    D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
  desc.DepthStencilState.DepthEnable           =
    depthStencil && (depthStencil->depthTestEnable ||
                     depthStencil->depthWriteEnable);
  desc.DepthStencilState.DepthWriteMask        =
    depthStencil && depthStencil->depthWriteEnable
      ? D3D12_DEPTH_WRITE_MASK_ALL
      : D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthStencilState.DepthFunc             =
    depthStencil && depthStencil->depthTestEnable
      ? dx12__compareFunction(depthStencil->depthCompare)
      : D3D12_COMPARISON_FUNC_ALWAYS;
  desc.DepthStencilState.StencilEnable         =
    depthStencil && depthStencil->stencilTestEnable;
  desc.DepthStencilState.StencilReadMask       =
    depthStencil ? (UINT8)depthStencil->stencilReadMask : 0u;
  desc.DepthStencilState.StencilWriteMask      =
    depthStencil ? (UINT8)depthStencil->stencilWriteMask : 0u;
  if (depthStencil) {
    dx12__fillStencilFace(&desc.DepthStencilState.FrontFace,
                          &depthStencil->front);
    dx12__fillStencilFace(&desc.DepthStencilState.BackFace,
                          &depthStencil->back);
  } else {
    GPUStencilFaceState defaultFace = {0};

    defaultFace.compare = GPU_COMPARE_ALWAYS;
    dx12__fillStencilFace(&desc.DepthStencilState.FrontFace, &defaultFace);
    dx12__fillStencilFace(&desc.DepthStencilState.BackFace, &defaultFace);
  }
  desc.InputLayout.pInputElementDescs          = elements;
  desc.InputLayout.NumElements                 = elementCount;
  desc.IBStripCutValue                         = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  desc.NumRenderTargets                        = info->colorTargetCount;
  desc.DSVFormat                               = dx12_format(info->depthStencilFormat);
  desc.SampleDesc.Count                        =
    info->multisample.sampleCount ? info->multisample.sampleCount : 1u;
  desc.SampleDesc.Quality                      = 0u;
  desc.Flags                                   = D3D12_PIPELINE_STATE_FLAG_NONE;

  for (uint32_t i = 0u; i < info->colorTargetCount; i++) {
    const GPUBlendState *blend;

    blend = &info->pColorTargets[i].blend;
    desc.RTVFormats[i] = dx12_format(info->pColorTargets[i].format);
    if (desc.RTVFormats[i] == DXGI_FORMAT_UNKNOWN) {
      result = E_INVALIDARG;
      goto done;
    }

    desc.BlendState.RenderTarget[i].BlendEnable           = blend->enabled;
    desc.BlendState.RenderTarget[i].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[i].SrcBlend              = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[i].DestBlend             = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[i].BlendOp               = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[i].SrcBlendAlpha         = D3D12_BLEND_ONE;
    desc.BlendState.RenderTarget[i].DestBlendAlpha        = D3D12_BLEND_ZERO;
    desc.BlendState.RenderTarget[i].BlendOpAlpha          = D3D12_BLEND_OP_ADD;
    desc.BlendState.RenderTarget[i].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[i].RenderTargetWriteMask =
      (UINT8)(blend->writeMask ? blend->writeMask : GPU_COLOR_WRITE_ALL);
  }

  result = deviceDX12->d3dDevice->lpVtbl->CreateGraphicsPipelineState(
    deviceDX12->d3dDevice,
    &desc,
    &IID_ID3D12PipelineState,
    (void **)&native->pipelineState
  );

done:
  dx12_freeShaderCode(&vertexCode);
  dx12_freeShaderCode(&fragmentCode);
  free(elements);
  if (FAILED(result)) {
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->rootSignature = layout->rootSignature;
  native->rootSignature->lpVtbl->AddRef(native->rootSignature);
  pipeline->_state = native;
  return GPU_OK;
}

GPU_HIDE
void
dx12_destroyRenderPipeline(GPURenderPipeline *pipeline) {
  GPURenderPipelineDX12 *native;

  if (!pipeline) {
    return;
  }

  native = pipeline->_state;
  if (native) {
    if (native->pipelineState) {
      native->pipelineState->lpVtbl->Release(native->pipelineState);
    }
    if (native->rootSignature) {
      native->rootSignature->lpVtbl->Release(native->rootSignature);
    }
    free(native);
  }
  free(pipeline);
}

GPU_HIDE
void
dx12_initRenderPipeline(GPUApiRender *api) {
  api->createPipeline        = dx12_createRenderPipeline;
  api->destroyRenderPipeline = dx12_destroyRenderPipeline;
}
