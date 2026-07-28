#include "../common/android_samples.h"

#include <string.h>

int gpu_android_start_triangle(void);
int gpu_android_start_textured_quad(void);
int gpu_android_start_compute(void);
int gpu_android_start_indexed_depth(void);
int gpu_android_start_instancing(void);
int gpu_android_start_textured_cube(void);
int gpu_android_start_storage_texture(void);
int gpu_android_start_push_constants(void);
int gpu_android_start_msaa(void);
int gpu_android_start_mrt_blend(void);
int gpu_android_start_shadow_compare(void);
int gpu_android_start_texture_array(void);
int gpu_android_start_texture_line(void);
int gpu_android_start_texture_shapes(void);
int gpu_android_start_descriptor_array(void);
int gpu_android_start_msaa_samples(void);
int gpu_android_start_subgroup(void);
int gpu_android_start_shader_f16(void);
int gpu_android_start_multi_draw(void);
int gpu_android_start_dispatch_indirect(void);
int gpu_android_start_timestamp_query(void);
int gpu_android_start_compute_particles(void);
int gpu_android_start_mip_lod(void);
int gpu_android_start_stencil_outline(void);
int gpu_android_start_bloom(void);
int gpu_android_start_image_texture(void);
int gpu_android_start_integer_cube(void);
int gpu_android_start_color_pipeline(void);
int gpu_android_start_compressed_texture(void);
int gpu_android_start_skinning(void);
int gpu_android_start_pbr_material(void);
int gpu_android_start_assetkit_damaged_helmet(void);
int gpu_android_start_blit(void);

static const GPUFeature descriptorArrayFeatures[] = {
  GPU_FEATURE_DESCRIPTOR_INDEXING
};

static const GPUFeature subgroupFeatures[] = {
  GPU_FEATURE_SUBGROUPS
};

static const GPUFeature shaderF16Features[] = {
  GPU_FEATURE_SHADER_F16
};

static const GPUFeature timestampFeatures[] = {
  GPU_FEATURE_TIMESTAMPS
};

#define GPU_ANDROID_WEB_SAMPLE(symbol, sampleId, sampleName, startFunction) \
  static const GPUAndroidWebConfig symbol##Config = {                      \
    .start = startFunction                                                 \
  };                                                                       \
  static GPUAndroidSampleDefinition symbol##Definition = {                 \
    .callbacks    = NULL,                                                   \
    .config       = &symbol##Config,                                        \
    .id           = sampleId,                                               \
    .name         = sampleName,                                             \
    .userDataSize = 1u                                                      \
  }

#define GPU_ANDROID_WEB_FEATURE_SAMPLE(                                    \
  symbol, sampleId, sampleName, startFunction, featureSet                  \
)                                                                         \
  static const GPUAndroidWebConfig symbol##Config = {                      \
    .start = startFunction                                                 \
  };                                                                       \
  static GPUAndroidSampleDefinition symbol##Definition = {                 \
    .callbacks            = NULL,                                           \
    .config               = &symbol##Config,                                \
    .optionalFeatures     = featureSet,                                     \
    .id                   = sampleId,                                       \
    .name                 = sampleName,                                     \
    .userDataSize         = 1u,                                             \
    .optionalFeatureCount =                                                 \
      (uint32_t)(sizeof(featureSet) / sizeof((featureSet)[0]))              \
  }

GPU_ANDROID_WEB_SAMPLE(texturedQuad,
                       "textured-quad",
                       "GPU + USL Transfer Chain",
                       gpu_android_start_textured_quad);
GPU_ANDROID_WEB_SAMPLE(triangle,
                       "triangle",
                       "GPU + USL First Pixel",
                       gpu_android_start_triangle);
GPU_ANDROID_WEB_SAMPLE(compute,
                       "compute",
                       "GPU + USL Compute Handoff",
                       gpu_android_start_compute);
GPU_ANDROID_WEB_SAMPLE(indexedDepth,
                       "indexed-depth",
                       "GPU + USL Indexed Depth",
                       gpu_android_start_indexed_depth);
GPU_ANDROID_WEB_SAMPLE(instancing,
                       "instancing",
                       "GPU + USL Instancing",
                       gpu_android_start_instancing);
GPU_ANDROID_WEB_SAMPLE(texturedCube,
                       "textured-cube",
                       "GPU + USL Rotating Cube",
                       gpu_android_start_textured_cube);
GPU_ANDROID_WEB_SAMPLE(storageTexture,
                       "storage-texture",
                       "GPU + USL Storage Texture",
                       gpu_android_start_storage_texture);
GPU_ANDROID_WEB_SAMPLE(pushConstants,
                       "push-constants",
                       "GPU + USL Push Constants",
                       gpu_android_start_push_constants);
GPU_ANDROID_WEB_SAMPLE(msaa,
                       "msaa",
                       "GPU + USL MSAA Resolve",
                       gpu_android_start_msaa);
GPU_ANDROID_WEB_SAMPLE(mrtBlend,
                       "mrt-blend",
                       "GPU + USL MRT Blend",
                       gpu_android_start_mrt_blend);
GPU_ANDROID_WEB_SAMPLE(shadowCompare,
                       "shadow-compare",
                       "GPU + USL Comparison Shadow",
                       gpu_android_start_shadow_compare);
GPU_ANDROID_WEB_SAMPLE(textureArray,
                       "texture-array",
                       "GPU + USL Texture Array",
                       gpu_android_start_texture_array);
GPU_ANDROID_WEB_SAMPLE(textureLine,
                       "texture-line",
                       "GPU + USL Texture Line",
                       gpu_android_start_texture_line);
