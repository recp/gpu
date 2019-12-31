/*
* Copyright (c), Recep Aslantas.
*
* MIT License (MIT), http://opensource.org/licenses/MIT
* Full license can be found in the LICENSE file
*/

#ifndef gpu_cmd_enc_h
#define gpu_cmd_enc_h

#include "common.h"
#include "pass.h"
#include "pipeline.h"
#include "depthstencil.h"

typedef struct GPURenderCommandEncoder GPURenderCommandEncoder;

typedef enum GPUPrimitiveType {
  GPUPrimitiveTypePoint         = 0,
  GPUPrimitiveTypeLine          = 1,
  GPUPrimitiveTypeLineStrip     = 2,
  GPUPrimitiveTypeTriangle      = 3,
  GPUPrimitiveTypeTriangleStrip = 4
} GPUPrimitiveType;

typedef enum GPUVisibilityResultMode {
  GPUVisibilityResultModeDisabled = 0,
  GPUVisibilityResultModeBoolean  = 1,
  GPUVisibilityResultModeCounting = 2
} GPUVisibilityResultMode;

typedef struct GPUScissorRect {
  uint32_t x, y, width, height;
} GPUScissorRect;

typedef struct GPUViewport {
  double originX, originY, width, height, znear, zfar;
} GPUViewport;

typedef enum GPUCullMode {
  GPUCullModeNone  = 0,
  GPUCullModeFront = 1,
  GPUCullModeBack  = 2
} GPUCullMode;

typedef enum GPUWinding {
  GPUWindingClockwise        = 0,
  GPUWindingCounterClockwise = 1
} GPUWinding;

typedef enum GPUDepthClipMode {
  GPUDepthClipModeClip  = 0,
  GPUDepthClipModeClamp = 1
} GPUDepthClipMode;

typedef enum GPUTriangleFillMode {
  GPUTriangleFillModeFill  = 0,
  GPUTriangleFillModeLines = 1
} GPUTriangleFillMode;

typedef enum GPURenderStages {
  GPURenderStageVertex   = 0,
  GPURenderStageFragment = 1
} GPURenderStages;

GPU_EXPORT
GPURenderCommandEncoder*
gpuRenderCommandEncoder(GPUCommandBuffer  * __restrict cmdb,
                        GPURenderPassDesc * __restrict passDesc);

GPU_EXPORT
void
gpuFrontFace(GPUCommandBuffer *cmdb,
             GPUWinding        winding);

GPU_EXPORT
void
gpuCullMode(GPUCommandBuffer *cmdb,
            GPUCullMode       mode);

GPU_EXPORT
void
gpuSetRenderPipeline(GPUCommandBuffer *cmdb,
                     GPUPipeline      *rs);

GPU_EXPORT
void
gpuSetDepthStencil(GPUCommandBuffer *cmdb,
                   GPUDepthStencil  *ds);

#endif /* gpu_cmd_enc_h */
