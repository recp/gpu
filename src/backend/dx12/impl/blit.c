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
#include "../../../api/pass/blit_internal.h"

static const char gpu_blitFloatHLSL[] =
  "struct GPUBlitVSOut { float4 position : SV_Position; };\n"
  "cbuffer GPUBlitPush : register(b0, "
    GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL ") {\n"
  "  float4 gpu_blit_src_rect;\n"
  "  float4 gpu_blit_dst_rect;\n"
  "  float4 gpu_blit_inv_src_size;\n"
  "};\n"
  "Texture2D<float4> gpu_blit_source : register(t0, space0);\n"
  "SamplerState gpu_blit_sampler : register(s0, space0);\n"
  "GPUBlitVSOut gpu_blit_vs(uint id : SV_VertexID) {\n"
  "  float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                 float2(-1.0, 3.0)};\n"
  "  GPUBlitVSOut result;\n"
  "  result.position = float4(p[id], 0.0, 1.0);\n"
  "  return result;\n"
  "}\n"
  "float4 gpu_blit_fs(GPUBlitVSOut input) : SV_Target0 {\n"
  "  float2 relative = (input.position.xy - gpu_blit_dst_rect.xy) *\n"
  "                    gpu_blit_dst_rect.zw;\n"
  "  float2 uv = (gpu_blit_src_rect.xy + relative * gpu_blit_src_rect.zw) *\n"
  "              gpu_blit_inv_src_size.xy;\n"
  "  return gpu_blit_source.SampleLevel(gpu_blit_sampler, uv, 0.0);\n"
  "}\n";

static const char gpu_blitFloatArrayHLSL[] =
  "struct GPUBlitVSOut { float4 position : SV_Position; };\n"
  "cbuffer GPUBlitPush : register(b0, "
    GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL ") {\n"
  "  float4 gpu_blit_src_rect;\n"
  "  float4 gpu_blit_dst_rect;\n"
  "  float4 gpu_blit_inv_src_size;\n"
  "};\n"
  "Texture2DArray<float4> gpu_blit_source : register(t0, space0);\n"
  "SamplerState gpu_blit_sampler : register(s0, space0);\n"
  "GPUBlitVSOut gpu_blit_vs(uint id : SV_VertexID) {\n"
  "  float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                 float2(-1.0, 3.0)};\n"
  "  GPUBlitVSOut result;\n"
  "  result.position = float4(p[id], 0.0, 1.0);\n"
  "  return result;\n"
  "}\n"
  "float4 gpu_blit_fs(GPUBlitVSOut input) : SV_Target0 {\n"
  "  float2 relative = (input.position.xy - gpu_blit_dst_rect.xy) *\n"
  "                    gpu_blit_dst_rect.zw;\n"
  "  float2 uv = (gpu_blit_src_rect.xy + relative * gpu_blit_src_rect.zw) *\n"
  "              gpu_blit_inv_src_size.xy;\n"
  "  return gpu_blit_source.SampleLevel(gpu_blit_sampler,\n"
  "                                     float3(uv, 0.0), 0.0);\n"
  "}\n";

static const char gpu_blitFloatManualHLSL[] =
  "struct GPUBlitVSOut { float4 position : SV_Position; };\n"
  "cbuffer GPUBlitPush : register(b0, "
    GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL ") {\n"
  "  float4 gpu_blit_src_rect;\n"
  "  float4 gpu_blit_dst_rect;\n"
  "  float4 gpu_blit_inv_src_size;\n"
  "};\n"
  "Texture2D<float4> gpu_blit_source : register(t0, space0);\n"
  "GPUBlitVSOut gpu_blit_vs(uint id : SV_VertexID) {\n"
  "  float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                 float2(-1.0, 3.0)};\n"
  "  GPUBlitVSOut result;\n"
  "  result.position = float4(p[id], 0.0, 1.0);\n"
  "  return result;\n"
  "}\n"
  "float4 gpu_blit_load(int2 coord, int2 size) {\n"
  "  return gpu_blit_source.Load(int3(clamp(coord, 0, size - 1), 0));\n"
  "}\n"
  "float4 gpu_blit_fs(GPUBlitVSOut input) : SV_Target0 {\n"
  "  float2 relative = (input.position.xy - gpu_blit_dst_rect.xy) *\n"
  "                    gpu_blit_dst_rect.zw;\n"
  "  float2 coord = gpu_blit_src_rect.xy +\n"
  "                 relative * gpu_blit_src_rect.zw;\n"
  "  int2 size = int2(1.0 / gpu_blit_inv_src_size.xy + 0.5);\n"
  "  if (gpu_blit_inv_src_size.z < 0.5) {\n"
  "    return gpu_blit_load(int2(floor(coord)), size);\n"
  "  }\n"
  "  float2 sample_pos = coord - 0.5;\n"
  "  float2 base_pos = floor(sample_pos);\n"
  "  int2 base = int2(base_pos);\n"
  "  float2 weight = sample_pos - base_pos;\n"
  "  float4 top = lerp(gpu_blit_load(base, size),\n"
  "                    gpu_blit_load(base + int2(1, 0), size), weight.x);\n"
  "  float4 bottom = lerp(gpu_blit_load(base + int2(0, 1), size),\n"
  "                       gpu_blit_load(base + int2(1, 1), size), weight.x);\n"
  "  return lerp(top, bottom, weight.y);\n"
  "}\n";

