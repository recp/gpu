#include "asset.h"

#include <ak/assetkit.h>

#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>

#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define ASSET_URL \
  "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/" \
  "main/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb"

#define ASSET_LOCAL_PATH "/tmp/DamagedHelmet.glb"

typedef struct AssetLoadJob {
  AssetCallback callback;
  void         *userData;
  AkDoc        *doc;
  Asset         asset;
  uint32_t      pendingImages;
  bool          active;
} AssetLoadJob;

static AssetLoadJob loadJob;

EM_JS(void,
      asset_decode_image,
      (uint32_t slot, const uint8_t *bytes, uint32_t byteCount), {
  const encoded = HEAPU8.slice(bytes, bytes + byteCount);
  const blob    = new Blob([encoded]);

  createImageBitmap(blob, {colorSpaceConversion: "none", premultiplyAlpha: "none"}).then((bitmap) => {
    const canvas  = document.createElement("canvas");
    const context = canvas.getContext("2d", {
      alpha: true,
      colorSpace: "srgb",
      willReadFrequently: true
    });

    canvas.width  = bitmap.width;
    canvas.height = bitmap.height;
    context.drawImage(bitmap, 0, 0);
    const image  = context.getImageData(0, 0, bitmap.width, bitmap.height);
    const pixels = _malloc(image.data.byteLength);

    HEAPU8.set(image.data, pixels);
    bitmap.close();

    _asset_image_ready(slot, pixels, canvas.width, canvas.height);
  }).catch(() => {
    _asset_image_ready(slot, 0, 0, 0);
  });
});

static void
asset_reset(Asset *asset) {
  memset(asset, 0, sizeof(*asset));

  asset->modelMatrix[0]  = 1.0f;
  asset->modelMatrix[5]  = 1.0f;
  asset->modelMatrix[10] = 1.0f;
  asset->modelMatrix[15] = 1.0f;

  for (uint32_t i = 0u; i < 3u; i++) {
    asset->boundsMin[i] = FLT_MAX;
    asset->boundsMax[i] = -FLT_MAX;
  }

  for (uint32_t i = 0u; i < 4u; i++) {
    asset->material.baseColorFactor[i] = 1.0f;
  }

  asset->material.metallicFactor    = 1.0f;
  asset->material.roughnessFactor   = 1.0f;
  asset->material.normalScale       = 1.0f;
  asset->material.occlusionStrength = 1.0f;
  asset->material.emissiveStrength  = 1.0f;
}

void
asset_release_uploads(Asset *asset) {
  if (!asset)
    return;

  free(asset->vertices);
  free(asset->indices);

  asset->vertices = NULL;
  asset->indices  = NULL;

  for (uint32_t i = 0u; i < ASSET_TEXTURE_COUNT; i++) {
    free(asset->material.images[i].pixels);
    asset->material.images[i].pixels = NULL;
  }
}

void
asset_release(Asset *asset) {
  if (!asset)
    return;

  asset_release_uploads(asset);
  asset_reset(asset);
}

static void
asset_job_cleanup(void) {
  if (loadJob.doc) {
    ak_free(loadJob.doc);
    loadJob.doc = NULL;
  }
  remove(ASSET_LOCAL_PATH);
}

static void
asset_fail(const char *message) {
  AssetCallback callback;
  void         *userData;

  if (!loadJob.active) {
    return;
  }

  callback         = loadJob.callback;
  userData         = loadJob.userData;
  loadJob.active   = false;

  asset_job_cleanup();
  asset_release(&loadJob.asset);

  callback(NULL, message, userData);
}

static AkInput *
asset_input(AkMeshPrimitive *prim,
             AkInputSemantic  semantic,
             uint32_t         index) {
  AkInput *input;

  for (input = prim ? prim->input : NULL;
       input;
       input = input->next) {
    if (input->semantic == semantic && input->index == index)
      return input;
  }

  if (prim && prim->pos &&
      prim->pos->semantic == semantic &&
      prim->pos->index == index) {
    return prim->pos;
  }

  return NULL;
}

