#include "../common/webgpu.h"
#include "asset.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#include <math.h>
#include <string.h>

typedef struct PBRUniforms {
  mat4 mvp;
  mat4 model;
  vec4 cameraPosition;
  vec4 lightDirection;
  vec4 baseColorFactor;
  vec4 emissiveFactor;
  vec4 materialFactors;
} PBRUniforms;

typedef struct AssetSample {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *vertexBuffer;
  GPUBuffer         *indexBuffer;
  GPUBuffer         *uniformBuffer;
  GPUTexture        *materialTextures[ASSET_TEXTURE_COUNT];
  GPUTextureView    *materialViews[ASSET_TEXTURE_COUNT];
  GPUTexture        *diffuseEnvironmentTexture;
  GPUTextureView    *diffuseEnvironmentView;
  GPUTexture        *specularEnvironmentTexture;
  GPUTextureView    *specularEnvironmentView;
  GPUTexture        *ggxLUTTexture;
  GPUTextureView    *ggxLUTView;
  GPUTexture        *depthTexture;
  GPUTextureView    *depthView;
  GPUSampler        *sampler;
  GPUBindGroup      *frameGroup;
  GPUBindGroup      *materialGroup;
  WebGPURequest      request;
  Asset              asset;
  mat4               viewProjection;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} AssetSample;

enum {
  PBR_DIFFUSE_ENV_SIZE   = 32u,
  PBR_SPECULAR_ENV_SIZE  = 64u,
  PBR_SPECULAR_ENV_MIPS  = 7u,
  PBR_RGBA16_FLOAT_BYTES = 8u,
  PBR_CUBE_FACE_COUNT    = 6u,
  WARM_FRAME_COUNT       = 8u
};

_Static_assert(sizeof(PBRUniforms) == 208u,
               "PBR uniforms must match the reflected USL layout");

static AssetSample app;

static void
build_view_projection(AssetSample *state) {
  vec3 eye = {0.0f, 0.0f, 3.35f}, center = {0.0f, 0.0f, 0.0f}, up = {0.0f, 1.0f, 0.0f};
  mat4 view, projection;

  glm_lookat(eye, center, up, view);
  glm_perspective(glm_rad(44.0f), 
                  state->height > 0u ? (float)state->width/(float)state->height : 1.0f, 
                  0.1f, 
                  100.0f, 
                  projection);

  glm_mat4_mul(projection, view, state->viewProjection);
}

static void
build_uniforms(AssetSample *state, float seconds, PBRUniforms *uniforms) {
  mat4  authored, centered;
  vec4  camera = {0.0f, 0.0f, 3.35f, 1.0f}, light = {0.44f, 0.78f, 0.54f, 0.0f};
  vec3  axisY  = {0.0f, 1.0f, 0.0f};
  vec3  center, extent;
  float radius, scale;

  glm_mat4_make(state->asset.modelMatrix, authored);

  for (uint32_t i = 0u; i < 3u; i++) {
    center[i] = (state->asset.boundsMin[i] + state->asset.boundsMax[i]) * 0.5f;
    extent[i] = state->asset.boundsMax[i]  - state->asset.boundsMin[i];
  }

  radius = glm_vec3_norm(extent) * 0.5f;
  scale  = radius > 0.0f ? 1.45f / radius : 1.0f;

  glm_mat4_identity(uniforms->model);
  glm_rotate(uniforms->model, seconds * 0.32f, axisY);
  glm_scale_uni(uniforms->model, scale);
  glm_mat4_mul(uniforms->model, authored, centered);

  center[0] = -center[0];
  center[1] = -center[1];
  center[2] = -center[2];

  glm_translate(centered, center);
  glm_mat4_copy(centered, uniforms->model);
  glm_mat4_mul(state->viewProjection, uniforms->model, uniforms->mvp);
  glm_vec4_copy(camera, uniforms->cameraPosition);
  glm_vec4_normalize_to(light, uniforms->lightDirection);
  glm_vec4_copy(state->asset.material.baseColorFactor, uniforms->baseColorFactor);

  uniforms->emissiveFactor[0]  = state->asset.material.emissiveFactor[0];
  uniforms->emissiveFactor[1]  = state->asset.material.emissiveFactor[1];
  uniforms->emissiveFactor[2]  = state->asset.material.emissiveFactor[2];
  uniforms->emissiveFactor[3]  = state->asset.material.emissiveStrength;
  uniforms->materialFactors[0] = state->asset.material.metallicFactor;
  uniforms->materialFactors[1] = state->asset.material.roughnessFactor;
  uniforms->materialFactors[2] = state->asset.material.normalScale;
  uniforms->materialFactors[3] = state->asset.material.occlusionStrength;
}

