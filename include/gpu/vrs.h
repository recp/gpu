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

#ifndef gpu_vrs_h
#define gpu_vrs_h
#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "geometric.h"

typedef struct GPUAdapter                 GPUAdapter;
typedef struct GPUBuffer                  GPUBuffer;
typedef struct GPUDevice                  GPUDevice;
typedef struct GPUTextureView             GPUTextureView;
typedef struct GPURasterizationRateMapEXT GPURasterizationRateMapEXT;

#ifndef GPU_RENDER_ENCODER_TYPES_DEFINED
#define GPU_RENDER_ENCODER_TYPES_DEFINED
typedef struct GPURenderPassEncoder GPURenderPassEncoder;
#endif

typedef uint32_t GPUVRSModeFlagsEXT;
enum {
  GPU_VRS_DRAW_RATE_BIT_EXT  = 1u << 0,
  GPU_VRS_ATTACHMENT_BIT_EXT = 1u << 1,
  GPU_VRS_RATE_MAP_BIT_EXT   = 1u << 2
};

typedef enum GPUShadingRateEXT {
  /* values are the portable R8_UINT attachment encoding. */
  GPU_SHADING_RATE_1X1_EXT = 0x0,
  GPU_SHADING_RATE_1X2_EXT = 0x1,
  GPU_SHADING_RATE_2X1_EXT = 0x4,
  GPU_SHADING_RATE_2X2_EXT = 0x5,
  GPU_SHADING_RATE_2X4_EXT = 0x6,
  GPU_SHADING_RATE_4X2_EXT = 0x9,
  GPU_SHADING_RATE_4X4_EXT = 0xA
} GPUShadingRateEXT;

typedef uint32_t GPUShadingRateFlagsEXT;
enum {
  GPU_SHADING_RATE_1X1_BIT_EXT = 1u << GPU_SHADING_RATE_1X1_EXT,
  GPU_SHADING_RATE_1X2_BIT_EXT = 1u << GPU_SHADING_RATE_1X2_EXT,
  GPU_SHADING_RATE_2X1_BIT_EXT = 1u << GPU_SHADING_RATE_2X1_EXT,
  GPU_SHADING_RATE_2X2_BIT_EXT = 1u << GPU_SHADING_RATE_2X2_EXT,
  GPU_SHADING_RATE_2X4_BIT_EXT = 1u << GPU_SHADING_RATE_2X4_EXT,
  GPU_SHADING_RATE_4X2_BIT_EXT = 1u << GPU_SHADING_RATE_4X2_EXT,
  GPU_SHADING_RATE_4X4_BIT_EXT = 1u << GPU_SHADING_RATE_4X4_EXT
};

typedef enum GPUShadingRateCombinerEXT {
  GPU_SHADING_RATE_COMBINER_KEEP_EXT = 0,
  GPU_SHADING_RATE_COMBINER_REPLACE_EXT,
  /* MIN/MAX select the component-wise fragment footprint. */
  GPU_SHADING_RATE_COMBINER_MIN_EXT,
  GPU_SHADING_RATE_COMBINER_MAX_EXT
} GPUShadingRateCombinerEXT;

typedef uint32_t GPUShadingRateCombinerFlagsEXT;
enum {
  GPU_SHADING_RATE_COMBINER_KEEP_BIT_EXT    = 1u << GPU_SHADING_RATE_COMBINER_KEEP_EXT,
  GPU_SHADING_RATE_COMBINER_REPLACE_BIT_EXT = 1u << GPU_SHADING_RATE_COMBINER_REPLACE_EXT,
  GPU_SHADING_RATE_COMBINER_MIN_BIT_EXT     = 1u << GPU_SHADING_RATE_COMBINER_MIN_EXT,
  GPU_SHADING_RATE_COMBINER_MAX_BIT_EXT     = 1u << GPU_SHADING_RATE_COMBINER_MAX_EXT
};

