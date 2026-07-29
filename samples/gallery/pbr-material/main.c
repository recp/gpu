#include "../../common/sample_platform.h"
#include "../../common/sample_orbit.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#include <math.h>

typedef struct PBRVertex {
  float position[3];
  float normal[3];
  float tangent[4];
  float uv[2];
} PBRVertex;

typedef struct PBRUniforms {
  mat4 mvp;
  mat4 model;
  vec4 cameraPosition;
  vec4 lightDirection;
} PBRUniforms;

typedef struct WebGPUPBR {
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
  GPUTexture        *albedoTexture;
  GPUTextureView    *albedoView;
  GPUTexture        *normalTexture;
  GPUTextureView    *normalView;
  GPUTexture        *materialTexture;
  GPUTextureView    *materialView;
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
  SampleOrbit        orbit;
  mat4               viewProjection;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} WebGPUPBR;

enum {
  PBR_LATITUDE_SEGMENTS  = 64u,
  PBR_LONGITUDE_SEGMENTS = 96u,
  PBR_VERTEX_COUNT       = (PBR_LATITUDE_SEGMENTS + 1u) *
                           (PBR_LONGITUDE_SEGMENTS + 1u),
  PBR_INDEX_COUNT        = PBR_LATITUDE_SEGMENTS *
                           PBR_LONGITUDE_SEGMENTS * 6u,
  PBR_TEXTURE_SIZE       = 64u,
  PBR_TEXTURE_ROW_BYTES  = PBR_TEXTURE_SIZE * 4u,
  PBR_TEXTURE_BYTES      = PBR_TEXTURE_ROW_BYTES * PBR_TEXTURE_SIZE,
  PBR_DIFFUSE_ENV_SIZE   = 32u,
  PBR_SPECULAR_ENV_SIZE  = 64u,
  PBR_SPECULAR_ENV_MIPS  = 7u,
  PBR_RGBA16_FLOAT_BYTES = 8u,
  PBR_CUBE_FACE_COUNT    = 6u,
  WARM_FRAME_COUNT       = 8u
};

_Static_assert(PBR_VERTEX_COUNT <= UINT16_MAX,
               "the PBR sphere must fit uint16 indices");
_Static_assert(sizeof(PBRUniforms) == 160u,
               "PBR uniforms must match the reflected USL layout");

static WebGPUPBR app;

static void
build_sphere(PBRVertex vertices[PBR_VERTEX_COUNT],
             uint16_t  indices[PBR_INDEX_COUNT]) {
  uint32_t index = 0u;

  for (uint32_t latitude = 0u;
       latitude <= PBR_LATITUDE_SEGMENTS;
       latitude++) {
    float v, theta, ringRadius, y;

    v          = (float)latitude / (float)PBR_LATITUDE_SEGMENTS;
    theta      = v * (float)GLM_PI;
    ringRadius = sinf(theta);
    y          = cosf(theta);

    for (uint32_t longitude = 0u;
         longitude <= PBR_LONGITUDE_SEGMENTS;
         longitude++) {
      PBRVertex *vertex;
      float      u, phi, x, z;

      u      = (float)longitude / (float)PBR_LONGITUDE_SEGMENTS;
      phi    = u * (float)GLM_PI * 2.0f;
      x      = ringRadius * cosf(phi);
      z      = ringRadius * sinf(phi);
      vertex = &vertices[latitude * (PBR_LONGITUDE_SEGMENTS + 1u) +
                         longitude];

      vertex->position[0] = x;
      vertex->position[1] = y;
      vertex->position[2] = z;
      vertex->normal[0]   = x;
      vertex->normal[1]   = y;
      vertex->normal[2]   = z;
      vertex->tangent[0]  = -sinf(phi);
      vertex->tangent[1]  = 0.0f;
      vertex->tangent[2]  = cosf(phi);
      vertex->tangent[3]  = 1.0f;
      vertex->uv[0]       = u;
      vertex->uv[1]       = v;
    }
  }

  for (uint32_t latitude = 0u;
       latitude < PBR_LATITUDE_SEGMENTS;
       latitude++) {
    for (uint32_t longitude = 0u;
         longitude < PBR_LONGITUDE_SEGMENTS;
         longitude++) {
      uint16_t upperLeft, upperRight, lowerLeft, lowerRight;

      upperLeft  = (uint16_t)(latitude *
                              (PBR_LONGITUDE_SEGMENTS + 1u) +
                              longitude);
      upperRight = (uint16_t)(upperLeft + 1u);
      lowerLeft  = (uint16_t)(upperLeft +
                              PBR_LONGITUDE_SEGMENTS + 1u);
      lowerRight = (uint16_t)(lowerLeft + 1u);

      indices[index++] = upperLeft;
      indices[index++] = lowerRight;
      indices[index++] = lowerLeft;
      indices[index++] = upperLeft;
      indices[index++] = upperRight;
      indices[index++] = lowerRight;
    }
  }
}