static const char gpu_blitFloatArrayManualHLSL[] =
  "struct GPUBlitVSOut { float4 position : SV_Position; };\n"
  "cbuffer GPUBlitPush : register(b0, "
    GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL ") {\n"
  "  float4 gpu_blit_src_rect;\n"
  "  float4 gpu_blit_dst_rect;\n"
  "  float4 gpu_blit_inv_src_size;\n"
  "};\n"
  "Texture2DArray<float4> gpu_blit_source : register(t0, space0);\n"
  "GPUBlitVSOut gpu_blit_vs(uint id : SV_VertexID) {\n"
  "  float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                 float2(-1.0, 3.0)};\n"
  "  GPUBlitVSOut result;\n"
  "  result.position = float4(p[id], 0.0, 1.0);\n"
  "  return result;\n"
  "}\n"
  "float4 gpu_blit_load(int2 coord, int2 size) {\n"
  "  return gpu_blit_source.Load(int4(clamp(coord, 0, size - 1), 0, 0));\n"
  "}\n"
  "float4 gpu_blit_fs(GPUBlitVSOut input) : SV_Target0 {\n"
  "  float2 relative = (input.position.xy - gpu_blit_dst_rect.xy) *\n"
  "                    gpu_blit_dst_rect.zw;\n"
  "  float2 coord = gpu_blit_src_rect.xy +\n"
  "                 relative * gpu_blit_src_rect.zw;\n"
  "  int2 size = int2(1.0 / gpu_blit_inv_src_size.xy + 0.5);\n"
  "  if (gpu_blit_inv_src_size.z < 0.5) {\n"
  "    return gpu_blit_load(int2(floor(coord)), size);\n"
  "  }\n"
  "  float2 sample_pos = coord - 0.5;\n"
  "  float2 base_pos = floor(sample_pos);\n"
  "  int2 base = int2(base_pos);\n"
  "  float2 weight = sample_pos - base_pos;\n"
  "  float4 top = lerp(gpu_blit_load(base, size),\n"
  "                    gpu_blit_load(base + int2(1, 0), size), weight.x);\n"
  "  float4 bottom = lerp(gpu_blit_load(base + int2(0, 1), size),\n"
  "                       gpu_blit_load(base + int2(1, 1), size), weight.x);\n"
  "  return lerp(top, bottom, weight.y);\n"
  "}\n";

static const char gpu_blitFloatUnfilterableHLSL[] =
  "struct GPUBlitVSOut { float4 position : SV_Position; };\n"
  "cbuffer GPUBlitPush : register(b0, "
    GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL ") {\n"
  "  float4 gpu_blit_src_rect;\n"
  "  float4 gpu_blit_dst_rect;\n"
  "  float4 gpu_blit_inv_src_size;\n"
  "};\n"
  "Texture2D<float4> gpu_blit_source : register(t0, space0);\n"
  "GPUBlitVSOut gpu_blit_vs(uint id : SV_VertexID) {\n"
  "  float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                 float2(-1.0, 3.0)};\n"
  "  GPUBlitVSOut result;\n"
  "  result.position = float4(p[id], 0.0, 1.0);\n"
  "  return result;\n"
  "}\n"
  "float4 gpu_blit_fs(GPUBlitVSOut input) : SV_Target0 {\n"
  "  float2 relative = (input.position.xy - gpu_blit_dst_rect.xy) *\n"
  "                    gpu_blit_dst_rect.zw;\n"
  "  int2 coord = int2(gpu_blit_src_rect.xy +\n"
  "                    relative * gpu_blit_src_rect.zw);\n"
  "  return gpu_blit_source.Load(int3(coord, 0));\n"
  "}\n";

