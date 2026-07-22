#ifndef gpu_sample_vrs_compare_h
#define gpu_sample_vrs_compare_h

#include <gpu/gpu.h>

#include <stdint.h>

typedef struct GPUSampleVRSCompare {
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPURenderPipeline  *finePipeline;
  GPURenderPipeline  *coarsePipeline;
  GPUBuffer          *vertexBuffer;
  GPUTexture         *rateTexture;
  GPUTextureView     *rateView;
  GPUExtent2D         attachmentTexelSize;
  GPUVRSModeFlagsEXT  mode;
  GPUShadingRateEXT   coarseRate;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
} GPUSampleVRSCompare;

GPUResult
GPUSampleChooseVRSRate(const GPUAdapter *adapter,
                       GPUShadingRateEXT *outRate);

GPUResult
GPUSampleChooseVRSAttachment(const GPUAdapter *adapter,
                             GPUShadingRateEXT *outRate,
                             GPUExtent2D       *outTexelSize);

GPUResult
GPUSampleVRSCompareInit(GPUSampleVRSCompare *state,
                        GPUDevice           *device,
                        GPUQueue            *queue,
                        GPUSwapchain        *swapchain,
                        GPUShaderLibrary    *library,
                        GPUShaderLayout     *shaderLayout,
                        GPUVRSModeFlagsEXT   mode,
                        GPUShadingRateEXT    coarseRate,
                        GPUExtent2D          attachmentTexelSize,
                        uint32_t             width,
                        uint32_t             height);

GPUResult
GPUSampleVRSCompareResize(GPUSampleVRSCompare *state,
                          uint32_t             width,
                          uint32_t             height);

GPUResult
GPUSampleVRSCompareRender(GPUSampleVRSCompare          *state,
                          void                         *completionSender,
                          GPUCommandBufferCompletionFn  completion);

void
GPUSampleVRSCompareDestroy(GPUSampleVRSCompare *state);

#endif
