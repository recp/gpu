#ifndef gpu_sample_mrt_blend_h
#define gpu_sample_mrt_blend_h

#include <gpu/gpu.h>

#include <stdint.h>

typedef struct GPUSampleMRTBlend {
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPURenderPipeline  *mrtPipeline;
  GPURenderPipeline  *compositePipeline;
  GPUTexture         *targets[2];
  GPUTextureView     *targetViews[2];
  GPUSampler         *sampler;
  GPUBindGroup       *compositeGroups[2];
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
} GPUSampleMRTBlend;

GPUResult
GPUSampleMRTBlendInit(GPUSampleMRTBlend *state,
                      GPUDevice         *device,
                      GPUQueue          *queue,
                      GPUSwapchain      *swapchain,
                      GPUShaderLibrary  *library,
                      GPUShaderLayout   *shaderLayout,
                      uint32_t           width,
                      uint32_t           height);

GPUResult
GPUSampleMRTBlendResize(GPUSampleMRTBlend *state,
                        uint32_t           width,
                        uint32_t           height);

GPUResult
GPUSampleMRTBlendRender(GPUSampleMRTBlend           *state,
                        void                        *completionSender,
                        GPUCommandBufferCompletionFn completion);

void
GPUSampleMRTBlendDestroy(GPUSampleMRTBlend *state);

#endif
