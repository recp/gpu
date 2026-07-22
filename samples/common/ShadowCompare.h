#ifndef gpu_sample_shadow_compare_h
#define gpu_sample_shadow_compare_h

#include <gpu/gpu.h>

#include <stdint.h>

typedef struct GPUSampleShadowCompare {
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *depthPipeline;
  GPURenderPipeline *previewPipeline;
  GPURenderPipeline *previewCoolPipeline;
  GPUTexture        *depthTexture;
  GPUTextureView    *depthView;
  GPUBindGroup      *shadowGroup;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} GPUSampleShadowCompare;

GPUResult
GPUSampleShadowCompareInit(GPUSampleShadowCompare *state,
                           GPUDevice               *device,
                           GPUQueue                *queue,
                           GPUSwapchain            *swapchain,
                           GPUShaderLibrary        *library,
                           GPUShaderLayout         *shaderLayout,
                           uint32_t                 width,
                           uint32_t                 height);

GPUResult
GPUSampleShadowCompareResize(GPUSampleShadowCompare *state,
                             uint32_t                 width,
                             uint32_t                 height);

GPUResult
GPUSampleShadowCompareRender(GPUSampleShadowCompare        *state,
                             void                          *completionSender,
                             GPUCommandBufferCompletionFn   completion);

void
GPUSampleShadowCompareDestroy(GPUSampleShadowCompare *state);

#endif
