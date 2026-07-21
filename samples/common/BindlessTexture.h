#ifndef gpu_sample_bindless_texture_h
#define gpu_sample_bindless_texture_h

#include <gpu/gpu.h>

#include <stdint.h>

enum {
  GPU_SAMPLE_BINDLESS_RESOURCE_COUNT = 2u
};

typedef struct GPUSampleBindlessTexture {
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUSwapchain       *swapchain;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *shaderLayout;
  GPUBindGroupLayout *bindlessLayout;
  GPUPipelineLayout  *pipelineLayout;
  GPURenderPipeline  *pipeline;
  GPUTexture         *textures[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT];
  GPUTextureView     *textureViews[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT];
  GPUBuffer          *selectionBuffers[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT];
  GPUBindGroup       *groups[GPU_SAMPLE_BINDLESS_RESOURCE_COUNT];
  GPUSampler         *sampler;
  uint32_t            width;
  uint32_t            height;
  uint32_t            frameCount;
} GPUSampleBindlessTexture;

GPUResult
GPUSampleBindlessTextureInit(GPUSampleBindlessTexture *state,
                             GPUDevice                *device,
                             GPUQueue                 *queue,
                             GPUSwapchain             *swapchain,
                             GPUShaderLibrary         *library,
                             GPUShaderLayout          *shaderLayout,
                             uint32_t                  width,
                             uint32_t                  height);

GPUResult
GPUSampleBindlessTextureResize(GPUSampleBindlessTexture *state,
                               uint32_t                  width,
                               uint32_t                  height);

GPUResult
GPUSampleBindlessTextureRender(GPUSampleBindlessTexture    *state,
                               void                        *completionSender,
                               GPUCommandBufferCompletionFn completion);

void
GPUSampleBindlessTextureDestroy(GPUSampleBindlessTexture *state);

#endif
