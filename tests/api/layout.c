#include <gpu/gpu.h>

#include <stddef.h>

#define GPU_ASSERT_CHAIN_FIRST(TYPE) \
  _Static_assert(offsetof(TYPE, chain) == 0u, #TYPE ".chain must be first")
#define GPU_ASSERT_64BIT_SIZE(TYPE, SIZE) \
  _Static_assert(sizeof(void *) != 8u || sizeof(TYPE) == (SIZE), \
                 #TYPE " 64-bit ABI size changed")

GPU_ASSERT_CHAIN_FIRST(GPUInstanceCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUSurfaceCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUNativeSurfaceCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUDeviceQueueCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUDeviceCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUBufferCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUTextureCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUTextureViewCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUHeapCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUSamplerCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUShaderLibraryCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUSwapchainCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUBindGroupLayoutCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUBindlessLayoutEXT);
GPU_ASSERT_CHAIN_FIRST(GPUBindGroupCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUPipelineLayoutCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUQueueSubmitInfo);
GPU_ASSERT_CHAIN_FIRST(GPUQueueSubmitExInfo);
GPU_ASSERT_CHAIN_FIRST(GPUFenceCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUSemaphoreCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPURenderPassCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPURenderPipelineCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUComputePipelineCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUPipelineCacheCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPURuntimeConfig);
GPU_ASSERT_CHAIN_FIRST(GPUTransientAllocatorConfig);
GPU_ASSERT_CHAIN_FIRST(GPUQuerySetCreateInfo);
GPU_ASSERT_CHAIN_FIRST(GPUDynamicStateApplyInfo);
GPU_ASSERT_CHAIN_FIRST(GPUMeshPipelineEXT);
GPU_ASSERT_CHAIN_FIRST(GPUShadingRateAttachmentEXT);
GPU_ASSERT_CHAIN_FIRST(GPURasterizationRateMapCreateInfoEXT);
GPU_ASSERT_CHAIN_FIRST(GPURasterizationRateMapRenderPassEXT);
GPU_ASSERT_CHAIN_FIRST(GPUAccelerationStructureBuildInfoEXT);
GPU_ASSERT_CHAIN_FIRST(GPUAccelerationStructureCreateInfoEXT);

_Static_assert(
  offsetof(GPUTransientAllocatorConfig, ringBytesPerFrame) <
    offsetof(GPUTransientAllocatorConfig, chunkBytes),
  "transient allocator 64-bit fields must stay packed"
);
_Static_assert(
  offsetof(GPUTransientAllocatorConfig, chunkBytes) <
    offsetof(GPUTransientAllocatorConfig, framesInFlight),
  "transient allocator 64-bit fields must stay packed"
);
GPU_ASSERT_64BIT_SIZE(GPUMemoryRequirements, 24u);
GPU_ASSERT_64BIT_SIZE(GPUHeapCreateInfo, 56u);
GPU_ASSERT_64BIT_SIZE(GPUSparseTextureRequirements, 48u);
GPU_ASSERT_64BIT_SIZE(GPUSparseBufferRequirements, 24u);
GPU_ASSERT_64BIT_SIZE(GPUSparseBufferMapping, 48u);
GPU_ASSERT_64BIT_SIZE(GPUSparseTextureMapping, 64u);
GPU_ASSERT_64BIT_SIZE(GPUQueueSparseSubmitInfo, 72u);
GPU_ASSERT_64BIT_SIZE(GPUAliasingBarrier, 32u);
GPU_ASSERT_64BIT_SIZE(GPUBarrierBatch, 48u);
GPU_ASSERT_64BIT_SIZE(GPUQueueSubmitExInfo, 64u);
GPU_ASSERT_64BIT_SIZE(GPUDepthStencilState, 48u);
GPU_ASSERT_64BIT_SIZE(GPUTransientAllocatorConfig, 40u);
GPU_ASSERT_64BIT_SIZE(GPUShaderReflection, 16u);
GPU_ASSERT_64BIT_SIZE(GPUShaderLibraryCreateInfo, 72u);
GPU_ASSERT_64BIT_SIZE(GPURenderPipelineCreateInfo, 128u);
GPU_ASSERT_64BIT_SIZE(GPUSubgroupMatrixPropertiesEXT, 40u);
GPU_ASSERT_64BIT_SIZE(GPUIndirectMemoryCopyCommandEXT, 24u);
GPU_ASSERT_64BIT_SIZE(GPUIndirectTextureSubresourceEXT, 16u);
GPU_ASSERT_64BIT_SIZE(GPUIndirectMemoryToTextureCommandEXT, 56u);
GPU_ASSERT_64BIT_SIZE(GPUIndirectCommandRangeEXT, 32u);
GPU_ASSERT_64BIT_SIZE(GPUIndirectMemoryCopyInfoEXT, 48u);
GPU_ASSERT_64BIT_SIZE(GPUIndirectMemoryToTextureCopyInfoEXT, 56u);
_Static_assert(
  offsetof(GPUQueueSubmitExInfo, ppCommandBuffers) <
    offsetof(GPUQueueSubmitExInfo, pWaits),
  "submit pointers must follow command, wait, signal order"
);
_Static_assert(
  offsetof(GPUQueueSubmitExInfo, pWaits) <
    offsetof(GPUQueueSubmitExInfo, pSignals),
  "submit pointers must follow command, wait, signal order"
);
_Static_assert(
  offsetof(GPUQueueSubmitExInfo, commandBufferCount) <
    offsetof(GPUQueueSubmitExInfo, waitCount),
  "submit counts must follow command, wait, signal order"
);
_Static_assert(
  offsetof(GPUQueueSubmitExInfo, waitCount) <
    offsetof(GPUQueueSubmitExInfo, signalCount),
  "submit counts must follow command, wait, signal order"
);

int
main(void) {
  return 0;
}