static void
fill_albedo(uint8_t pixels[PBR_TEXTURE_BYTES]) {
  static const uint8_t colors[2][3] = {
    {224u,  76u,  30u},
    { 18u, 122u, 154u}
  };

  for (uint32_t y = 0u; y < PBR_TEXTURE_SIZE; y++) {
    for (uint32_t x = 0u; x < PBR_TEXTURE_SIZE; x++) {
      const uint8_t *color;
      uint32_t       offset;

      color  = colors[((x >> 3u) ^ (y >> 3u)) & 1u];
      offset = (y * PBR_TEXTURE_SIZE + x) * 4u;
      pixels[offset + 0u] = color[0];
      pixels[offset + 1u] = color[1];
      pixels[offset + 2u] = color[2];
      pixels[offset + 3u] = 255u;
    }
  }
}

static void
fill_normal(uint8_t pixels[PBR_TEXTURE_BYTES]) {
  for (uint32_t y = 0u; y < PBR_TEXTURE_SIZE; y++) {
    for (uint32_t x = 0u; x < PBR_TEXTURE_SIZE; x++) {
      vec3     normal;
      float    u, v;
      uint32_t offset;

      u         = ((float)x + 0.5f) / (float)PBR_TEXTURE_SIZE;
      v         = ((float)y + 0.5f) / (float)PBR_TEXTURE_SIZE;
      normal[0] = sinf(u * (float)GLM_PI * 8.0f) * 0.20f;
      normal[1] = cosf(v * (float)GLM_PI * 8.0f) * 0.20f;
      normal[2] = 1.0f;
      glm_vec3_normalize(normal);
      offset = (y * PBR_TEXTURE_SIZE + x) * 4u;
      pixels[offset + 0u] = (uint8_t)((normal[0] * 0.5f + 0.5f) * 255.0f);
      pixels[offset + 1u] = (uint8_t)((normal[1] * 0.5f + 0.5f) * 255.0f);
      pixels[offset + 2u] = (uint8_t)((normal[2] * 0.5f + 0.5f) * 255.0f);
      pixels[offset + 3u] = 255u;
    }
  }
}

static void
fill_material(uint8_t pixels[PBR_TEXTURE_BYTES]) {
  for (uint32_t y = 0u; y < PBR_TEXTURE_SIZE; y++) {
    for (uint32_t x = 0u; x < PBR_TEXTURE_SIZE; x++) {
      float    u, v, ao, roughness, metallic;
      uint32_t offset;

      u         = ((float)x + 0.5f) / (float)PBR_TEXTURE_SIZE;
      v         = ((float)y + 0.5f) / (float)PBR_TEXTURE_SIZE;
      ao        = 0.84f + 0.16f * sinf((u + v) * (float)GLM_PI_2);
      roughness = 0.14f + v * 0.74f;
      metallic  = 0.06f + u * 0.90f;
      offset    = (y * PBR_TEXTURE_SIZE + x) * 4u;
      pixels[offset + 0u] = (uint8_t)(ao * 255.0f);
      pixels[offset + 1u] = (uint8_t)(roughness * 255.0f);
      pixels[offset + 2u] = (uint8_t)(metallic * 255.0f);
      pixels[offset + 3u] = 255u;
    }
  }
}

