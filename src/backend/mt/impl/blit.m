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

static const char gpu_blitFloatMSL[] =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct GPUBlitParams {\n"
  "  float4 srcRect;\n"
  "  float4 dstRect;\n"
  "  float4 invSrcSize;\n"
  "};\n"
  "vertex float4 gpu_blit_vs(uint id [[vertex_id]]) {\n"
  "  const float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                       float2(-1.0, 3.0)};\n"
  "  return float4(p[id], 0.0, 1.0);\n"
  "}\n"
  "fragment float4 gpu_blit_fs(\n"
  "  float4 position [[position]],\n"
  "  texture2d<float> source [[texture(0)]],\n"
  "  sampler sourceSampler [[sampler(0)]],\n"
  "  constant GPUBlitParams& params [[buffer(30)]]) {\n"
  "  float2 relative = (position.xy - params.dstRect.xy) *\n"
  "                    params.dstRect.zw;\n"
  "  float2 uv = (params.srcRect.xy + relative * params.srcRect.zw) *\n"
  "              params.invSrcSize.xy;\n"
  "  return source.sample(sourceSampler, uv, level(0.0));\n"
  "}\n";

static const char gpu_blitFloatArrayMSL[] =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct GPUBlitParams {\n"
  "  float4 srcRect;\n"
  "  float4 dstRect;\n"
  "  float4 invSrcSize;\n"
  "};\n"
  "vertex float4 gpu_blit_vs(uint id [[vertex_id]]) {\n"
  "  const float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                       float2(-1.0, 3.0)};\n"
  "  return float4(p[id], 0.0, 1.0);\n"
  "}\n"
  "fragment float4 gpu_blit_fs(\n"
  "  float4 position [[position]],\n"
  "  texture2d_array<float> source [[texture(0)]],\n"
  "  sampler sourceSampler [[sampler(0)]],\n"
  "  constant GPUBlitParams& params [[buffer(30)]]) {\n"
  "  float2 relative = (position.xy - params.dstRect.xy) *\n"
  "                    params.dstRect.zw;\n"
  "  float2 uv = (params.srcRect.xy + relative * params.srcRect.zw) *\n"
  "              params.invSrcSize.xy;\n"
  "  return source.sample(sourceSampler, uv, 0u, level(0.0));\n"
  "}\n";

static const char gpu_blitFloatUnfilterableMSL[] =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct GPUBlitParams {\n"
  "  float4 srcRect;\n"
  "  float4 dstRect;\n"
  "  float4 invSrcSize;\n"
  "};\n"
  "vertex float4 gpu_blit_vs(uint id [[vertex_id]]) {\n"
  "  const float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                       float2(-1.0, 3.0)};\n"
  "  return float4(p[id], 0.0, 1.0);\n"
  "}\n"
  "fragment float4 gpu_blit_fs(\n"
  "  float4 position [[position]],\n"
  "  texture2d<float> source [[texture(0)]],\n"
  "  constant GPUBlitParams& params [[buffer(30)]]) {\n"
  "  float2 relative = (position.xy - params.dstRect.xy) *\n"
  "                    params.dstRect.zw;\n"
  "  uint2 coord = uint2(params.srcRect.xy + relative * params.srcRect.zw);\n"
  "  return source.read(coord);\n"
  "}\n";

static const char gpu_blitUintMSL[] =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct GPUBlitParams {\n"
  "  float4 srcRect;\n"
  "  float4 dstRect;\n"
  "  float4 invSrcSize;\n"
  "};\n"
  "vertex float4 gpu_blit_vs(uint id [[vertex_id]]) {\n"
  "  const float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                       float2(-1.0, 3.0)};\n"
  "  return float4(p[id], 0.0, 1.0);\n"
  "}\n"
  "fragment uint4 gpu_blit_fs(\n"
  "  float4 position [[position]],\n"
  "  texture2d<uint> source [[texture(0)]],\n"
  "  constant GPUBlitParams& params [[buffer(30)]]) {\n"
  "  float2 relative = (position.xy - params.dstRect.xy) *\n"
  "                    params.dstRect.zw;\n"
  "  uint2 coord = uint2(params.srcRect.xy + relative * params.srcRect.zw);\n"
  "  return source.read(coord);\n"
  "}\n";

static const char gpu_blitSintMSL[] =
  "#include <metal_stdlib>\n"
  "using namespace metal;\n"
  "struct GPUBlitParams {\n"
  "  float4 srcRect;\n"
  "  float4 dstRect;\n"
  "  float4 invSrcSize;\n"
  "};\n"
  "vertex float4 gpu_blit_vs(uint id [[vertex_id]]) {\n"
  "  const float2 p[3] = {float2(-1.0, -1.0), float2(3.0, -1.0),\n"
  "                       float2(-1.0, 3.0)};\n"
  "  return float4(p[id], 0.0, 1.0);\n"
  "}\n"
  "fragment int4 gpu_blit_fs(\n"
  "  float4 position [[position]],\n"
  "  texture2d<int> source [[texture(0)]],\n"
  "  constant GPUBlitParams& params [[buffer(30)]]) {\n"
  "  float2 relative = (position.xy - params.dstRect.xy) *\n"
  "                    params.dstRect.zw;\n"
  "  uint2 coord = uint2(params.srcRect.xy + relative * params.srcRect.zw);\n"
  "  return source.read(coord);\n"
  "}\n";

static const GPUBlitShaderSet mt_blitTextureShaders = {
  .filteringFloat = {
    .data = gpu_blitFloatMSL,
    .size = sizeof(gpu_blitFloatMSL) - 1u
  },
  .filteringFloatArray = {
    .data = gpu_blitFloatArrayMSL,
    .size = sizeof(gpu_blitFloatArrayMSL) - 1u
  },
  .unfilterableFloat = {
    .data = gpu_blitFloatUnfilterableMSL,
    .size = sizeof(gpu_blitFloatUnfilterableMSL) - 1u
  },
  .unsignedInteger = {
    .data = gpu_blitUintMSL,
    .size = sizeof(gpu_blitUintMSL) - 1u
  },
  .signedInteger = {
    .data = gpu_blitSintMSL,
    .size = sizeof(gpu_blitSintMSL) - 1u
  }
};

GPU_HIDE
void
mt_blitTexture(GPUCommandBuffer         *cmdb,
               const GPUTextureBlitInfo *info) {
  gpuBlitTextureRenderFallback(cmdb, info, &mt_blitTextureShaders);
}