static int
create_depth_target(AssetSample *state,
                    uint32_t     width,
                    uint32_t     height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;

  texture                      = NULL;
  view                         = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "assetkit-damaged-helmet-depth";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  textureInfo.width            = width;
  textureInfo.height           = height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL;

  if (GPUCreateTexture(state->device, &textureInfo, &texture) != GPU_OK) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "assetkit-damaged-helmet-depth-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;

  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK) {
    GPUDestroyTexture(texture);
    return 0;
  }

  GPUDestroyTextureView(state->depthView);
  GPUDestroyTexture(state->depthTexture);
  state->depthTexture = texture;
  state->depthView    = view;
  return 1;
}

static int
resize_canvas(AssetSample *state) {
  uint32_t oldWidth, oldHeight;

  oldWidth  = state->width;
  oldHeight = state->height;
  if (!resize_webgpu_canvas(state->swapchain,
                            &state->width,
                            &state->height)) {
    return 0;
  }
  if (oldWidth == state->width && oldHeight == state->height) {
    return 1;
  }
  if (state->swapchain &&
      !create_depth_target(state, state->width, state->height)) {
    state->width  = 0u;
    state->height = 0u;
    return 0;
  }
  build_view_projection(state);
  return 1;
}

static int
create_pipeline(AssetSample *state) {
  GPUVertexAttribute             attributes[3] = {0};
  GPUVertexBufferLayout          vertexLayout  = {0};
  GPUColorTargetState            color         = {0};
  GPUDepthStencilState           depth         = {0};
  GPURenderPipelineCreateInfo    info          = {0};
  const GPUBindGroupLayoutEntry *frameEntries, *materialEntries;
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       frameEntryCount, materialEntryCount;
  GPUResult                      result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/damaged_helmet.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /damaged_helmet.us", 1);
    return 0;
  }
  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the DamagedHelmet artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 2u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0] ||
      !state->shaderLayout->bindGroupLayouts[1]) {
    set_status("GPU: unexpected DamagedHelmet shader reflection", 1);
    return 0;
  }

  frameEntries    = GPUGetBindGroupLayoutEntries(state->shaderLayout->bindGroupLayouts[0],
                                                 &frameEntryCount);
  materialEntries = GPUGetBindGroupLayoutEntries(state->shaderLayout->bindGroupLayouts[1],
                                                 &materialEntryCount);

  if (!frameEntries || frameEntryCount != 1u ||
      frameEntries[0].binding != 0u ||
      frameEntries[0].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      !materialEntries || materialEntryCount != 9u ||
      materialEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[1].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[1].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[2].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[2].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[3].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[3].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[4].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[4].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[5].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[5].sampledTexture.viewType != GPU_TEXTURE_VIEW_CUBE ||
      materialEntries[6].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[6].sampledTexture.viewType != GPU_TEXTURE_VIEW_CUBE ||
      materialEntries[7].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[7].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[8].bindingType != GPU_BINDING_SAMPLER) {
    set_status("GPU: DamagedHelmet reflection lost its material layout", 1);
    return 0;
  }

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[0].offset          = offsetof(AssetVertex, position);
  attributes[0].shaderLocation  = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[1].offset          = offsetof(AssetVertex, normal);
  attributes[1].shaderLocation  = 1u;
  attributes[2].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[2].offset          = offsetof(AssetVertex, uv);
  attributes[2].shaderLocation  = 2u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(AssetVertex);
  vertexLayout.attributeCount   = GPU_ARRAY_LEN(attributes);
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format           = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask  = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "assetkit-damaged-helmet-webgpu-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "asset_vs";
  info.fragmentEntry            = "asset_fs";
  info.pColorTargets            = &color;
  info.pDepthStencilState       = &depth;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount         = 1u;
  info.depthStencilFormat       = GPU_FORMAT_DEPTH32_FLOAT;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_BACK;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create the DamagedHelmet pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_texture_2d(AssetSample      *state,
                  const char       *label,
                  GPUFormat         format,
                  const AssetImage *image,
                  GPUTexture       **outTexture,
                  GPUTextureView   **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = image->width;
  textureInfo.height           = image->height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       outTexture) != GPU_OK) {
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = image->width;
  writeRegion.height       = image->height;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = image->width * 4u;
  writeRegion.rowsPerImage = image->height;
  if (GPUQueueWriteTexture(state->queue,
                           *outTexture,
                           &writeRegion,
                           image->pixels,
                           (uint64_t)image->width * image->height * 4u) != GPU_OK) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(*outTexture, &viewInfo, outView) == GPU_OK;
}

