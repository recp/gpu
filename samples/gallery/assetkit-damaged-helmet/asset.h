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
asset_load(AssetCallback  callback,
           void          *userData);

void
asset_release_uploads(Asset *asset);

void
asset_release(Asset *asset);

#endif