static const char gpu_blitUintHLSL[] =
  "struct GPUBlitVSOut { float4 position : SV_Position; };\n"
  "cbuffer GPUBlitPush : register(b0, "
    GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL ") {\n"
  "  float4 gpu_blit_src_rect;\n"
  "  float4 gpu_blit_dst_rect;\n"
  "  float4 gpu_blit_inv_src_size;\n"
  "};\n"
  "Texture2D<uint4> gpu_blit_source : register(t0, space0);\n"
  "GPUBlitVSOut gpu_blit_vs(uint id : SV_VertexID) {\n"
  "  float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                 float2(-1.0, 3.0)};\n"
  "  GPUBlitVSOut result;\n"
  "  result.position = float4(p[id], 0.0, 1.0);\n"
  "  return result;\n"
  "}\n"
  "uint4 gpu_blit_fs(GPUBlitVSOut input) : SV_Target0 {\n"
  "  float2 relative = (input.position.xy - gpu_blit_dst_rect.xy) *\n"
  "                    gpu_blit_dst_rect.zw;\n"
  "  int2 coord = int2(gpu_blit_src_rect.xy +\n"
  "                    relative * gpu_blit_src_rect.zw);\n"
  "  return gpu_blit_source.Load(int3(coord, 0));\n"
  "}\n";

static const char gpu_blitSintHLSL[] =
  "struct GPUBlitVSOut { float4 position : SV_Position; };\n"
  "cbuffer GPUBlitPush : register(b0, "
    GPU_DX12_PUSH_CONSTANT_REGISTER_SPACE_HLSL ") {\n"
  "  float4 gpu_blit_src_rect;\n"
  "  float4 gpu_blit_dst_rect;\n"
  "  float4 gpu_blit_inv_src_size;\n"
  "};\n"
  "Texture2D<int4> gpu_blit_source : register(t0, space0);\n"
  "GPUBlitVSOut gpu_blit_vs(uint id : SV_VertexID) {\n"
  "  float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                 float2(-1.0, 3.0)};\n"
  "  GPUBlitVSOut result;\n"
  "  result.position = float4(p[id], 0.0, 1.0);\n"
  "  return result;\n"
  "}\n"
  "int4 gpu_blit_fs(GPUBlitVSOut input) : SV_Target0 {\n"
  "  float2 relative = (input.position.xy - gpu_blit_dst_rect.xy) *\n"
  "                    gpu_blit_dst_rect.zw;\n"
  "  int2 coord = int2(gpu_blit_src_rect.xy +\n"
  "                    relative * gpu_blit_src_rect.zw);\n"
  "  return gpu_blit_source.Load(int3(coord, 0));\n"
  "}\n";

static const GPUBlitShaderSet dx12_blitTextureShaders = {
  .filteringFloat = {
    .data = gpu_blitFloatHLSL,
    .size = sizeof(gpu_blitFloatHLSL) - 1u
  },
  .filteringFloatArray = {
    .data = gpu_blitFloatArrayHLSL,
    .size = sizeof(gpu_blitFloatArrayHLSL) - 1u
  },
  .unfilterableFloat = {
    .data = gpu_blitFloatUnfilterableHLSL,
    .size = sizeof(gpu_blitFloatUnfilterableHLSL) - 1u
  },
  .unsignedInteger = {
    .data = gpu_blitUintHLSL,
    .size = sizeof(gpu_blitUintHLSL) - 1u
  },
  .signedInteger = {
    .data = gpu_blitSintHLSL,
    .size = sizeof(gpu_blitSintHLSL) - 1u
  }
};

static const GPUBlitShaderSet dx12_blitTextureManualShaders = {
  .filteringFloat = {
    .data = gpu_blitFloatManualHLSL,
    .size = sizeof(gpu_blitFloatManualHLSL) - 1u
  },
  .filteringFloatArray = {
    .data = gpu_blitFloatArrayManualHLSL,
    .size = sizeof(gpu_blitFloatArrayManualHLSL) - 1u
  },
  .unfilterableFloat = {
    .data = gpu_blitFloatUnfilterableHLSL,
    .size = sizeof(gpu_blitFloatUnfilterableHLSL) - 1u
  },
  .unsignedInteger = {
    .data = gpu_blitUintHLSL,
    .size = sizeof(gpu_blitUintHLSL) - 1u
  },
  .signedInteger = {
    .data = gpu_blitSintHLSL,
    .size = sizeof(gpu_blitSintHLSL) - 1u
  }
};

GPU_HIDE
void
dx12_blitTexture(GPUCommandBuffer         *cmdb,
                 const GPUTextureBlitInfo *info) {
  GPUDevice     *gpuDevice;
  GPUDeviceDX12 *device;

  gpuDevice = gpuCommandBufferDevice(cmdb);
  device    = gpuDevice ? gpuDevice->_priv : NULL;
  gpuBlitTextureRenderFallback(
    cmdb,
    info,
    device && device->manualBlitFiltering
      ? &dx12_blitTextureManualShaders
      : &dx12_blitTextureShaders
  );
}
