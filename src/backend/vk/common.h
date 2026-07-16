/*
 * Copyright (C) 2020 Recep Aslantas
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

#ifndef vk_common_h
#define vk_common_h

#include "../common.h"
#include "../../api/adapter_internal.h"
#include "../../api/cmdqueue_internal.h"
#include "../../api/device_internal.h"
#include "../../api/frame_internal.h"
#include "../../api/instance_internal.h"
#include "../../api/library_internal.h"
#include "../../api/ray_internal.h"
#include "../../api/surface_internal.h"
#include "../../api/swapchain_internal.h"
#include "../../api/texture_internal.h"
#include <stdarg.h>
#include <assert.h>
#include <inttypes.h>

/* MoltenVK */
#ifdef __APPLE__
#  include <Availability.h>
#  define VK_USE_PLATFORM_METAL_EXT        1
#  define VK_ENABLE_BETA_EXTENSIONS        1    // VK_KHR_portability_subset
#  ifdef __IPHONE_OS_VERSION_MAX_ALLOWED
#    define VK_USE_PLATFORM_IOS_MVK        1
#  endif
#  ifdef __MAC_OS_X_VERSION_MAX_ALLOWED
#    define VK_USE_PLATFORM_MACOS_MVK      1
#  endif
#elif defined(_WIN32) || defined(WIN32)
#  define VK_USE_PLATFORM_WIN32_KHR        1
#endif

#ifdef ANDROID
#  include "vulkan_wrapper.h"
#else
#  include <vulkan/vulkan.h>
#endif

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#include "object_type_string_helper.h"

#define APP_SHORT_NAME "libgpu"
#define APP_LONG_NAME  "libgpu"

enum {
  GPU_VK_MAX_DYNAMIC_OFFSETS          = 64u,
  GPU_VK_DESCRIPTOR_POOL_TYPE_COUNT   = 8u
};

#if defined(NDEBUG) && defined(__GNUC__)
#  define U_ASSERT_ONLY __attribute__((unused))
#else
#  define U_ASSERT_ONLY
#endif

#if defined(__GNUC__)
#  define UNUSED __attribute__((unused))
#else
#  define UNUSED
#endif

#ifdef _WIN32
bool in_callback = false;
#define ERR_EXIT(err_msg, err_class)                                             \
    do {                                                                         \
        if (!demo->suppress_popups) MessageBox(NULL, err_msg, err_class, MB_OK); \
        exit(1);                                                                 \
    } while (0)
void DbgMsg(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    fflush(stdout);
}

#elif defined __ANDROID__
#include <android/log.h>
#define ERR_EXIT(err_msg, err_class)                                           \
    do {                                                                       \
        ((void)__android_log_print(ANDROID_LOG_INFO, "Vulkan Cube", err_msg)); \
        exit(1);                                                               \
    } while (0)