typedef struct GPUVRSCapabilitiesEXT {
  GPUVRSModeFlagsEXT             modes;
  GPUShadingRateFlagsEXT         rates;
  GPUShadingRateCombinerFlagsEXT combiners;
  GPUExtent2D                    minAttachmentTexelSize;
  GPUExtent2D                    maxAttachmentTexelSize;
  uint32_t                       maxRateMapLayers;
} GPUVRSCapabilitiesEXT;

typedef struct GPUShadingRateAttachmentEXT {
  GPUChainedStruct chain;
  GPUTextureView  *view;
  GPUExtent2D      texelSize;
} GPUShadingRateAttachmentEXT;

typedef struct GPURasterizationRateLayerEXT {
  const float *pHorizontal;
  const float *pVertical;
  uint32_t     horizontalCount;
  uint32_t     verticalCount;
} GPURasterizationRateLayerEXT;

typedef struct GPURasterizationRateMapCreateInfoEXT {
  GPUChainedStruct                    chain;
  const char                         *label;
  const GPURasterizationRateLayerEXT *pLayers;
  GPUExtent2D                         screenSize;
  uint32_t                            layerCount;
} GPURasterizationRateMapCreateInfoEXT;

typedef struct GPURasterizationRateMapRenderPassEXT {
  GPUChainedStruct             chain;
  GPURasterizationRateMapEXT  *map;
} GPURasterizationRateMapRenderPassEXT;

typedef struct GPURasterizationRateMapParameterInfoEXT {
  uint64_t sizeBytes;
  uint64_t alignment;
} GPURasterizationRateMapParameterInfoEXT;

GPU_EXPORT
GPUResult
GPUGetVRSCapabilitiesEXT(const GPUAdapter      *adapter,
                         GPUVRSCapabilitiesEXT *outCaps);

GPU_EXPORT
GPUResult
GPUCreateRasterizationRateMapEXT(
  GPUDevice                                  *device,
  const GPURasterizationRateMapCreateInfoEXT *info,
  GPURasterizationRateMapEXT                **outMap
);

GPU_EXPORT
void
GPUDestroyRasterizationRateMapEXT(GPURasterizationRateMapEXT *map);

GPU_EXPORT
GPUResult
GPUGetRasterizationRateMapPhysicalSizeEXT(
  const GPURasterizationRateMapEXT *map,
  uint32_t                          layer,
  GPUExtent2D                      *outSize
);

/* maps logical screen coordinates into the physical intermediate target. */
GPU_EXPORT
GPUResult
GPUMapRasterizationRateScreenToPhysicalEXT(
  const GPURasterizationRateMapEXT *map,
  uint32_t                          layer,
  GPUCoordinate2D                   screen,
  GPUCoordinate2D                  *outPhysical
);

/* maps physical intermediate coordinates back into logical screen space. */
GPU_EXPORT
GPUResult
GPUMapRasterizationRatePhysicalToScreenEXT(
  const GPURasterizationRateMapEXT *map,
  uint32_t                          layer,
  GPUCoordinate2D                   physical,
  GPUCoordinate2D                  *outScreen
);

/* returns the size and alignment of shader-visible map parameters. */
GPU_EXPORT
GPUResult
GPUGetRasterizationRateMapParameterInfoEXT(
  const GPURasterizationRateMapEXT         *map,
  GPURasterizationRateMapParameterInfoEXT  *outInfo
);

/* copies parameters to an aligned GPU_BUFFER_USAGE_UNIFORM range. */
GPU_EXPORT
GPUResult
GPUCopyRasterizationRateMapParametersEXT(
  const GPURasterizationRateMapEXT *map,
  GPUBuffer                        *buffer,
  uint64_t                          offset
);

GPU_EXPORT
void
GPUSetFragmentShadingRateEXT(
  GPURenderPassEncoder      *pass,
  GPUShadingRateEXT          rate,
  GPUShadingRateCombinerEXT  primitiveCombiner,
  GPUShadingRateCombinerEXT  attachmentCombiner
);

#ifdef __cplusplus
}
#endif
#endif /* gpu_vrs_h */