GPU_ANDROID_WEB_SAMPLE(textureShapes,
                       "texture-shapes",
                       "GPU + USL Texture Shapes",
                       gpu_android_start_texture_shapes);
GPU_ANDROID_WEB_FEATURE_SAMPLE(descriptorArray,
                               "descriptor-array",
                               "GPU + USL Descriptor Arrays",
                               gpu_android_start_descriptor_array,
                               descriptorArrayFeatures);
GPU_ANDROID_WEB_SAMPLE(msaaSamples,
                       "msaa-samples",
                       "GPU + USL MSAA Samples",
                       gpu_android_start_msaa_samples);
GPU_ANDROID_WEB_FEATURE_SAMPLE(subgroup,
                               "subgroup",
                               "GPU + USL Subgroup Exchange",
                               gpu_android_start_subgroup,
                               subgroupFeatures);
GPU_ANDROID_WEB_FEATURE_SAMPLE(shaderF16,
                               "shader-f16",
                               "GPU + USL Half Precision",
                               gpu_android_start_shader_f16,
                               shaderF16Features);
GPU_ANDROID_WEB_SAMPLE(multiDraw,
                       "multi-draw",
                       "GPU + USL Multi Draw",
                       gpu_android_start_multi_draw);
GPU_ANDROID_WEB_SAMPLE(dispatchIndirect,
                       "dispatch-indirect",
                       "GPU + USL Indirect Dispatch",
                       gpu_android_start_dispatch_indirect);
GPU_ANDROID_WEB_FEATURE_SAMPLE(timestampQuery,
                               "timestamp-query",
                               "GPU + USL Pass Timestamps",
                               gpu_android_start_timestamp_query,
                               timestampFeatures);
GPU_ANDROID_WEB_SAMPLE(computeParticles,
                       "compute-particles",
                       "GPU + USL Compute Particles",
                       gpu_android_start_compute_particles);
GPU_ANDROID_WEB_SAMPLE(mipLod,
                       "mip-lod",
                       "GPU + USL Mip Chain",
                       gpu_android_start_mip_lod);
GPU_ANDROID_WEB_SAMPLE(stencilOutline,
                       "stencil-outline",
                       "GPU + USL Stencil Outline",
                       gpu_android_start_stencil_outline);
GPU_ANDROID_WEB_SAMPLE(bloom,
                       "bloom",
                       "GPU + USL Bloom",
                       gpu_android_start_bloom);
GPU_ANDROID_WEB_SAMPLE(imageTexture,
                       "image-texture",
                       "GPU + USL Image Texture",
                       gpu_android_start_image_texture);
GPU_ANDROID_WEB_SAMPLE(integerCube,
                       "integer-cube",
                       "GPU + USL Integer Cubemap",
                       gpu_android_start_integer_cube);
GPU_ANDROID_WEB_SAMPLE(colorPipeline,
                       "color-pipeline",
                       "GPU + USL Color Pipeline",
                       gpu_android_start_color_pipeline);
GPU_ANDROID_WEB_SAMPLE(compressedTexture,
                       "compressed-texture",
                       "GPU + USL Compressed Texture",
                       gpu_android_start_compressed_texture);
GPU_ANDROID_WEB_SAMPLE(skinning,
                       "skinning",
                       "GPU + USL Skinning",
                       gpu_android_start_skinning);
GPU_ANDROID_WEB_SAMPLE(pbrMaterial,
                       "pbr-material",
                       "GPU + USL PBR Material",
                       gpu_android_start_pbr_material);
GPU_ANDROID_WEB_SAMPLE(assetkitDamagedHelmet,
                       "assetkit-damaged-helmet",
                       "GPU + USL AssetKit DamagedHelmet",
                       gpu_android_start_assetkit_damaged_helmet);
GPU_ANDROID_WEB_SAMPLE(blit,
                       "blit",
                       "GPU + USL Texture Blit",
                       gpu_android_start_blit);

const GPUAndroidSampleDefinition*
GPUSampleAndroidWebDefinition(const char *id) {
  static GPUAndroidSampleDefinition *definitions[] = {
    &triangleDefinition,
    &texturedQuadDefinition,
    &computeDefinition,
    &indexedDepthDefinition,
    &instancingDefinition,
    &texturedCubeDefinition,
    &storageTextureDefinition,
    &pushConstantsDefinition,
    &msaaDefinition,
    &mrtBlendDefinition,
    &shadowCompareDefinition,
    &textureArrayDefinition,
    &textureLineDefinition,
    &textureShapesDefinition,
    &descriptorArrayDefinition,
    &msaaSamplesDefinition,
    &subgroupDefinition,
    &shaderF16Definition,
    &multiDrawDefinition,
    &dispatchIndirectDefinition,
    &timestampQueryDefinition,
    &computeParticlesDefinition,
    &mipLodDefinition,
    &stencilOutlineDefinition,
    &bloomDefinition,
    &imageTextureDefinition,
    &integerCubeDefinition,
    &colorPipelineDefinition,
    &compressedTextureDefinition,
    &skinningDefinition,
    &pbrMaterialDefinition,
    &assetkitDamagedHelmetDefinition,
    &blitDefinition
  };
  const GPUAndroidSampleCallbacks *callbacks;

  callbacks = GPUSampleAndroidWebCallbacks();
  for (uint32_t i = 0u;
       i < sizeof(definitions) / sizeof(definitions[0]);
       i++) {
    GPUAndroidSampleDefinition *definition;

    definition = definitions[i];
    definition->callbacks = callbacks;
    if (id && strcmp(id, definition->id) == 0) {
      return definition;
    }
  }
  return NULL;
}

#undef GPU_ANDROID_WEB_FEATURE_SAMPLE
#undef GPU_ANDROID_WEB_SAMPLE