static void
build_view_projection(WebGPUPBR *state) {
  vec3 eye = {0.0f, 0.0f, 3.35f}, center = {0.0f, 0.0f, 0.0f}, up = {0.0f, 1.0f, 0.0f};
  mat4 view, projection;
  float aspect;

  aspect = gpu_sample_aspect_ratio(state->width, state->height);
  glm_lookat(eye, center, up, view);
  glm_perspective(glm_rad(44.0f), aspect, 0.1f, 100.0f, projection);
  glm_mat4_mul(projection, view, state->viewProjection);
}

static void
build_uniforms(WebGPUPBR   *state,
               PBRUniforms *uniforms) {
  vec3 axisX = {1.0f, 0.0f, 0.0f}, axisY = {0.0f, 1.0f, 0.0f};
  vec4 camera = {0.0f, 0.0f, 3.35f, 1.0f}, light = {0.44f, 0.78f, 0.54f, 0.0f};

  glm_mat4_identity(uniforms->model);
  glm_rotate(uniforms->model, state->orbit.yaw, axisY);
  glm_rotate(uniforms->model, state->orbit.pitch, axisX);
  glm_mat4_mul(state->viewProjection, uniforms->model, uniforms->mvp);
  glm_vec4_copy(camera, uniforms->cameraPosition);
  glm_vec4_normalize_to(light, uniforms->lightDirection);
}

static int
create_depth_target(WebGPUPBR *state,
                    uint32_t   width,
                    uint32_t   height) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};
  GPUTexture              *texture;
  GPUTextureView          *view;

  texture = NULL;
  view    = NULL;
  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "pbr-material-depth";
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
  viewInfo.label            = "pbr-material-depth-view";
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
resize_canvas(WebGPUPBR *state) {
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
create_pipeline(WebGPUPBR *state) {
  GPUVertexAttribute             attributes[4] = {0};
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
  if (!read_file("/pbr_material.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /pbr_material.us", 1);
    return 0;
  }
  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the PBR artifact", 1);
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
    set_status("GPU: unexpected PBR shader reflection", 1);
    return 0;
  }

  frameEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &frameEntryCount
  );
  materialEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[1],
    &materialEntryCount
  );
  if (!frameEntries || frameEntryCount != 1u ||
      frameEntries[0].binding != 0u ||
      frameEntries[0].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      !materialEntries || materialEntryCount != 7u ||
      materialEntries[0].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[0].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[1].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[1].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[2].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[2].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[3].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[3].sampledTexture.viewType != GPU_TEXTURE_VIEW_CUBE ||
      materialEntries[4].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[4].sampledTexture.viewType != GPU_TEXTURE_VIEW_CUBE ||
      materialEntries[5].bindingType != GPU_BINDING_SAMPLED_TEXTURE ||
      materialEntries[5].sampledTexture.viewType != GPU_TEXTURE_VIEW_2D ||
      materialEntries[6].bindingType != GPU_BINDING_SAMPLER) {
    set_status("GPU: PBR reflection lost its material layout", 1);
    return 0;
  }

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[0].offset          = offsetof(PBRVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X3;
  attributes[1].offset          = offsetof(PBRVertex, normal);
  attributes[1].shaderLocation = 1u;
  attributes[2].format          = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[2].offset          = offsetof(PBRVertex, tangent);
  attributes[2].shaderLocation = 2u;
  attributes[3].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[3].offset          = offsetof(PBRVertex, uv);
  attributes[3].shaderLocation = 3u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(PBRVertex);
  vertexLayout.attributeCount   = GPU_ARRAY_LEN(attributes);
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;
  depth.depthCompare     = GPU_COMPARE_LESS;
  depth.depthTestEnable  = true;
  depth.depthWriteEnable = true;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "pbr-material-webgpu-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "pbr_vs";
  info.fragmentEntry            = "pbr_fs";
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
    set_status("GPU: failed to create the PBR pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_texture_2d(WebGPUPBR      *state,
                  const char     *label,
                  GPUFormat       format,
                  const uint8_t  *pixels,
                  GPUTexture    **outTexture,
                  GPUTextureView **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUTextureViewCreateInfo viewInfo    = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = PBR_TEXTURE_SIZE;
  textureInfo.height           = PBR_TEXTURE_SIZE;
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
  writeRegion.width        = PBR_TEXTURE_SIZE;
  writeRegion.height       = PBR_TEXTURE_SIZE;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = PBR_TEXTURE_ROW_BYTES;
  writeRegion.rowsPerImage = PBR_TEXTURE_SIZE;
  if (GPUQueueWriteTexture(state->queue,
                           *outTexture,
                           &writeRegion,
                           pixels,
                           PBR_TEXTURE_BYTES) != GPU_OK) {
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
create_environment_cube(WebGPUPBR      *state,
                        const char     *label,
                        const char     *path,
                        uint32_t        baseSize,
                        uint32_t        mipCount,
                        GPUTexture    **outTexture,
                        GPUTextureView **outView) {
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
create_ggx_lut(WebGPUPBR *state) {
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
  textureInfo.label            = "pbr-material-ggx-lut";
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
  viewInfo.label            = "pbr-material-ggx-lut-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(state->ggxLUTTexture,
                              &viewInfo,
                              &state->ggxLUTView) == GPU_OK;
}

static int
create_geometry(WebGPUPBR *state) {
  PBRVertex          *vertices;
  uint16_t           *indices;
  void               *geometry;
  PBRUniforms         uniforms;
  GPUBufferCreateInfo info = {0};
  size_t              vertexBytes, indexBytes;
  int                 result;

  vertexBytes = sizeof(PBRVertex) * PBR_VERTEX_COUNT;
  indexBytes  = sizeof(uint16_t) * PBR_INDEX_COUNT;
  geometry    = malloc(vertexBytes + indexBytes);
  if (!geometry) {
    return 0;
  }
  vertices = geometry;
  indices  = (uint16_t *)((uint8_t *)geometry + vertexBytes);
  build_sphere(vertices, indices);
  build_uniforms(state, &uniforms);
  result = 0;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "pbr-material-vertices";
  info.sizeBytes        = vertexBytes;
  info.usage            = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &info,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          vertices,
                          vertexBytes) != GPU_OK) {
    goto cleanup;
  }

  info.label     = "pbr-material-indices";
  info.sizeBytes = indexBytes;
  info.usage     = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &info,
                      &state->indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->indexBuffer,
                          0u,
                          indices,
                          indexBytes) != GPU_OK) {
    goto cleanup;
  }

  info.label     = "pbr-material-uniforms";
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
    goto cleanup;
  }
  result = 1;