static uint32_t
asset_read_index(const AkAccessor *acc,
                  uint32_t          index) {
  const uint8_t *bytes;
  size_t         stride;

  stride = acc->byteStride ? acc->byteStride : acc->bytesPerComponent;
  bytes  = (const uint8_t *)acc->buffer->data + acc->byteOffset + (size_t)index * stride;

  switch (acc->componentType) {
    case AKT_UBYTE:  return *bytes;
    case AKT_USHORT: {
      uint16_t value;
      memcpy(&value, bytes, sizeof(value));
      return value;
    }
    case AKT_UINT: {
      uint32_t value;
      memcpy(&value, bytes, sizeof(value));
      return value;
    }
    default:
      return UINT32_MAX;
  }
}

static AkNode *
asset_geometry_node(AkNode *node) {
  AkNode *found;

  for (; node; node = node->next) {
    if (node->geometry)
      return node;

    if ((found = asset_geometry_node(node->chld)))
      return found;
  }
  return NULL;
}

static int
asset_extract_geometry(AkDoc            *doc,
                        AkGeometry       *geometry,
                        AkMeshPrimitive **outPrim) {
  AkAccessor      *posAcc, *normAcc, *uvAcc;
  AkAccessor      *idxAcc;
  AkInput         *posInp, *normInp, *uvInput;
  AkMesh          *mesh;
  AkMeshPrimitive *prim;
  AssetVertex     *vertices;
  float           *attributes, *positions, *normals, *uvs;
  uint32_t         maxIndex;
  size_t           floatCount;

  if (!geometry || !geometry->gdata ||
      geometry->gdata->type != AK_GEOMETRY_MESH)
    return 0;

  mesh = ak_objGet(geometry->gdata);
  prim = mesh ? mesh->primitive : NULL;

  if ((!prim || prim->type != AK_PRIMITIVE_TRIANGLES)
       || !(posInp  = asset_input(prim, AK_INPUT_POSITION, 0u))
       || !(normInp = asset_input(prim, AK_INPUT_NORMAL,   0u))
       || !(uvInput = asset_input(prim, AK_INPUT_TEXCOORD, 0u))
       || !(posAcc  = posInp->accessor)
       || !(normAcc = normInp->accessor)
       || !(uvAcc   = uvInput->accessor)
       || posAcc->count  == 0u
       || normAcc->count != posAcc->count
       || uvAcc->count   != posAcc->count
       || posAcc->componentCount  != 3u
       || normAcc->componentCount != 3u
       || uvAcc->componentCount   != 2u) {
    return 0;
  }

  floatCount = (size_t)posAcc->count * 8u;
  attributes = malloc(floatCount * sizeof(float));
  vertices   = malloc((size_t)posAcc->count * sizeof(*vertices));
  if (!attributes || !vertices) {
    free(attributes);
    free(vertices);
    return 0;
  }

  positions = attributes;
  normals   = positions + (size_t)posAcc->count * 3u;
  uvs       = normals + (size_t)posAcc->count   * 3u;

  if (ak_accessorAsFloat(posAcc, positions, (size_t)posAcc->count * 3u) == 0u
      || ak_accessorAsFloat(normAcc, normals, (size_t)normAcc->count * 3u) == 0u
      || ak_accessorAsFloat(uvAcc, uvs, (size_t)uvAcc->count * 2u) == 0u) {
    free(attributes);
    free(vertices);
    return 0;
  }

  for (uint32_t i = 0u; i < posAcc->count; i++) {
    memcpy(vertices[i].position, positions + (size_t)i * 3u, sizeof(vertices[i].position));
    memcpy(vertices[i].normal,   normals   + (size_t)i * 3u, sizeof(vertices[i].normal));
    memcpy(vertices[i].uv,       uvs       + (size_t)i * 2u, sizeof(vertices[i].uv));

    for (uint32_t axis = 0u; axis < 3u; axis++) {
      float value;

      value = vertices[i].position[axis];
      if (value < loadJob.asset.boundsMin[axis]) {
        loadJob.asset.boundsMin[axis] = value;
      }

      if (value > loadJob.asset.boundsMax[axis]) {
        loadJob.asset.boundsMax[axis] = value;
      }
    }
  }

  free(attributes);

  idxAcc = ak_meshPrimitiveIndexAccessor(prim);
  if (!idxAcc || !idxAcc->buffer ||
      idxAcc->count == 0u) {
    free(vertices);
    return 0;
  }

  maxIndex = 0u;
  for (uint32_t i = 0u; i < idxAcc->count; i++) {
    uint32_t value;

    value = asset_read_index(idxAcc, i);
    if (value == UINT32_MAX) {
      free(vertices);
      return 0;
    }

    if (value > maxIndex) {
      maxIndex = value;
    }
  }

  if (maxIndex <= UINT16_MAX) {
    uint16_t *indices;

    indices = malloc((size_t)idxAcc->count * sizeof(*indices));
    if (!indices) {
      free(vertices);
      return 0;
    }

    for (uint32_t i = 0u; i < idxAcc->count; i++) {
      indices[i] = (uint16_t)asset_read_index(idxAcc, i);
    }

    loadJob.asset.indices   = indices;
    loadJob.asset.indexType = ASSET_INDEX_UINT16;
  } else {
    uint32_t *indices;

    indices = malloc((size_t)idxAcc->count * sizeof(*indices));
    if (!indices) {
      free(vertices);
      return 0;
    }

    for (uint32_t i = 0u; i < idxAcc->count; i++) {
      indices[i] = asset_read_index(idxAcc, i);
    }

    loadJob.asset.indices   = indices;
    loadJob.asset.indexType = ASSET_INDEX_UINT32;
  }


  loadJob.asset.vertices    = vertices;
  loadJob.asset.vertexCount = posAcc->count;
  loadJob.asset.indexCount  = idxAcc->count;
  *outPrim = prim;

  if (doc->scene && doc->scene->node) {
    AkNode *node;

    node = asset_geometry_node(doc->scene->node->chld);
    if (node) {
      ak_transformCombineWorld(node, loadJob.asset.modelMatrix);
    }
  }

  return 1;
}