#ifdef VARARGS_WORKS_ON_ANDROID
void DbgMsg(const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    __android_log_print(ANDROID_LOG_INFO, "Vulkan Cube", fmt, va);
    va_end(va);
}
#else  // VARARGS_WORKS_ON_ANDROID
#define DbgMsg(fmt, ...)                                                                  \
    do {                                                                                  \
        ((void)__android_log_print(ANDROID_LOG_INFO, "Vulkan Cube", fmt, ##__VA_ARGS__)); \
    } while (0)
#endif  // VARARGS_WORKS_ON_ANDROID
#else
#define ERR_EXIT(err_msg, err_class) \
    do {                             \
        printf("%s\n", err_msg);     \
        fflush(stdout);              \
        exit(1);                     \
    } while (0)
GPU_INLINE void DbgMsg(char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    fflush(stdout);
}
#endif

#define GET_INSTANCE_PROC_ADDR(gpuInstVk, entrypoint)                         \
  gpuInstVk->fp##entrypoint = (PFN_vk##entrypoint)                            \
      vkGetInstanceProcAddr(gpuInstVk->inst, "vk" #entrypoint);               \

typedef struct GPUInstanceVk {
  char       *extensionNames[64];
  char       *enabledLayers[64];
  VkInstance  inst;
  uint32_t    apiVersion;
  uint32_t    nEnabledExtensions;
  uint32_t    nEnabledLayers;
  int32_t     gpu_number;
  bool        invalid_gpu_selection;
#if GPU_BUILD_WITH_VALIDATION || GPU_BUILD_WITH_DEBUG_MARKERS
  bool        debugUtilsEnabled;
#endif

  PFN_vkGetPhysicalDeviceSurfaceSupportKHR      fpGetPhysicalDeviceSurfaceSupportKHR;
  PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
  PFN_vkGetPhysicalDeviceSurfaceFormatsKHR      fpGetPhysicalDeviceSurfaceFormatsKHR;
  PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
  PFN_vkCreateSwapchainKHR                      fpCreateSwapchainKHR;
  PFN_vkDestroySwapchainKHR                     fpDestroySwapchainKHR;
  PFN_vkGetSwapchainImagesKHR                   fpGetSwapchainImagesKHR;
  PFN_vkAcquireNextImageKHR                     fpAcquireNextImageKHR;
  PFN_vkQueuePresentKHR                         fpQueuePresentKHR;
  PFN_vkGetRefreshCycleDurationGOOGLE           fpGetRefreshCycleDurationGOOGLE;
  PFN_vkGetPastPresentationTimingGOOGLE         fpGetPastPresentationTimingGOOGLE;

#if GPU_BUILD_WITH_VALIDATION
  PFN_vkCreateDebugUtilsMessengerEXT            CreateDebugUtilsMessengerEXT;
  PFN_vkDestroyDebugUtilsMessengerEXT           DestroyDebugUtilsMessengerEXT;
  PFN_vkSubmitDebugUtilsMessageEXT              SubmitDebugUtilsMessageEXT;
  VkDebugUtilsMessengerEXT                      dbg_messenger;
#endif
#if GPU_BUILD_WITH_DEBUG_MARKERS
  PFN_vkCmdBeginDebugUtilsLabelEXT              CmdBeginDebugUtilsLabelEXT;
  PFN_vkCmdEndDebugUtilsLabelEXT                CmdEndDebugUtilsLabelEXT;
  PFN_vkCmdInsertDebugUtilsLabelEXT             CmdInsertDebugUtilsLabelEXT;
  PFN_vkSetDebugUtilsObjectNameEXT              SetDebugUtilsObjectNameEXT;
#endif
} GPUInstanceVk;

typedef struct GPUAdapterVk {
  char                         *extensionNames[64];
  VkQueueFamilyProperties      *queueFamilyProps;
#ifdef VK_KHR_cooperative_matrix
  PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR
                                getCooperativeMatrixProperties;
#endif
  VkPhysicalDevice              physicalDevice;
  VkSubgroupFeatureFlags        subgroupOperations;
  VkShaderStageFlags            subgroupStages;
#ifdef VK_KHR_cooperative_matrix
  VkShaderStageFlags            subgroupMatrixStages;
#endif
  VkPhysicalDeviceProperties    props;
  VkPhysicalDeviceFeatures      features;
  VkDisplayPropertiesKHR        displayProps;
  GPUShadingRateFlagsEXT         vrsRates;
  GPUShadingRateCombinerFlagsEXT vrsCombiners;
  VkExtent2D                    minVRSTexelSize;
  VkExtent2D                    maxVRSTexelSize;
  uint32_t                      nQueFamilies;
  uint32_t                      nEnabledExtensions;
  uint32_t                      subgroupSize;
  uint32_t                      minSubgroupSize;
  uint32_t                      maxSubgroupSize;
  uint32_t                      nDisplayProperties;
  uint32_t                      maxVRSTexelAspectRatio;
  uint32_t                      accelerationStructureScratchAlignment;
  bool                          dynamicRendering;
  bool                          shaderFloat16;
  bool                          storageBuffer16BitAccess;
  bool                          vulkanMemoryModel;
  bool                          descriptorIndexing;
  bool                          bindless;
  bool                          meshShader;
  bool                          taskShader;
  bool                          vrsDrawRate;
  bool                          vrsAttachment;
  bool                          timelineSemaphore;
  bool                          synchronization2;
  bool                          rayQuery;
  bool                          subgroupMatrix;
  bool                          atomic64;
  bool                          negativeViewport;
} GPUAdapterVk;

typedef struct GPUDeviceVk {
  GPUQueue                  **createdQueues;
  PFN_vkCmdBeginRenderingKHR  beginRendering;
  PFN_vkCmdEndRenderingKHR    endRendering;
  PFN_vkCmdPipelineBarrier2KHR pipelineBarrier2;
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  PFN_vkCreateAccelerationStructureKHR createAccelerationStructure;
  PFN_vkDestroyAccelerationStructureKHR destroyAccelerationStructure;
  PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationStructureBuildSizes;
  PFN_vkCmdBuildAccelerationStructuresKHR buildAccelerationStructures;
  PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationStructureAddress;
  PFN_vkGetBufferDeviceAddress getBufferDeviceAddress;
#endif
#ifdef VK_EXT_mesh_shader
  PFN_vkCmdDrawMeshTasksEXT    drawMeshTasks;
#endif
#ifdef VK_KHR_fragment_shading_rate
  PFN_vkCmdSetFragmentShadingRateKHR setFragmentShadingRate;
#endif
  VkDevice                   device;
  VkSampleCountFlags         colorSampleCounts;
  VkSampleCountFlags         depthSampleCounts;
  GPUShadingRateFlagsEXT         vrsRates;
  GPUShadingRateCombinerFlagsEXT vrsCombiners;
  VkExtent2D                 minVRSTexelSize;
  VkExtent2D                 maxVRSTexelSize;
  uint32_t                   nCreatedQueues;
  uint32_t                   maxDrawIndirectCount;
  uint32_t                   maxVRSTexelAspectRatio;
  uint32_t                   accelerationStructureScratchAlignment;
  VkBool32                   multiDrawIndirect;
  VkBool32                   independentBlend;
  bool                       dynamicRendering;
  bool                       meshShader;
  bool                       taskShader;
  bool                       vrsDrawRate;
  bool                       vrsAttachment;
  bool                       timelineSemaphore;
  bool                       synchronization2;
  bool                       rayQuery;
} GPUDeviceVk;

#if GPU_BUILD_WITH_DEBUG_MARKERS
static inline GPUInstanceVk *
vk_debugInstance(GPUDevice *device) {
  return device && device->inst ? device->inst->_priv : NULL;
}

static inline bool
vk_beginDebugLabel(GPUDevice      *device,
                   VkCommandBuffer command,
                   const char     *label) {
  GPUInstanceVk        *instance;
  VkDebugUtilsLabelEXT info = {0};

  instance = vk_debugInstance(device);
  if (!gpuDeviceDebugMarkersEnabled(device) || !command ||
      !label || label[0] == '\0' || !instance ||
      !instance->CmdBeginDebugUtilsLabelEXT) {
    return false;
  }

  info.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
  info.pLabelName = label;
  instance->CmdBeginDebugUtilsLabelEXT(command, &info);
  return true;
}

static inline void
vk_endDebugLabel(GPUDevice *device, VkCommandBuffer command) {
  GPUInstanceVk *instance;

  instance = vk_debugInstance(device);
  if (command && instance && instance->CmdEndDebugUtilsLabelEXT) {
    instance->CmdEndDebugUtilsLabelEXT(command);
  }
}

static inline void
vk_setDebugName(GPUDevice   *device,
                VkObjectType objectType,
                uint64_t     objectHandle,
                const char  *label) {
  GPUInstanceVk                 *instance;
  GPUDeviceVk                   *deviceVk;
  VkDebugUtilsObjectNameInfoEXT  info = {0};

  instance = vk_debugInstance(device);
  deviceVk = device ? device->_priv : NULL;
  if (!gpuDeviceDebugMarkersEnabled(device) || objectHandle == 0u ||
      !label || label[0] == '\0' || !instance || !deviceVk ||
      !instance->SetDebugUtilsObjectNameEXT) {
    return;
  }

  info.sType        = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  info.objectType   = objectType;
  info.objectHandle = objectHandle;
  info.pObjectName  = label;
  instance->SetDebugUtilsObjectNameEXT(deviceVk->device, &info);
}
#else
#  define vk_beginDebugLabel(device, command, label) false
#  define vk_endDebugLabel(device, command) ((void)0)
#  define vk_setDebugName(device, objectType, objectHandle, label) ((void)0)
#endif

typedef struct GPUBufferVk {
  void          *mapped;
  VkDevice       device;
  VkBuffer       buffer;
  VkDeviceMemory memory;
  VkDeviceSize   allocationSize;
  bool           coherent;
} GPUBufferVk;

#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
typedef struct GPUAccelerationStructureVk {
  GPUDeviceVk                                *gpuDevice;
  GPUBuffer                                  *instanceBuffer;
  VkAccelerationStructureGeometryKHR        *geometries;
  VkAccelerationStructureBuildRangeInfoKHR  *ranges;
  VkDevice                                    device;
  VkBuffer                                    buffer;
  VkDeviceMemory                              memory;
  VkAccelerationStructureKHR                  structure;
  VkDeviceAddress                             address;
  uint64_t                                    instanceCapacity;
  uint32_t                                    geometryCapacity;
} GPUAccelerationStructureVk;

typedef struct GPUAccelerationStructureEncoderVk {
  VkCommandBuffer command;
  bool            debugLabelActive;
} GPUAccelerationStructureEncoderVk;
#endif

#ifdef __APPLE__
typedef struct GPUTransferChunkVk {
  GPUBuffer                 *buffer;
  struct GPUTransferChunkVk *next;
  uint64_t                   offset;
  uint64_t                   capacity;
} GPUTransferChunkVk;
#endif

typedef struct GPUTextureVk {
  GPUDeviceVk       *gpuDevice;
  VkImageLayout     *layouts;
  VkDevice           device;
  VkImage            image;
  VkDeviceMemory     memory;
  VkRenderPass       renderPasses[3][2];
  VkImageLayout      layout;
  VkImageAspectFlags aspect;
  uint32_t           mipLevelCount;
  uint32_t           arrayLayerCount;
  uint32_t           subresourceCount;
  bool               layoutUniform;
} GPUTextureVk;

typedef struct GPUDescriptorPoolVk {
  struct GPUDescriptorPoolVk *next;
  VkDescriptorPool            pool;
} GPUDescriptorPoolVk;

typedef struct GPUBindGroupLayoutVk {
  GPUDescriptorPoolVk  *descriptorPools;
  uint32_t             *dynamicOrder;
  VkSampler            *immutableSamplers;
  VkDevice              device;
  VkDescriptorSetLayout layout;
#if defined(_WIN32) || defined(WIN32)
  CRITICAL_SECTION      poolLock;
#else
  pthread_mutex_t       poolLock;
#endif
  uint32_t              dynamicCount;
  uint32_t              immutableSamplerCount;
  uint32_t              poolSizeCount;
  uint32_t              poolSetCapacity;
  bool                  poolLockInitialized;
  VkDescriptorPoolSize  poolSizes[GPU_VK_DESCRIPTOR_POOL_TYPE_COUNT];
} GPUBindGroupLayoutVk;

typedef struct GPUPipelineLayoutVk {
  VkDevice         device;
  VkPipelineLayout layout;
} GPUPipelineLayoutVk;

typedef struct GPUBindGroupVk {
  GPUBindGroupLayoutVk *layout;
  GPUDescriptorPoolVk  *pool;
  VkDevice               device;
  VkDescriptorSet        set;
} GPUBindGroupVk;

typedef struct GPUSemaphoreVk {
  VkDevice    device;
  VkSemaphore semaphore;
} GPUSemaphoreVk;

typedef struct GPUQueueVk         GPUQueueVk;
typedef struct GPUCommandBufferVk GPUCommandBufferVk;
typedef struct GPUSwapchainVk     GPUSwapchainVk;
typedef struct GPUTextureViewVk   GPUTextureViewVk;

typedef struct GPURenderPassVk {
  GPUSwapchainVk              *swapchain;
  GPUTextureViewVk            *colorViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUTextureViewVk            *resolveViews[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  GPUTextureViewVk            *depthStencilView;
  GPUTextureViewVk            *shadingRateView;
  VkRenderPass                 renderPass;
  VkFramebuffer                framebuffer;
  VkRenderingAttachmentInfoKHR colorAttachments[GPU_RENDER_ENCODER_MAX_COLOR_ATTACHMENTS];
  VkRenderingAttachmentInfoKHR depthAttachment;
  VkRenderingAttachmentInfoKHR stencilAttachment;
#ifdef VK_KHR_fragment_shading_rate
  VkRenderingFragmentShadingRateAttachmentInfoKHR shadingRateAttachment;
#endif
  VkRenderingInfoKHR           renderingInfo;
  VkExtent2D                    extent;
  VkClearValue                 clearValue;
  uint32_t                     colorCount;
  bool                         dynamic;
} GPURenderPassVk;

typedef struct GPURenderEncoderVk {
  GPUDeviceVk     *device;
  GPURenderPassVk *renderPass;
  GPUBuffer       *indexBuffer;
  VkCommandBuffer  command;
  VkPipelineLayout pipelineLayout;
  VkDeviceSize     indexOffset;
  VkExtent2D       extent;
  uint32_t         dynamicOffsets[GPU_VK_MAX_DYNAMIC_OFFSETS];
  GPUIndexType     indexType;
  bool             indexBound;
  bool             debugLabelActive;
} GPURenderEncoderVk;

typedef struct GPUComputeEncoderVk {
  VkCommandBuffer  command;
  VkPipelineLayout pipelineLayout;
  uint32_t         dynamicOffsets[GPU_VK_MAX_DYNAMIC_OFFSETS];
  bool             debugLabelActive;
} GPUComputeEncoderVk;

struct GPUCommandBufferVk {
  GPUQueueVk              *owner;
  GPUCommandBufferVk      *next;
  GPUCommandBufferVk      *poolNext;
  GPUCommandBufferVk      *pendingNext;
  GPUSwapchainVk          *presentSwapchain;
#ifdef __APPLE__
  GPUTransferChunkVk      *transferChunks;
#endif
  VkCommandBuffer          command;
  VkFence                  fence;
  VkFence                  submitFence;
  VkQueryPool              frameTimeQueries;
  GPURenderPassDesc         renderPass;
  GPURenderPassVk           renderPassState;
  GPURenderCommandEncoder   renderEncoder;
  GPURenderEncoderVk        renderState;
  GPUComputePassEncoder     computeEncoder;
  GPUComputeEncoderVk       computeState;
  GPUCopyPassEncoder        copyEncoder;
#if defined(VK_KHR_acceleration_structure) && defined(VK_KHR_ray_query)
  GPUAccelerationStructurePassEncoderEXT rayQueryEncoder;
  GPUAccelerationStructureEncoderVk      rayQueryState;
#endif
  GPUCommandBuffer          commandBuffer;
  uint32_t                  presentImageIndex;
  uint32_t                  presentFrameIndex;
  bool                      frameTimeActive;
  bool                      copyDebugLabelActive;
};

enum {
  GPU_VK_BUFFER_TRANSFER_CAPACITY  = 256u * 1024u,
  GPU_VK_TEXTURE_TRANSFER_CAPACITY = 1024u * 1024u,
  GPU_VK_TRANSFER_SLOT_COUNT       = 8
};

typedef struct GPUTransferSlotVk {
  GPUBuffer       *uploadStaging;
  VkCommandBuffer  command;
  VkFence          fence;
  uint64_t         uploadCapacity;
  uint64_t         uploadUsed;
  bool             pending;
} GPUTransferSlotVk;

struct GPUQueueVk {
  GPUQueue           *queue;
  GPUCommandBufferVk *commands;
  GPUCommandBufferVk *freeCommands;
  GPUCommandBufferVk *pendingHead;
  GPUCommandBufferVk *pendingTail;
  GPUBuffer          *readbackStaging;
  GPUTransferSlotVk   transferSlots[GPU_VK_TRANSFER_SLOT_COUNT];
  VkQueue             queRaw;
  VkCommandPool       commandPool;
#if defined(_WIN32) || defined(WIN32)
  HANDLE              worker;
  CRITICAL_SECTION    poolLock;
  CONDITION_VARIABLE  pendingCondition;
#else
  pthread_t           worker;
  pthread_mutex_t     poolLock;
  pthread_cond_t      pendingCondition;
#endif
  uint32_t            familyIndex;
  uint32_t            queueIndex;
  uint32_t            timestampValidBits;
  uint32_t            inFlightCount;
  uint32_t            activeTransferSlot;
  uint32_t            nextTransferSlot;
  uint64_t            readbackCapacity;
  double              timestampPeriodNs;
  bool                stopping;
  bool                workerStarted;
  bool                transferOpen;
  bool                transferUpload;
};

typedef struct GPUSurfaceVk {
  void        *metalLayer;
  VkInstance   inst;
  VkSurfaceKHR surface;
  uint32_t     formats[GPU_FORMAT_COUNT];
  uint32_t     presentModes[GPU_PRESENT_MODE_IMMEDIATE + 1u];
} GPUSurfaceVk;

typedef struct GPUFrameSyncVk {
  VkSemaphore imageAvailable;
  VkSemaphore renderFinished;
  VkFence     fence;
} GPUFrameSyncVk;

struct GPUTextureViewVk {
  GPUSwapchainVk    *swapchain;
  GPUTextureVk      *texture;
  VkImageLayout     *layout;
  VkDevice           device;
  VkImage            image;
  VkImageView        view;
  VkFramebuffer      framebuffer;
  VkExtent2D         extent;
  VkImageLayout      localLayout;
  VkImageAspectFlags aspect;
  uint32_t           imageIndex;
  uint32_t           baseMip;
  uint32_t           mipCount;
  uint32_t           baseLayer;
  uint32_t           layerCount;
};

typedef struct GPUSamplerVk {
  VkDevice  device;
  VkSampler sampler;
} GPUSamplerVk;

typedef struct GPUShaderLayoutVk {
  VkSampler             *samplers;
  VkDevice               device;
  VkPipelineLayout       layout;
  VkDescriptorSetLayout  samplerLayout;
  VkDescriptorSetLayout  emptyLayout;
  VkDescriptorPool       samplerPool;
  VkDescriptorSet        samplerSet;
  uint32_t               samplerCount;
  uint32_t               samplerGroup;
  bool                   ownsLayout;
} GPUShaderLayoutVk;

GPU_HIDE
void
vk_fillSamplerInfo(const GPUSamplerDesc *desc, VkSamplerCreateInfo *outInfo);

GPU_HIDE
void
vk_fillStaticSamplerInfo(const GPUStaticSamplerDesc *desc,
                         VkSamplerCreateInfo        *outInfo);

GPU_HIDE
GPUResult
vk_createShaderLayout(GPUDevice             *device,
                      GPUPipelineLayout      *layout,
                      const GPUShaderLibrary *library,
                      GPUShaderLayoutVk      *outLayout);

GPU_HIDE
void
vk_destroyShaderLayout(GPUShaderLayoutVk *layout);

GPU_HIDE
void
vk_bindShaderSamplers(VkCommandBuffer          command,
                      VkPipelineBindPoint      bindPoint,
                      const GPUShaderLayoutVk *layout);

typedef struct GPURenderPipelineVk {
  GPUShaderLayoutVk shaderLayout;
  VkDevice          device;
  VkPipeline        pipeline;
  VkRenderPass      renderPass;
} GPURenderPipelineVk;

typedef struct GPUComputePipelineVk {
  GPUShaderLayoutVk shaderLayout;
  VkDevice          device;
  VkPipeline        pipeline;
} GPUComputePipelineVk;

struct GPUSwapchainVk {
  GPUDevice        *gpuDevice;
  GPUSwapchain     *gpuSwapchain;
  GPUQueueVk       *queue;
  GPUSurfaceVk     *surface;
  VkImage          *images;
  VkImageView      *imageViews;
  VkFramebuffer    *framebuffers;
  GPUTexture       *textures;
  GPUTextureView   *textureViews;
  GPUTextureViewVk *nativeViews;
  GPUFrameSyncVk   *frameSync;
  VkDevice          device;
  VkPhysicalDevice  physicalDevice;
  VkSwapchainKHR    swapchain;
  VkRenderPass      renderPasses[3][2];
  GPUFrame          frame;
  VkFormat          format;
  VkExtent2D        extent;
  uint32_t          imageCount;
  uint32_t          requestedImageCount;
  uint32_t          frameIndex;
  uint32_t          acquiredImageIndex;
  uint32_t          inFlightCommandCount;
  GPUFormat         gpuFormat;
  GPUPresentMode    presentMode;
  bool              frameActive;
  bool              frameScheduled;
  bool              frameSubmitted;
};

static inline void
vk_setSwapchainStatus(GPUSwapchainVk *swapchain, VkResult result) {
  GPUSwapchainStatus status;

  switch (result) {
    case VK_SUCCESS:
      status = GPU_SWAPCHAIN_STATUS_READY;
      break;
    case VK_SUBOPTIMAL_KHR:
      status = GPU_SWAPCHAIN_STATUS_SUBOPTIMAL;
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
      status = GPU_SWAPCHAIN_STATUS_OUT_OF_DATE;
      break;
    case VK_ERROR_SURFACE_LOST_KHR:
      status = GPU_SWAPCHAIN_STATUS_SURFACE_LOST;
      break;
    default:
      status = GPU_SWAPCHAIN_STATUS_UNAVAILABLE;
      break;
  }
  gpuSwapchainSetStatus(swapchain ? swapchain->gpuSwapchain : NULL, status);
}

typedef struct GPUShaderLibraryVk {
  VkDevice       device;
  VkShaderModule module;
} GPUShaderLibraryVk;

GPU_HIDE
bool
vk_formatFromGPU(GPUFormat format, VkFormat *outFormat);

GPU_HIDE
bool
vk_findMemoryType(GPUDevice             *device,
                  uint32_t               typeBits,
                  VkMemoryPropertyFlags  required,
                  VkMemoryPropertyFlags  preferred,
                  uint32_t              *outIndex,
                  VkMemoryPropertyFlags *outFlags);

GPU_HIDE
GPUFormat
vk_formatToGPU(VkFormat format);

GPU_HIDE
void
vk_pipelineBarrier(GPUDeviceVk                *device,
                   VkCommandBuffer             command,
                   VkPipelineStageFlags        srcStages,
                   VkPipelineStageFlags        dstStages,
                   uint32_t                    bufferBarrierCount,
                   const VkBufferMemoryBarrier *bufferBarriers,
                   uint32_t                    imageBarrierCount,
                   const VkImageMemoryBarrier  *imageBarriers);

GPU_HIDE
void
vk_transitionView(VkCommandBuffer   command,
                  GPUTextureViewVk *view,
                  VkImageLayout     nextLayout);

GPU_HIDE
bool
vk_transitionTexture(VkCommandBuffer command,
                     GPUTextureVk   *texture,
                     uint32_t        baseMip,
                     uint32_t        mipCount,
                     uint32_t        baseLayer,
                     uint32_t        layerCount,
                     VkImageLayout   nextLayout);

GPU_HIDE
bool
vk_transitionTextureBarrier(VkCommandBuffer      command,
                            GPUTextureVk        *texture,
                            uint32_t             baseMip,
                            uint32_t             mipCount,
                            uint32_t             baseLayer,
                            uint32_t             layerCount,
                            VkImageLayout        nextLayout,
                            VkPipelineStageFlags srcStages,
                            VkPipelineStageFlags dstStages,
                            VkAccessFlags        srcAccess,
                            VkAccessFlags        dstAccess);

GPU_HIDE
void
vk_setTextureLayout(GPUTextureVk  *texture,
                    uint32_t       baseMip,
                    uint32_t       mipCount,
                    uint32_t       baseLayer,
                    uint32_t       layerCount,
                    VkImageLayout  layout);

GPU_HIDE
bool
vk_restoreFrameFence(GPUSwapchainVk *swapchain, GPUFrameSyncVk *sync);

GPU_HIDE
GPUResult
vk_beginTransfer(GPUQueue *queue,
                 bool             upload,
                 uint64_t         sizeBytes,
                 uint64_t         minimumCapacity,
                 VkCommandBuffer *outCommand,
                 GPUBuffer      **outStaging,
                 uint64_t        *outOffset);

GPU_HIDE
GPUResult
vk_submitTransfer(GPUQueue *queue, bool wait);

GPU_HIDE
void
vk_abortTransfer(GPUQueue *queue);

GPU_HIDE
void
vk_waitSwapchainIdle(GPUSwapchainVk *swapchain);

GPU_HIDE
void
vk_resetQuerySet(GPUCommandBuffer *cmdb, GPUQuerySet *set);

#if defined(__APPLE__)
GPU_HIDE
void*
vk_createMetalLayer(void *nativeHandle, GPUSurfaceType type, float scale);

GPU_HIDE
void
vk_resizeMetalLayer(void *metalLayer,
                    uint32_t width,
                    uint32_t height,
                    float scale);

GPU_HIDE
void
vk_destroyMetalLayer(void *metalLayer);
#endif

#endif /* vk_common_h */