static int
create_environment_cube(AssetSample      *state,
                        const char        *label,
                        const char        *path,
                        uint32_t           baseSize,
                        uint32_t           mipCount,
                        GPUTexture       **outTexture,
                        GPUTextureView   **outView) {
  uint8_t                 *pixels;
  void                    *asset;
  uint64_t                 assetSize;
  uint64_t                 expectedSize;
  uint64_t                 offset;
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  asset        = NULL;
  assetSize    = 0u;
  expectedSize = 0u;
  offset       = 0u;

  for (uint32_t mip = 0u; mip < mipCount; mip++) {
    uint32_t size;

    size = baseSize >> mip;
    if (size == 0u) {
      size = 1u;
    }
    expectedSize += (uint64_t)size * size *
                    PBR_RGBA16_FLOAT_BYTES * PBR_CUBE_FACE_COUNT;
  }

  if (!read_file(path, &asset, &assetSize) ||
      !asset || assetSize != expectedSize) {
    free(asset);
    return 0;
  }
  pixels = asset;

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA16_FLOAT;
  textureInfo.width            = baseSize;
  textureInfo.height           = baseSize;
  textureInfo.depthOrLayers    = PBR_CUBE_FACE_COUNT;
  textureInfo.mipLevelCount    = mipCount;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       outTexture) != GPU_OK) {
    free(asset);
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;

  for (uint32_t mip = 0u; mip < mipCount; mip++) {
    uint32_t size;
    uint64_t faceBytes;

    size                     = baseSize >> mip;
    if (size == 0u) size = 1u;
    faceBytes                = (uint64_t)size * size *
                               PBR_RGBA16_FLOAT_BYTES;
    writeRegion.width        = size;
    writeRegion.height       = size;
    writeRegion.mipLevel     = mip;
    writeRegion.bytesPerRow  = size * PBR_RGBA16_FLOAT_BYTES;
    writeRegion.rowsPerImage = size;
    for (uint32_t face = 0u; face < PBR_CUBE_FACE_COUNT; face++) {
      writeRegion.baseArrayLayer = face;
      if (GPUQueueWriteTexture(state->queue,
                               *outTexture,
                               &writeRegion,
                               pixels + offset,
                               faceBytes) != GPU_OK) {
        free(asset);
        return 0;
      }
      offset += faceBytes;
    }
  }
  free(asset);

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_CUBE;
  viewInfo.format           = GPU_FORMAT_RGBA16_FLOAT;
  viewInfo.mipLevelCount    = mipCount;
  viewInfo.arrayLayerCount  = PBR_CUBE_FACE_COUNT;
  return GPUCreateTextureView(*outTexture, &viewInfo, outView) == GPU_OK;
}

