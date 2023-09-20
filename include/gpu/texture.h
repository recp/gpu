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

#ifndef gpu_texture_h
#define gpu_texture_h
#ifdef __cplusplus
extern "C" {
#endif

typedef enum GPUTextureUsage {
  GPUTextureUsageUnknown         = 0x0000,
  GPUTextureUsageShaderRead      = 0x0001,
  GPUTextureUsageShaderWrite     = 0x0002,
  GPUTextureUsageRenderTarget    = 0x0004,
  GPUTextureUsagePixelFormatView = 0x0010,
  GPUTextureUsageShaderAtomic    = 0x0020,
} GPUTextureUsage;

typedef struct GPUTextureDesc {
  GPUPixelFormat  format;
  uint32_t        width;
  uint32_t        height;
  uint32_t        depth;            // 1
  uint32_t        mipmapLevelCount; // 1
  uint32_t        textureType;      // GPUTextureType2D
  uint32_t        sampleCount;      // 1
  uint32_t        storageMode;      // Default is MTLStorageModeManaged on macOS and MTLStorageModeShared on iOS
  GPUTextureUsage usage;
  // TODO: other properties to expose if nededed
} GPUTextureDesc;



typedef struct GPUTexture GPUTexture;

#ifdef __cplusplus
}
#endif
#endif /* gpu_texture_h */