cleanup:
  free(geometry);
  return result;
}

static int
create_material(WebGPUPBR *state) {
  uint8_t                albedoPixels[PBR_TEXTURE_BYTES];
  uint8_t                normalPixels[PBR_TEXTURE_BYTES];
  uint8_t                materialPixels[PBR_TEXTURE_BYTES];
  GPUSamplerCreateInfo   samplerInfo        = {0};
  GPUBindGroupEntry      frameEntry         = {0};
  GPUBindGroupEntry      materialEntries[7] = {0};
  GPUBindGroupCreateInfo frameInfo          = {0};
  GPUBindGroupCreateInfo materialInfo       = {0};

  fill_albedo(albedoPixels);
  fill_normal(normalPixels);
  fill_material(materialPixels);
  if (!create_texture_2d(state,
                         "pbr-material-albedo",
                         GPU_FORMAT_RGBA8_UNORM_SRGB,
                         albedoPixels,
                         &state->albedoTexture,
                         &state->albedoView) ||
      !create_texture_2d(state,
                         "pbr-material-normal",
                         GPU_FORMAT_RGBA8_UNORM,
                         normalPixels,
                         &state->normalTexture,
                         &state->normalView) ||
      !create_texture_2d(state,
                         "pbr-material-metallic-roughness",
                         GPU_FORMAT_RGBA8_UNORM,
                         materialPixels,
                         &state->materialTexture,
                         &state->materialView) ||
      !create_environment_cube(state,
                               "pbr-material-diffuse-environment",
                               "/studio_diffuse.rgba16f",
                               PBR_DIFFUSE_ENV_SIZE,
                               1u,
                               &state->diffuseEnvironmentTexture,
                               &state->diffuseEnvironmentView) ||
      !create_environment_cube(state,
                               "pbr-material-specular-environment",
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
  samplerInfo.label            = "pbr-material-sampler";
  samplerInfo.desc.minFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.magFilter   = GPU_FILTER_LINEAR;
  samplerInfo.desc.mipFilter   = GPU_MIP_FILTER_LINEAR;
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
  frameInfo.label             = "pbr-material-group0";
  frameInfo.layout            = state->shaderLayout->bindGroupLayouts[0];
  frameInfo.pEntries          = &frameEntry;
  frameInfo.entryCount        = 1u;
  if (GPUCreateBindGroup(state->device,
                         &frameInfo,
                         &state->frameGroup) != GPU_OK) {
    return 0;
  }

  materialEntries[0].textureView = state->albedoView;
  materialEntries[0].binding     = 0u;
  materialEntries[0].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[1].textureView = state->normalView;
  materialEntries[1].binding     = 1u;
  materialEntries[1].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[2].textureView = state->materialView;
  materialEntries[2].binding     = 2u;
  materialEntries[2].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[3].textureView = state->diffuseEnvironmentView;
  materialEntries[3].binding     = 3u;
  materialEntries[3].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[4].textureView = state->specularEnvironmentView;
  materialEntries[4].binding     = 4u;
  materialEntries[4].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[5].textureView = state->ggxLUTView;
  materialEntries[5].binding     = 5u;
  materialEntries[5].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
  materialEntries[6].sampler     = state->sampler;
  materialEntries[6].binding     = 6u;
  materialEntries[6].bindingType = GPU_BINDING_SAMPLER;
  materialInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  materialInfo.chain.structSize = sizeof(materialInfo);
  materialInfo.label             = "pbr-material-group1";
  materialInfo.layout            = state->shaderLayout->bindGroupLayouts[1];
  materialInfo.pEntries          = materialEntries;
  materialInfo.entryCount        = GPU_ARRAY_LEN(materialEntries);
  return GPUCreateBindGroup(state->device,
                            &materialInfo,
                            &state->materialGroup) == GPU_OK;
}

static int
update_uniforms(WebGPUPBR *state) {
  PBRUniforms uniforms;

  sample_orbit_update(&state->orbit, emscripten_get_now() * 0.001);
  build_uniforms(state, &uniforms);
  return GPUQueueWriteBuffer(state->queue,
                             state->uniformBuffer,
                             0u,
                             &uniforms,
                             sizeof(uniforms)) == GPU_OK;
}

static void
render_frame(void *userData) {
  WebGPUPBR                         *state;
  GPUFrame                          *frame;
  GPUCommandBuffer                  *cmdb;
  GPURenderPassEncoder              *pass;
  GPUBufferBinding                   vertexBuffer = {0};
  GPURenderPassColorAttachment       color        = {0};
  GPURenderPassDepthStencilAttachment depth       = {0};
  GPURenderPassCreateInfo            passInfo     = {0};

  state = userData;
  if (!resize_canvas(state) || !update_uniforms(state)) {
    set_status("GPU: failed to update the PBR frame", 1);
    emscripten_cancel_main_loop();
    return;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return;
  }
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "pbr-material-webgpu-frame",
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
  passInfo.label                   = "pbr-material-webgpu-pass";
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
  GPUBindIndexBuffer(pass, state->indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexed(pass, PBR_INDEX_COUNT, 1u, 0u, 0, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the PBR frame", 1);
  } else {
    GPUFrameStats stats;

    state->frameCount++;
    if (state->frameCount > WARM_FRAME_COUNT &&
        GPUGetLastFrameStats(state->device, &stats) == GPU_OK &&
        (stats.hotPathAllocCount != 0u || stats.hotPathFreeCount != 0u)) {
      set_status("GPU: warm PBR frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUPBR       *state;
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
  sample_orbit_init(&state->orbit, 0.0f, -0.12f, 0.32f, 0.0f);
  if (!state->swapchain ||
      !create_depth_target(state, state->width, state->height) ||
      !create_pipeline(state) ||
      !create_geometry(state) ||
      !create_material(state)) {
    set_status("GPU: failed to initialize PBR resources", 1);
    return;
  }

  sample_orbit_activate(&state->orbit);
  set_status("GPU: WebGPU USL PBR material ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "pbr-material-webgpu-usl";
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