static int
create_ggx_lut(AssetSample *state) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  unsigned char           *pixels;
  int                      width, height;

  width  = 0;
  height = 0;
  pixels = (unsigned char *)
           emscripten_get_preloaded_image_data("/lut_ggx.png",
                                               &width,
                                               &height);
  if (!pixels || width <= 0 || height <= 0) {
    free(pixels);
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "assetkit-damaged-helmet-ggx-lut";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = (uint32_t)width;
  textureInfo.height           = (uint32_t)height;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(state->device,
                       &textureInfo,
                       &state->ggxLUTTexture) != GPU_OK) {
    free(pixels);
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = (uint32_t)width;
  writeRegion.height       = (uint32_t)height;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = (uint32_t)width * 4u;
  writeRegion.rowsPerImage = (uint32_t)height;

  if (GPUQueueWriteTexture(state->queue,
                           state->ggxLUTTexture,
                           &writeRegion,
                           pixels,
                           (uint64_t)width * (uint64_t)height * 4u) != GPU_OK) {
    free(pixels);
    return 0;
  }
  free(pixels);

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "assetkit-damaged-helmet-ggx-lut-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;

  return GPUCreateTextureView(state->ggxLUTTexture,
                              &viewInfo,
                              &state->ggxLUTView) == GPU_OK;
}

static int
create_geometry(AssetSample *state) {
  PBRUniforms         uniforms;
  GPUBufferCreateInfo info = {0};
  size_t              vertexBytes, indexBytes, indexElementSize;

  if (!state->asset.vertices || !state->asset.indices ||
      state->asset.vertexCount == 0u || state->asset.indexCount == 0u) {
    return 0;
  }
  indexElementSize = state->asset.indexType == ASSET_INDEX_UINT16
                   ? sizeof(uint16_t)
                   : sizeof(uint32_t);
  vertexBytes = sizeof(AssetVertex) * state->asset.vertexCount;
  indexBytes  = indexElementSize * state->asset.indexCount;
  build_uniforms(state, 0.0f, &uniforms);

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "assetkit-damaged-helmet-vertices";
  info.sizeBytes        = vertexBytes;
  info.usage            = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &info,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          state->asset.vertices,
                          vertexBytes) != GPU_OK) {
    return 0;
  }

  info.label     = "assetkit-damaged-helmet-indices";
  info.sizeBytes = indexBytes;
  info.usage     = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &info,
                      &state->indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->indexBuffer,
                          0u,
                          state->asset.indices,
                          indexBytes) != GPU_OK) {
    return 0;
  }

  info.label     = "assetkit-damaged-helmet-uniforms";
  info.sizeBytes = sizeof(uniforms);
  info.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &info,
                      &state->uniformBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->uniformBuffer,
                          0u,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    return 0;
  }
  return 1;
}

