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

#ifndef gpu_assetkit_asset_h
#define gpu_assetkit_asset_h

#include <stdbool.h>
#include <stdint.h>

typedef struct AssetVertex {
  float position[3];
  float normal[3];
  float uv[2];
} AssetVertex;

typedef enum AssetTextureSlot {
  ASSET_TEXTURE_BASE_COLOR = 0,
  ASSET_TEXTURE_NORMAL,
  ASSET_TEXTURE_METALLIC_ROUGHNESS,
  ASSET_TEXTURE_OCCLUSION,
  ASSET_TEXTURE_EMISSIVE,
  ASSET_TEXTURE_COUNT
} AssetTextureSlot;

typedef enum AssetIndexType {
  ASSET_INDEX_UINT16 = 0,
  ASSET_INDEX_UINT32
} AssetIndexType;

typedef struct AssetImage {
  uint8_t *pixels;
  uint32_t width;
  uint32_t height;
} AssetImage;

typedef struct AssetMaterial {
  AssetImage images[ASSET_TEXTURE_COUNT];
  float      baseColorFactor[4];
  float      emissiveFactor[3];
  float      metallicFactor;
  float      roughnessFactor;
  float      normalScale;
  float      occlusionStrength;
  float      emissiveStrength;
} AssetMaterial;

typedef struct Asset {
  AssetVertex   *vertices;
  void          *indices;
  float          modelMatrix[16];
  float          boundsMin[3];
  float          boundsMax[3];
  AssetMaterial  material;
  uint32_t       vertexCount;
  uint32_t       indexCount;
  AssetIndexType indexType;
} Asset;

typedef void
(*AssetCallback)(Asset      *asset,
                 const char *error,
                 void       *userData);

void
asset_load(AssetCallback callback,
           void         *userData);

void
asset_release_uploads(Asset *asset);

void
asset_release(Asset *asset);

#endif
