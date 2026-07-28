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

static const char gpu_blitFloatWGSL[] =
  "struct GPUBlitParams {\n"
  "  srcRect: vec4<f32>,\n"
  "  dstRect: vec4<f32>,\n"
  "  invSrcSize: vec4<f32>,\n"
  "}\n"
  "@group(0) @binding(0) var gpuBlitSource: texture_2d<f32>;\n"
  "@group(0) @binding(1) var gpuBlitSampler: sampler;\n"
  "@group(3) @binding(0) var<uniform> gpuBlitParams: GPUBlitParams;\n"
  "@vertex\n"
  "fn gpu_blit_vs(@builtin(vertex_index) id: u32) ->\n"
  "  @builtin(position) vec4<f32> {\n"
  "  var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0),\n"
  "                                vec2<f32>(3.0, -1.0),\n"
  "                                vec2<f32>(-1.0, 3.0));\n"
  "  return vec4<f32>(p[id], 0.0, 1.0);\n"
  "}\n"
  "@fragment\n"
  "fn gpu_blit_fs(@builtin(position) position: vec4<f32>) ->\n"
  "  @location(0) vec4<f32> {\n"
  "  let relative = (position.xy - gpuBlitParams.dstRect.xy) *\n"
  "                 gpuBlitParams.dstRect.zw;\n"
  "  let uv = (gpuBlitParams.srcRect.xy +\n"
  "            relative * gpuBlitParams.srcRect.zw) *\n"
  "           gpuBlitParams.invSrcSize.xy;\n"
  "  return textureSampleLevel(gpuBlitSource, gpuBlitSampler, uv, 0.0);\n"
  "}\n";

static const char gpu_blitFloatUnfilterableWGSL[] =
  "struct GPUBlitParams {\n"
  "  srcRect: vec4<f32>,\n"
  "  dstRect: vec4<f32>,\n"
  "  invSrcSize: vec4<f32>,\n"
  "}\n"
  "@group(0) @binding(0) var gpuBlitSource: texture_2d<f32>;\n"
  "@group(0) @binding(1) var gpuBlitSampler: sampler;\n"
  "@group(3) @binding(0) var<uniform> gpuBlitParams: GPUBlitParams;\n"
  "@vertex\n"
  "fn gpu_blit_vs(@builtin(vertex_index) id: u32) ->\n"
  "  @builtin(position) vec4<f32> {\n"
  "  var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0),\n"
  "                                vec2<f32>(3.0, -1.0),\n"
  "                                vec2<f32>(-1.0, 3.0));\n"
  "  return vec4<f32>(p[id], 0.0, 1.0);\n"
  "}\n"
  "@fragment\n"
  "fn gpu_blit_fs(@builtin(position) position: vec4<f32>) ->\n"
  "  @location(0) vec4<f32> {\n"
  "  let relative = (position.xy - gpuBlitParams.dstRect.xy) *\n"
  "                 gpuBlitParams.dstRect.zw;\n"
  "  let coord = vec2<i32>(gpuBlitParams.srcRect.xy +\n"
  "                        relative * gpuBlitParams.srcRect.zw);\n"
  "  return textureLoad(gpuBlitSource, coord, 0);\n"
  "}\n";

static const char gpu_blitUintWGSL[] =
  "struct GPUBlitParams {\n"
  "  srcRect: vec4<f32>,\n"
  "  dstRect: vec4<f32>,\n"
  "  invSrcSize: vec4<f32>,\n"
  "}\n"
  "@group(0) @binding(0) var gpuBlitSource: texture_2d<u32>;\n"
  "@group(0) @binding(1) var gpuBlitSampler: sampler;\n"
  "@group(3) @binding(0) var<uniform> gpuBlitParams: GPUBlitParams;\n"
  "@vertex\n"
  "fn gpu_blit_vs(@builtin(vertex_index) id: u32) ->\n"
  "  @builtin(position) vec4<f32> {\n"
  "  var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0),\n"
  "                                vec2<f32>(3.0, -1.0),\n"
  "                                vec2<f32>(-1.0, 3.0));\n"
  "  return vec4<f32>(p[id], 0.0, 1.0);\n"
  "}\n"
  "@fragment\n"
  "fn gpu_blit_fs(@builtin(position) position: vec4<f32>) ->\n"
  "  @location(0) vec4<u32> {\n"
  "  let relative = (position.xy - gpuBlitParams.dstRect.xy) *\n"
  "                 gpuBlitParams.dstRect.zw;\n"
  "  let coord = vec2<i32>(gpuBlitParams.srcRect.xy +\n"
  "                        relative * gpuBlitParams.srcRect.zw);\n"
  "  return textureLoad(gpuBlitSource, coord, 0);\n"
  "}\n";

static const char gpu_blitSintWGSL[] =
  "struct GPUBlitParams {\n"
  "  srcRect: vec4<f32>,\n"
  "  dstRect: vec4<f32>,\n"
  "  invSrcSize: vec4<f32>,\n"
  "}\n"
  "@group(0) @binding(0) var gpuBlitSource: texture_2d<i32>;\n"
  "@group(0) @binding(1) var gpuBlitSampler: sampler;\n"
  "@group(3) @binding(0) var<uniform> gpuBlitParams: GPUBlitParams;\n"
  "@vertex\n"
  "fn gpu_blit_vs(@builtin(vertex_index) id: u32) ->\n"
  "  @builtin(position) vec4<f32> {\n"
  "  var p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0),\n"
  "                                vec2<f32>(3.0, -1.0),\n"
  "                                vec2<f32>(-1.0, 3.0));\n"
  "  return vec4<f32>(p[id], 0.0, 1.0);\n"
  "}\n"
  "@fragment\n"
  "fn gpu_blit_fs(@builtin(position) position: vec4<f32>) ->\n"
  "  @location(0) vec4<i32> {\n"
  "  let relative = (position.xy - gpuBlitParams.dstRect.xy) *\n"
  "                 gpuBlitParams.dstRect.zw;\n"
  "  let coord = vec2<i32>(gpuBlitParams.srcRect.xy +\n"
  "                        relative * gpuBlitParams.srcRect.zw);\n"
  "  return textureLoad(gpuBlitSource, coord, 0);\n"
  "}\n";

static const GPUBlitShaderSet webgpu_blitTextureShaders = {
  .filteringFloat = {
    .data = gpu_blitFloatWGSL,
    .size = sizeof(gpu_blitFloatWGSL) - 1u
  },
  .unfilterableFloat = {
    .data = gpu_blitFloatUnfilterableWGSL,
    .size = sizeof(gpu_blitFloatUnfilterableWGSL) - 1u
  },
  .unsignedInteger = {
    .data = gpu_blitUintWGSL,
    .size = sizeof(gpu_blitUintWGSL) - 1u
  },
  .signedInteger = {
    .data = gpu_blitSintWGSL,
    .size = sizeof(gpu_blitSintWGSL) - 1u
  }
};

GPU_HIDE
void
webgpu_blitTexture(GPUCommandBuffer         *cmdb,
                   const GPUTextureBlitInfo *info) {
  gpuBlitTextureRenderFallback(cmdb, info, &webgpu_blitTextureShaders);
}
