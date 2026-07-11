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
  GPU_VK_MAX_DYNAMIC_OFFSETS = 64u
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

static PFN_vkGetDeviceProcAddr g_gdpa = NULL;

#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                                                                  \
  {                                                                                                            \
      if (!g_gdpa) g_gdpa = (PFN_vkGetDeviceProcAddr)vkGetInstanceProcAddr(demo->inst, "vkGetDeviceProcAddr"); \
      demo->fp##entrypoint = (PFN_vk##entrypoint)g_gdpa(dev, "vk" #entrypoint);                                \
      if (demo->fp##entrypoint == NULL) {                                                                      \
          ERR_EXIT("vkGetDeviceProcAddr failed to find vk" #entrypoint, "vkGetDeviceProcAddr Failure");        \
      }                                                                                                        \
  }

typedef struct GPUInstanceVk {
  char       *extensionNames[64];
  char       *enabledLayers[64];
  VkInstance  inst;
  bool        invalid_gpu_selection;
  int32_t     gpu_number;
  uint32_t    nEnabledExtensions;
  uint32_t    nEnabledLayers;

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

  /* DEBUG helpers */
  PFN_vkCreateDebugUtilsMessengerEXT            CreateDebugUtilsMessengerEXT;
  PFN_vkDestroyDebugUtilsMessengerEXT           DestroyDebugUtilsMessengerEXT;
  PFN_vkSubmitDebugUtilsMessageEXT              SubmitDebugUtilsMessageEXT;
  PFN_vkCmdBeginDebugUtilsLabelEXT              CmdBeginDebugUtilsLabelEXT;
  PFN_vkCmdEndDebugUtilsLabelEXT                CmdEndDebugUtilsLabelEXT;
  PFN_vkCmdInsertDebugUtilsLabelEXT             CmdInsertDebugUtilsLabelEXT;
  PFN_vkSetDebugUtilsObjectNameEXT              SetDebugUtilsObjectNameEXT;
  VkDebugUtilsMessengerEXT                      dbg_messenger;
} GPUInstanceVk;

typedef struct GPUPhysicalDeviceVk {
  char                      *extensionNames[64];
  VkQueueFamilyProperties   *queueFamilyProps;
  VkPhysicalDevice           phyDevice;
  uint32_t                   nQueFamilies;
  uint32_t                   nEnabledExtensions;
  VkPhysicalDeviceProperties props;
  VkPhysicalDeviceFeatures   features;
  uint32_t                   nDisplayProperties;
  VkDisplayPropertiesKHR     displayProps;
} GPUPhysicalDeviceVk;

typedef struct GPUDeviceVk {
  GPUCommandQueue           **createdQueues;
  VkDevice                   device;
  uint32_t                   nCreatedQueues;
  uint32_t                   maxDrawIndirectCount;
  VkBool32                   multiDrawIndirect;
} GPUDeviceVk;

typedef struct GPUBufferVk {
  void          *mapped;
  VkDevice       device;
  VkBuffer       buffer;
  VkDeviceMemory memory;
  VkDeviceSize   allocationSize;
  bool           coherent;
} GPUBufferVk;

typedef struct GPUTextureVk {
  VkDevice           device;
  VkImage            image;
  VkDeviceMemory     memory;
  VkRenderPass       renderPasses[3][2];
  VkImageLayout      layout;
  VkImageAspectFlags aspect;
} GPUTextureVk;

typedef struct GPUBindGroupLayoutVk {
  uint32_t              *dynamicOrder;
  VkDevice               device;
  VkDescriptorSetLayout  layout;
  uint32_t                dynamicCount;
} GPUBindGroupLayoutVk;

typedef struct GPUPipelineLayoutVk {
  VkDevice         device;
  VkPipelineLayout layout;
} GPUPipelineLayoutVk;

typedef struct GPUBindGroupVk {
  VkDevice         device;
  VkDescriptorPool pool;
  VkDescriptorSet  set;
} GPUBindGroupVk;

typedef struct GPUCommandQueueVk  GPUCommandQueueVk;
typedef struct GPUCommandBufferVk GPUCommandBufferVk;
typedef struct GPUSwapChainVk     GPUSwapChainVk;

typedef struct GPURenderPassVk {
  GPUSwapChainVk *swapchain;
  VkRenderPass    renderPass;
  VkFramebuffer   framebuffer;
  VkExtent2D      extent;
  VkClearValue    clearValue;
} GPURenderPassVk;

typedef struct GPURenderEncoderVk {
  GPUDeviceVk     *device;
  GPUBuffer       *indexBuffer;
  VkCommandBuffer  command;
  VkPipelineLayout pipelineLayout;
  VkDeviceSize     indexOffset;
  VkExtent2D       extent;
  uint32_t         dynamicOffsets[GPU_VK_MAX_DYNAMIC_OFFSETS];
  GPUIndexType     indexType;
  bool             indexBound;
} GPURenderEncoderVk;

typedef struct GPUComputeEncoderVk {
  VkCommandBuffer  command;
  VkPipelineLayout pipelineLayout;
  uint32_t         dynamicOffsets[GPU_VK_MAX_DYNAMIC_OFFSETS];
} GPUComputeEncoderVk;

struct GPUCommandBufferVk {
  GPUCommandQueueVk       *owner;
  GPUCommandBufferVk      *next;
  GPUCommandBufferVk      *poolNext;
  GPUCommandBufferVk      *pendingNext;
  GPUSwapChainVk          *presentSwapchain;
  VkCommandBuffer          command;
  VkFence                  fence;
  VkFence                  submitFence;
  GPURenderPassDesc         renderPass;
  GPURenderPassVk           renderPassState;
  GPURenderCommandEncoder   renderEncoder;
  GPURenderEncoderVk        renderState;
  GPUComputePassEncoder     computeEncoder;
  GPUComputeEncoderVk       computeState;
  GPUCommandBuffer          commandBuffer;
  uint32_t                  presentImageIndex;
  uint32_t                  presentFrameIndex;
};

struct GPUCommandQueueVk {
  GPUCommandQueue    *queue;
  GPUCommandBufferVk *commands;
  GPUCommandBufferVk *freeCommands;
  GPUCommandBufferVk *pendingHead;
  GPUCommandBufferVk *pendingTail;
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
  bool                stopping;
  bool                workerStarted;
};

typedef struct GPUSurfaceVk {
  void        *metalLayer;
  VkInstance   inst;
  VkSurfaceKHR surface;
} GPUSurfaceVk;

typedef struct GPUFrameSyncVk {
  VkSemaphore imageAvailable;
  VkSemaphore renderFinished;
  VkFence     fence;
} GPUFrameSyncVk;

typedef struct GPUTextureViewVk {
  GPUSwapChainVk *swapchain;
  GPUTextureVk   *texture;
  VkDevice        device;
  VkImageView     view;
  VkFramebuffer   framebuffer;
  VkExtent2D      extent;
  uint32_t        imageIndex;
} GPUTextureViewVk;

typedef struct GPUSamplerVk {
  VkDevice  device;
  VkSampler sampler;
} GPUSamplerVk;

typedef struct GPURenderPipelineVk {
  VkDevice         device;
  VkPipeline       pipeline;
  VkPipelineLayout layout;
  VkRenderPass     renderPass;
} GPURenderPipelineVk;

typedef struct GPUComputePipelineVk {
  VkDevice         device;
  VkPipeline       pipeline;
  VkPipelineLayout layout;
} GPUComputePipelineVk;

struct GPUSwapChainVk {
  GPUDevice         *gpuDevice;
  GPUCommandQueueVk *queue;
  GPUSurfaceVk      *surface;
  VkImage           *images;
  VkImageView       *imageViews;
  VkFramebuffer     *framebuffers;
  GPUTexture        *textures;
  GPUTextureView    *textureViews;
  GPUTextureViewVk  *nativeViews;
  GPUFrameSyncVk    *frameSync;
  VkDevice           device;
  VkPhysicalDevice   physicalDevice;
  VkSwapchainKHR     swapchain;
  VkRenderPass       renderPasses[3][2];
  GPUFrame           frame;
  VkFormat           format;
  VkExtent2D         extent;
  uint32_t           imageCount;
  uint32_t           requestedImageCount;
  uint32_t           frameIndex;
  uint32_t           acquiredImageIndex;
  uint32_t           inFlightCommandCount;
  GPUFormat          gpuFormat;
  GPUPresentMode     presentMode;
  bool               frameActive;
  bool               frameScheduled;
  bool               frameSubmitted;
};

typedef struct GPULibraryVk {
  VkDevice       device;
  VkShaderModule module;
} GPULibraryVk;

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
bool
vk_restoreFrameFence(GPUSwapChainVk *swapchain, GPUFrameSyncVk *sync);

GPU_HIDE
void
vk_waitSwapChainIdle(GPUSwapChainVk *swapchain);

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