static void
asset_copy_factor(float                 *out,
                   uint32_t               count,
                   const AkMaterialInput *input,
                   float                  fallback) {
  for (uint32_t i = 0u; i < count; i++) {
    out[i] = fallback;
  }

  if (!input)
    return;

  switch (input->valueType) {
    case AK_MATERIAL_VALUE_COLOR:
    case AK_MATERIAL_VALUE_FLOAT4:
      memcpy(out, input->value, sizeof(float) * count);
      break;
    case AK_MATERIAL_VALUE_FLOAT3:
      memcpy(out, input->value, sizeof(float) * (count < 3u ? count : 3u));
      break;
    case AK_MATERIAL_VALUE_FLOAT2:
      memcpy(out, input->value, sizeof(float) * (count < 2u ? count : 2u));
      break;
    case AK_MATERIAL_VALUE_FLOAT:
      out[0] = input->value[0];
      break;
    default:
      break;
  }
}

static AkImageSource *
asset_image_source(const AkMaterialInput *input) {
  AkTextureRef *reference;
  AkTexture    *texture;
  AkImage      *image;

  reference = ak_materialInputTexture(input);
  texture   = reference ? reference->texture : NULL;
  image     = texture ? texture->image : NULL;
  return image ? image->source : NULL;
}

static void
asset_finish_if_ready(void) {
  AssetCallback callback;
  void         *userData;

  if (!loadJob.active || loadJob.pendingImages != 0u)
    return;

  callback         = loadJob.callback;
  userData         = loadJob.userData;
  loadJob.active   = false;

  asset_job_cleanup();
  callback(&loadJob.asset, NULL, userData);
}

EMSCRIPTEN_KEEPALIVE
void
asset_image_ready(uint32_t slot,
                  uint8_t *pixels,
                  uint32_t width,
                  uint32_t height) {
  AssetImage *image;

  if (!loadJob.active || slot >= ASSET_TEXTURE_COUNT) {
    free(pixels);
    return;
  }

  if (!pixels || width == 0u || height == 0u) {
    free(pixels);
    asset_fail("AssetKit: failed to decode an embedded glTF image");
    return;
  }

  image         = &loadJob.asset.material.images[slot];
  image->pixels = pixels;
  image->width  = width;
  image->height = height;

  loadJob.pendingImages--;
  asset_finish_if_ready();
}

