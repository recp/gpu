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
#include "pipeline_cache.h"

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

static const DXGI_FORMAT dx12_vertexFormats[GPU_VERTEX_FORMAT_COUNT] = {
  [GPU_VERTEX_FORMAT_UINT8]           = DXGI_FORMAT_R8_UINT,
  [GPU_VERTEX_FORMAT_UINT8X2]         = DXGI_FORMAT_R8G8_UINT,
  [GPU_VERTEX_FORMAT_UINT8X4]         = DXGI_FORMAT_R8G8B8A8_UINT,
  [GPU_VERTEX_FORMAT_SINT8]           = DXGI_FORMAT_R8_SINT,
  [GPU_VERTEX_FORMAT_SINT8X2]         = DXGI_FORMAT_R8G8_SINT,
  [GPU_VERTEX_FORMAT_SINT8X4]         = DXGI_FORMAT_R8G8B8A8_SINT,
  [GPU_VERTEX_FORMAT_UNORM8]          = DXGI_FORMAT_R8_UNORM,
  [GPU_VERTEX_FORMAT_UNORM8X2]        = DXGI_FORMAT_R8G8_UNORM,
  [GPU_VERTEX_FORMAT_UNORM8X4]        = DXGI_FORMAT_R8G8B8A8_UNORM,
  [GPU_VERTEX_FORMAT_SNORM8]          = DXGI_FORMAT_R8_SNORM,
  [GPU_VERTEX_FORMAT_SNORM8X2]        = DXGI_FORMAT_R8G8_SNORM,
  [GPU_VERTEX_FORMAT_SNORM8X4]        = DXGI_FORMAT_R8G8B8A8_SNORM,
  [GPU_VERTEX_FORMAT_UINT16]          = DXGI_FORMAT_R16_UINT,
  [GPU_VERTEX_FORMAT_UINT16X2]        = DXGI_FORMAT_R16G16_UINT,
  [GPU_VERTEX_FORMAT_UINT16X4]        = DXGI_FORMAT_R16G16B16A16_UINT,
  [GPU_VERTEX_FORMAT_SINT16]          = DXGI_FORMAT_R16_SINT,
  [GPU_VERTEX_FORMAT_SINT16X2]        = DXGI_FORMAT_R16G16_SINT,
  [GPU_VERTEX_FORMAT_SINT16X4]        = DXGI_FORMAT_R16G16B16A16_SINT,
  [GPU_VERTEX_FORMAT_UNORM16]         = DXGI_FORMAT_R16_UNORM,
  [GPU_VERTEX_FORMAT_UNORM16X2]       = DXGI_FORMAT_R16G16_UNORM,
  [GPU_VERTEX_FORMAT_UNORM16X4]       = DXGI_FORMAT_R16G16B16A16_UNORM,
  [GPU_VERTEX_FORMAT_SNORM16]         = DXGI_FORMAT_R16_SNORM,
  [GPU_VERTEX_FORMAT_SNORM16X2]       = DXGI_FORMAT_R16G16_SNORM,
  [GPU_VERTEX_FORMAT_SNORM16X4]       = DXGI_FORMAT_R16G16B16A16_SNORM,
  [GPU_VERTEX_FORMAT_FLOAT16]         = DXGI_FORMAT_R16_FLOAT,
  [GPU_VERTEX_FORMAT_FLOAT16X2]       = DXGI_FORMAT_R16G16_FLOAT,
  [GPU_VERTEX_FORMAT_FLOAT16X4]       = DXGI_FORMAT_R16G16B16A16_FLOAT,
  [GPU_VERTEX_FORMAT_FLOAT32]         = DXGI_FORMAT_R32_FLOAT,
  [GPU_VERTEX_FORMAT_FLOAT32X2]       = DXGI_FORMAT_R32G32_FLOAT,
  [GPU_VERTEX_FORMAT_FLOAT32X3]       = DXGI_FORMAT_R32G32B32_FLOAT,
  [GPU_VERTEX_FORMAT_FLOAT32X4]       = DXGI_FORMAT_R32G32B32A32_FLOAT,
  [GPU_VERTEX_FORMAT_SINT32]          = DXGI_FORMAT_R32_SINT,
  [GPU_VERTEX_FORMAT_SINT32X2]        = DXGI_FORMAT_R32G32_SINT,
  [GPU_VERTEX_FORMAT_SINT32X3]        = DXGI_FORMAT_R32G32B32_SINT,
  [GPU_VERTEX_FORMAT_SINT32X4]        = DXGI_FORMAT_R32G32B32A32_SINT,
  [GPU_VERTEX_FORMAT_UINT32]          = DXGI_FORMAT_R32_UINT,
  [GPU_VERTEX_FORMAT_UINT32X2]        = DXGI_FORMAT_R32G32_UINT,
  [GPU_VERTEX_FORMAT_UINT32X3]        = DXGI_FORMAT_R32G32B32_UINT,
  [GPU_VERTEX_FORMAT_UINT32X4]        = DXGI_FORMAT_R32G32B32A32_UINT,
  [GPU_VERTEX_FORMAT_UNORM10_10_10_2] = DXGI_FORMAT_R10G10B10A2_UNORM,
  [GPU_VERTEX_FORMAT_UNORM8X4_BGRA]   = DXGI_FORMAT_B8G8R8A8_UNORM
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
                 bool            enable16BitTypes,
                 DX12ShaderCode *outCode) {
  DXCCreateInstanceFn createInstance;
  DXCCompiler3       *compiler;
  DXCResult          *result;
  DXCBlob            *blob;
  DXCBlob            *errors;
  DXCBuffer           sourceBuffer;
  wchar_t             entryWide[256];
  LPCWSTR             args[9];
  uint32_t            argCount;
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
  argCount              = 8u;
  if (enable16BitTypes) {
    args[argCount++] = L"-enable-16bit-types";
  }
  rc = compiler->lpVtbl->Compile(compiler,
                                  &sourceBuffer,
                                  args,
                                  argCount,
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
dx12_compileShader(GPUDeviceDX12        *device,
                   GPUShaderLibraryDX12 *library,
                   const char           *entry,
                   GPUShaderStageFlags   stage,
                   DX12ShaderCode       *outCode) {
  static const wchar_t *dxcProfiles60[GPU_SHADER_STAGE_COMPUTE_BIT + 1u] = {
    [GPU_SHADER_STAGE_VERTEX_BIT]   = L"vs_6_0",
    [GPU_SHADER_STAGE_FRAGMENT_BIT] = L"ps_6_0",
    [GPU_SHADER_STAGE_COMPUTE_BIT]  = L"cs_6_0"
  };
  static const wchar_t *dxcProfiles62[GPU_SHADER_STAGE_COMPUTE_BIT + 1u] = {
    [GPU_SHADER_STAGE_VERTEX_BIT]   = L"vs_6_2",
    [GPU_SHADER_STAGE_FRAGMENT_BIT] = L"ps_6_2",
    [GPU_SHADER_STAGE_COMPUTE_BIT]  = L"cs_6_2"
  };
  static const char *legacyProfiles[GPU_SHADER_STAGE_COMPUTE_BIT + 1u] = {
    [GPU_SHADER_STAGE_VERTEX_BIT]   = "vs_5_1",
    [GPU_SHADER_STAGE_FRAGMENT_BIT] = "ps_5_1",
    [GPU_SHADER_STAGE_COMPUTE_BIT]  = "cs_5_1"
  };
  const wchar_t *dxcProfile;

  if (!device || !library || !entry || !outCode ||
      stage >= GPU_ARRAY_LEN(dxcProfiles60) || !dxcProfiles60[stage] ||
      !dxcProfiles62[stage] ||
      !legacyProfiles[stage]) {
    return false;
  }

  if (device->dxcAvailable) {
    dxcProfile = device->shaderF16Enabled
                   ? dxcProfiles62[stage]
                   : dxcProfiles60[stage];
    return dx12__compileDXC(device,
                            library->source,
                            library->sourceSize,
                            entry,
                            dxcProfile,
                            device->shaderF16Enabled,
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

static D3D12_BLEND
dx12__blendFactor(GPUBlendFactor factor) {
  static const D3D12_BLEND factors[] = {
    [GPU_BLEND_FACTOR_ZERO]                = D3D12_BLEND_ZERO,
    [GPU_BLEND_FACTOR_ONE]                 = D3D12_BLEND_ONE,
    [GPU_BLEND_FACTOR_SRC_ALPHA]           = D3D12_BLEND_SRC_ALPHA,
    [GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = D3D12_BLEND_INV_SRC_ALPHA
  };

  return (uint32_t)factor < GPU_ARRAY_LEN(factors)
           ? factors[factor]
           : D3D12_BLEND_ZERO;
}

static D3D12_BLEND_OP
dx12__blendOperation(GPUBlendOp op) {
  static const D3D12_BLEND_OP operations[] = {
    [GPU_BLEND_OP_ADD]              = D3D12_BLEND_OP_ADD,
    [GPU_BLEND_OP_SUBTRACT]         = D3D12_BLEND_OP_SUBTRACT,
    [GPU_BLEND_OP_REVERSE_SUBTRACT] = D3D12_BLEND_OP_REV_SUBTRACT,
    [GPU_BLEND_OP_MIN]              = D3D12_BLEND_OP_MIN,
    [GPU_BLEND_OP_MAX]              = D3D12_BLEND_OP_MAX
  };

  return (uint32_t)op < GPU_ARRAY_LEN(operations)
           ? operations[op]
           : D3D12_BLEND_OP_ADD;
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
  GPUShaderLibraryDX12                 *library;
  GPUPipelineLayoutDX12          *layout;
  GPURenderPipelineDX12          *native;
  ID3D12RootSignature            *rootSignature;
  const GPUDepthStencilState     *depthStencil;
  D3D12_INPUT_ELEMENT_DESC       *elements;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {0};
  DX12ShaderCode                 vertexCode = {0};
  DX12ShaderCode                 fragmentCode = {0};
  DX12PipelineKey                rootKey;
  uint32_t                       elementCount;
  HRESULT                        result;

  if (!device || !device->_priv || !info || !info->library ||
      !info->layout || !pipeline) {
    return GPU_ERROR_UNSUPPORTED;
  }

  GPU__UNUSED(requiredBindGroupMask);

  elements     = NULL;
  elementCount = 0u;
  rootSignature = NULL;
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
  if (dx12_createShaderRootSignature(device,
                                     info->layout,
                                     info->library,
                                     &rootSignature,
                                     rootKey.value) != GPU_OK) {
    free(elements);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
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
    rootSignature->lpVtbl->Release(rootSignature);
    free(elements);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  desc.pRootSignature        = rootSignature;
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
    UINT8                writeMask;

    blend = &info->pColorTargets[i].blend;
    if (blend->writeMask == GPU_COLOR_WRITE_DEFAULT) {
      writeMask = GPU_COLOR_WRITE_ALL;
    } else if (blend->writeMask == GPU_COLOR_WRITE_NONE) {
      writeMask = 0u;
    } else {
      writeMask = (UINT8)blend->writeMask;
    }
    desc.RTVFormats[i] = dx12_format(info->pColorTargets[i].format);
    if (desc.RTVFormats[i] == DXGI_FORMAT_UNKNOWN) {
      result = E_INVALIDARG;
      goto done;
    }

    desc.BlendState.RenderTarget[i].BlendEnable           = blend->enabled;
    desc.BlendState.RenderTarget[i].LogicOpEnable         = FALSE;
    desc.BlendState.RenderTarget[i].SrcBlend              =
      dx12__blendFactor(blend->color.srcFactor);
    desc.BlendState.RenderTarget[i].DestBlend             =
      dx12__blendFactor(blend->color.dstFactor);
    desc.BlendState.RenderTarget[i].BlendOp               =
      dx12__blendOperation(blend->color.op);
    desc.BlendState.RenderTarget[i].SrcBlendAlpha         =
      dx12__blendFactor(blend->alpha.srcFactor);
    desc.BlendState.RenderTarget[i].DestBlendAlpha        =
      dx12__blendFactor(blend->alpha.dstFactor);
    desc.BlendState.RenderTarget[i].BlendOpAlpha          =
      dx12__blendOperation(blend->alpha.op);
    desc.BlendState.RenderTarget[i].LogicOp               = D3D12_LOGIC_OP_NOOP;
    desc.BlendState.RenderTarget[i].RenderTargetWriteMask =
      writeMask;
  }

  result = dx12_createGraphicsPSO(info->cache,
                                  deviceDX12,
                                  &desc,
                                  info,
                                  &rootKey,
                                  &native->pipelineState) == GPU_OK
             ? S_OK
             : E_FAIL;

done:
  dx12_freeShaderCode(&vertexCode);
  dx12_freeShaderCode(&fragmentCode);
  free(elements);
  if (FAILED(result)) {
    rootSignature->lpVtbl->Release(rootSignature);
    free(native);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  native->rootSignature = rootSignature;
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