static int
create_material(AssetSample *state) {
  static const char *textureLabels[ASSET_TEXTURE_COUNT] = {
    "assetkit-damaged-helmet-base-color",
    "assetkit-damaged-helmet-normal",
    "assetkit-damaged-helmet-metallic-roughness",
    "assetkit-damaged-helmet-occlusion",
    "assetkit-damaged-helmet-emissive"
  };
  GPUSamplerCreateInfo   samplerInfo        = {0};
  GPUBindGroupEntry      frameEntry         = {0};
  GPUBindGroupEntry      materialEntries[9] = {0};
  GPUBindGroupCreateInfo frameInfo          = {0};
  GPUBindGroupCreateInfo materialInfo       = {0};

  for (uint32_t i = 0u; i < ASSET_TEXTURE_COUNT; i++) {
    GPUFormat format;

    format = i == ASSET_TEXTURE_BASE_COLOR ||
             i == ASSET_TEXTURE_EMISSIVE
           ? GPU_FORMAT_RGBA8_UNORM_SRGB
           : GPU_FORMAT_RGBA8_UNORM;
    if (!create_texture_2d(state,
                           textureLabels[i],
                           format,
                           &state->asset.material.images[i],
                           &state->materialTextures[i],
                           &state->materialViews[i])) {
      return 0;
    }
  }
  if (!create_environment_cube(state,
                               "assetkit-damaged-helmet-diffuse-environment",
                               "/studio_diffuse.rgba16f",
                               PBR_DIFFUSE_ENV_SIZE,
                               1u,
                               &state->diffuseEnvironmentTexture,
                               &state->diffuseEnvironmentView) ||
      !create_environment_cube(state,
                               "assetkit-damaged-helmet-specular-environment",
                               "/studio_specular.rgba16f",
                               PBR_SPECULAR_ENV_SIZE,
                               PBR_SPECULAR_ENV_MIPS,
                               &state->specularEnvironmentTexture,
                               &state->specularEnvironmentView) ||
      !create_ggx_lut(state)) {
    return 0;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "assetkit-damaged-helmet-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressV    = GPU_ADDRESS_MODE_REPEAT;
  samplerInfo.desc.addressW    = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  if (GPUCreateSampler(state->device,
                       &samplerInfo,
                       false,
                       &state->sampler) != GPU_OK) {
    return 0;
  }

  frameEntry.buffer.buffer = state->uniformBuffer;
  frameEntry.buffer.size   = sizeof(PBRUniforms);
  frameEntry.binding       = 0u;
  frameEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  frameInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  frameInfo.chain.structSize = sizeof(frameInfo);
  frameInfo.label             = "assetkit-damaged-helmet-group0";
  frameInfo.layout            = state->shaderLayout->bindGroupLayouts[0];
  frameInfo.pEntries          = &frameEntry;
  frameInfo.entryCount        = 1u;
  if (GPUCreateBindGroup(state->device,
                         &frameInfo,
                         &state->frameGroup) != GPU_OK) {
    return 0;
  }

  for (uint32_t i = 0u; i < ASSET_TEXTURE_COUNT; i++) {
    materialEntries[i].textureView = state->materialViews[i];
    materialEntries[i].binding     = i;
    materialEntries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  }
  materialEntries[5].textureView = state->diffuseEnvironmentView;
  materialEntries[5].binding     = 5u;
  materialEntries[5].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[6].textureView = state->specularEnvironmentView;
  materialEntries[6].binding     = 6u;
  materialEntries[6].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[7].textureView = state->ggxLUTView;
  materialEntries[7].binding     = 7u;
  materialEntries[7].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[8].sampler     = state->sampler;
  materialEntries[8].binding     = 8u;
  materialEntries[8].bindingType = GPU_BINDING_SAMPLER;
  materialInfo.chain.sType       = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  materialInfo.chain.structSize  = sizeof(materialInfo);
  materialInfo.label             = "assetkit-damaged-helmet-group1";
  materialInfo.layout            = state->shaderLayout->bindGroupLayouts[1];
  materialInfo.pEntries          = materialEntries;
  materialInfo.entryCount        = GPU_ARRAY_LEN(materialEntries);
  return GPUCreateBindGroup(state->device,
                            &materialInfo,
                            &state->materialGroup) == GPU_OK;
}

static int
update_uniforms(AssetSample *state) {
  PBRUniforms uniforms;
  float          seconds;

  seconds = (float)(emscripten_get_now() * 0.001);
  build_uniforms(state, seconds, &uniforms);
  return GPUQueueWriteBuffer(state->queue,
                             state->uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

static void
render_frame(void *userData) {
  AssetSample                        *state;
  GPUFrame                            *frame;
  GPUCommandBuffer                    *cmdb;
  GPURenderPassEncoder                *pass;
  GPUBufferBinding                     vertexBuffer = {0};
  GPURenderPassColorAttachment         color        = {0};
  GPURenderPassDepthStencilAttachment depth       = {0};
  GPURenderPassCreateInfo              passInfo     = {0};

  state = userData;
  if (!resize_canvas(state) || !update_uniforms(state)) {
    set_status("GPU: failed to update the DamagedHelmet frame", 1);
    emscripten_cancel_main_loop();
    return;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return;
  }
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "assetkit-damaged-helmet-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.015f;
  color.clearColor.float32[2] = 0.036f;
  color.clearColor.float32[3] = 1.0f;
  depth.view                  = state->depthView;
  depth.depthLoadOp           = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp          = GPU_STORE_OP_DONT_CARE;
  depth.stencilLoadOp         = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp        = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth            = 1.0f;

  passInfo.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize        = sizeof(passInfo);
  passInfo.label                   = "assetkit-damaged-helmet-webgpu-pass";
  passInfo.pColorAttachments       = &color;
  passInfo.pDepthStencilAttachment = &depth;
  passInfo.colorAttachmentCount    = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  vertexBuffer.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass, 0u, state->frameGroup, 0u, NULL);
  GPUBindRenderGroup(pass, 1u, state->materialGroup, 0u, NULL);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindIndexBuffer(
    pass,
    state->indexBuffer,
    0u,
    state->asset.indexType == ASSET_INDEX_UINT16
      ? GPU_INDEX_TYPE_UINT16
      : GPU_INDEX_TYPE_UINT32
  );
  GPUDrawIndexed(pass, state->asset.indexCount, 1u, 0u, 0, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the DamagedHelmet frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.hotPathAllocCount != 0u || stats.hotPathFreeCount != 0u)) {
      set_status("GPU: warm DamagedHelmet frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
asset_ready(Asset      *asset,
            const char *error,
            void       *userData) {
  AssetSample *state;

  state = userData;
  if (!asset || error) {
    set_status(error ? error : "AssetKit: failed to load DamagedHelmet", 1);
    return;
  }
  state->asset = *asset;
  memset(asset, 0, sizeof(*asset));
  if (!create_geometry(state) || !create_material(state)) {
    asset_release(&state->asset);
    set_status("GPU: failed to upload DamagedHelmet resources", 1);
    return;
  }
  asset_release_uploads(&state->asset);
  set_status("GPU: AssetKit DamagedHelmet ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, false);
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  AssetSample     *state;
  GPURuntimeConfig runtime = {0};

  state = userData;
  if (result != GPU_OK || !adapter || !device) {
    set_status(!adapter ? "GPU: failed to request WebGPU adapter"
                        : "GPU: failed to request WebGPU device",
               1);
    return;
  }

  state->adapter = adapter;
  state->device  = device;
  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (GPUConfigureRuntime(device, &runtime) != GPU_OK) {
    set_status("GPU: failed to configure WebGPU runtime stats", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               state->adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->queue || !state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create WebGPU queue or canvas surface", 1);
    return;
  }

  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain ||
      !create_depth_target(state, state->width, state->height) ||
      !create_pipeline(state)) {
    set_status("GPU: failed to initialize DamagedHelmet resources", 1);
    return;
  }

  set_status("AssetKit: downloading DamagedHelmet from Khronos", 0);
  asset_load(asset_ready, state);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "assetkit-damaged-helmet-webgpu-usl";
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  result = GPUCreateInstance(&info, &app.instance);
  if (result != GPU_OK || !app.instance) {
    set_status("GPU: failed to create WebGPU instance", 1);
    return 1;
  }

  set_status("GPU: requesting WebGPU device", 0);
  result = request_webgpu_device(app.instance,
                                 &app.request,
                                 webgpu_ready,
                                 &app);
  return result == GPU_OK ? 0 : 1;
}
