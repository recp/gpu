#ifndef gpu_sample_mesh_triangle_h
#define gpu_sample_mesh_triangle_h

#include <gpu/gpu.h>

#include <stdint.h>

typedef struct GPUSampleMeshTriangle {
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBuffer         *taskBuffer;
  GPUBindGroup      *taskGroup;
  uint32_t           width;
  uint32_t           height;
  uint32_t           frameCount;
} GPUSampleMeshTriangle;

GPUResult
GPUSampleMeshTriangleInit(GPUSampleMeshTriangle *state,
                          GPUDevice             *device,
                          GPUQueue              *queue,
                          GPUSwapchain          *swapchain,
                          GPUShaderLibrary      *library,
                          GPUShaderLayout       *shaderLayout,
                          uint32_t               width,
                          uint32_t               height);

GPUResult
GPUSampleMeshTriangleResize(GPUSampleMeshTriangle *state,
                            uint32_t               width,
                            uint32_t               height);

GPUResult
GPUSampleMeshTriangleRender(GPUSampleMeshTriangle        *state,
                            void                         *completionSender,
                            GPUCommandBufferCompletionFn  completion);

void
GPUSampleMeshTriangleDestroy(GPUSampleMeshTriangle *state);

#endif
