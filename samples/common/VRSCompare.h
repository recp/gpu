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
  GPUShadingRateEXT   coarseRate;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
} GPUSampleVRSCompare;

GPUResult
GPUSampleChooseVRSRate(const GPUAdapter *adapter,
                       GPUShadingRateEXT *outRate);

GPUResult
GPUSampleVRSCompareInit(GPUSampleVRSCompare *state,
                        GPUDevice           *device,
                        GPUQueue            *queue,
                        GPUSwapchain        *swapchain,
                        GPUShaderLibrary    *library,
                        GPUShaderLayout     *shaderLayout,
                        GPUShadingRateEXT    coarseRate,
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