static int
asset_extract_material(AkMeshPrimitive *prim) {
  static const AkMaterialSemantic semantics[ASSET_TEXTURE_COUNT] = {
    AK_MATERIAL_SEMANTIC_BASE_COLOR,
    AK_MATERIAL_SEMANTIC_NORMAL,
    AK_MATERIAL_SEMANTIC_METALLIC,
    AK_MATERIAL_SEMANTIC_OCCLUSION,
    AK_MATERIAL_SEMANTIC_EMISSIVE
  };

  AkResolvedMaterial resolved = {0};
  AssetMaterial     *mat;
  AkMaterialSurface *surface;

  if (!ak_materialResolveForPrimitive(prim, UINT32_MAX, &resolved) ||
      !resolved.surface) {
    return 0;
  }

  mat     = &loadJob.asset.material;
  surface = resolved.surface;

  asset_copy_factor(mat->baseColorFactor, 4u, surface->baseColor, 1.0f);
  asset_copy_factor(mat->emissiveFactor,  3u, surface->emissive,  0.0f);

  mat->metallicFactor    = ak_materialInputScalar(surface->metallic, 1.0f);
  mat->roughnessFactor   = ak_materialInputScalar(surface->roughness, 1.0f);
  mat->normalScale       = ak_materialNormalScale(surface);
  mat->occlusionStrength = ak_materialOcclusionStrength(surface);
  mat->emissiveStrength  = ak_materialEmissiveStrength(surface);

  for (uint32_t i = 0u; i < ASSET_TEXTURE_COUNT; i++) {
    const AkMaterialInput *input;
    AkImageSource         *source;

    input  = ak_materialInputBySemantic(surface, semantics[i]);
    source = asset_image_source(input);

    if (!source || source->type != AK_IMAGE_SOURCE_BUFFER ||
        !source->buffer || !source->buffer->data ||
        source->buffer->length == 0u ||
        source->buffer->length > UINT32_MAX) {
      return 0;
    }

    loadJob.pendingImages++;
    asset_decode_image(i, source->buffer->data, (uint32_t)source->buffer->length);
  }
  return 1;
}

static int
asset_write_file(const void *bytes, size_t byteCount) {
  FILE  *file;
  size_t written;

  if (mkdir("/tmp", 0777) != 0 && errno != EEXIST)
    return 0;

  file = fopen(ASSET_LOCAL_PATH, "wb");
  if (!file)
    return 0;

  written = fwrite(bytes, 1u, byteCount, file);
  if (fclose(file) != 0)
    return 0;

  return written == byteCount;
}

static void
asset_fetch_success(emscripten_fetch_t *fetch) {
  AkGeometry      *geom;
  AkMeshPrimitive *prim;

  if (!loadJob.active) {
    emscripten_fetch_close(fetch);
    return;
  }

  if (!asset_write_file(fetch->data, fetch->numBytes)) {
    emscripten_fetch_close(fetch);
    asset_fail("AssetKit: failed to stage the downloaded GLB");
    return;
  }

  emscripten_fetch_close(fetch);

  if (ak_load(&loadJob.doc, ASSET_LOCAL_PATH, AK_FILE_TYPE_GLB) != AK_OK || !loadJob.doc) {
    asset_fail("AssetKit: failed to parse DamagedHelmet.glb");
    return;
  }

  geom = loadJob.doc->lib.geometries.first;
  prim = NULL;

  if (!asset_extract_geometry(loadJob.doc, geom, &prim)) {
    asset_fail("AssetKit: DamagedHelmet geometry is incomplete");
    return;
  }

  if (!asset_extract_material(prim)) {
    asset_fail("AssetKit: DamagedHelmet PBR material is incomplete");
    return;
  }

  asset_finish_if_ready();
}

static void
asset_fetch_error(emscripten_fetch_t *fetch) {
  emscripten_fetch_close(fetch);
  asset_fail("AssetKit: failed to download DamagedHelmet from Khronos");
}

void
asset_load(AssetCallback callback,
           void         *userData) {
  emscripten_fetch_attr_t attributes;

  if (!callback || loadJob.active) {
    if (callback) {
      callback(NULL, "AssetKit: an asset load is already active", userData);
    }
    return;
  }

  memset(&loadJob, 0, sizeof(loadJob));
  asset_reset(&loadJob.asset);
  loadJob.callback = callback;
  loadJob.userData = userData;
  loadJob.active   = true;

  emscripten_fetch_attr_init(&attributes);
  strcpy(attributes.requestMethod, "GET");
  attributes.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
  attributes.onsuccess  = asset_fetch_success;
  attributes.onerror    = asset_fetch_error;
  emscripten_fetch(&attributes, ASSET_URL);
}
