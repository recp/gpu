/*
 * Copyright (C) 2026 Recep Aslantas
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

#include "bench.h"

#include <us/us.h>
#include <vulkan/vulkan.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  NATIVE_DEFAULT_DRAWS    = 1000,
  NATIVE_DEFAULT_WARMUP   = 300,
  NATIVE_DEFAULT_FRAMES   = 3000,
  NATIVE_DEFAULT_REPEATS  = 5,
  NATIVE_STATE_TARGET     = 64,
  NATIVE_BINDING_COUNT    = 2,
  NATIVE_UPLOAD_ALIGNMENT = 256,
  NATIVE_UPLOAD_FRAMES    = 3
};

typedef enum NativeVulkanMode {
  NativeVulkanModeStatic,
  NativeVulkanModeState,
  NativeVulkanModeBinding,
  NativeVulkanModeUpload
} NativeVulkanMode;

typedef struct NativeVulkanConfig {
  const char       *artifactPath;
  NativeVulkanMode  mode;
  uint32_t          drawCount;
  uint32_t          warmupFrames;
  uint32_t          measuredFrames;
  uint32_t          repeats;
} NativeVulkanConfig;

typedef struct NativeVulkanBuffer {
  void           *mapped;
  VkBuffer        buffer;
  VkDeviceMemory  memory;
  VkDeviceSize    size;
} NativeVulkanBuffer;

typedef struct NativeVulkanBench {
  VkInstance             instance;
  VkPhysicalDevice       physicalDevice;
  VkDevice               device;
  VkQueue                queue;
  VkCommandPool          commandPool;
  VkCommandBuffer        commandBuffer;
  VkFence                fence;
  VkQueryPool            queryPool;
  VkRenderPass           renderPass;
  VkImage                target;
  VkDeviceMemory         targetMemory;
  VkImageView            targetView;
  VkFramebuffer          framebuffer;
  VkShaderModule         shaderModule;
  VkDescriptorSetLayout  descriptorSetLayout;
  VkDescriptorPool       descriptorPool;
  VkDescriptorSet        descriptorSets[NATIVE_BINDING_COUNT];
  VkPipelineLayout       pipelineLayout;
  VkPipeline             pipelines[NATIVE_BINDING_COUNT];
  NativeVulkanBuffer     bindingBuffers[NATIVE_BINDING_COUNT];
  NativeVulkanBuffer     vertexBuffer;
  NativeVulkanBuffer     uploadBuffer;
  VkPhysicalDeviceProperties properties;
  BenchProcessMemory     baselineMemory;
  NativeVulkanMode       mode;
  uint64_t               uploadBytesPerFrame;
  uint32_t               queueFamilyIndex;
  uint32_t               targetSize;
  uint32_t               frameIndex;
} NativeVulkanBench;

typedef struct NativeVulkanMetrics {
  double *encodeSamples;
  double *encodeRepeatMedians;
  double *gpuSamples;
  double *gpuRepeatMedians;
  size_t  sampleCount;
} NativeVulkanMetrics;

static const float nativeVertices[] = {
  -1.0f, -1.0f,
   3.0f, -1.0f,
  -1.0f,  3.0f
};

static const float nativeBindingColors[NATIVE_BINDING_COUNT][4] = {
  {1.0f, 0.2f, 0.1f, 1.0f},
  {0.1f, 0.4f, 1.0f, 1.0f}
};

static const char *
native_modeName(NativeVulkanMode mode) {
  static const char *names[] = {
    [NativeVulkanModeStatic]  = "static scene",
    [NativeVulkanModeState]   = "state churn",
    [NativeVulkanModeBinding] = "binding churn",
    [NativeVulkanModeUpload]  = "upload heavy"
  };

  return (uint32_t)mode < sizeof(names) / sizeof(names[0])
           ? names[mode]
           : "unknown";
}

static bool
native_parseMode(const char *text, NativeVulkanMode *outMode) {
  if (!text || !outMode) {
    return false;
  }
  if (strcmp(text, "static") == 0) {
    *outMode = NativeVulkanModeStatic;
  } else if (strcmp(text, "state") == 0) {
    *outMode = NativeVulkanModeState;
  } else if (strcmp(text, "binding") == 0) {
    *outMode = NativeVulkanModeBinding;
  } else if (strcmp(text, "upload") == 0) {
    *outMode = NativeVulkanModeUpload;
  } else {
    return false;
  }
  return true;
}

static bool
native_parseConfig(int argc, char *argv[], NativeVulkanConfig *config) {
  if (!config || argc < 3 || argc > 7) {
    if (argv && argv[0]) {
      fprintf(stderr,
              "usage: %s <static|state|binding|upload> <shader.us> "
              "[draws] [warmup] [frames] [repeats]\n",
              argv[0]);
    }
    return false;
  }

  memset(config, 0, sizeof(*config));
  config->artifactPath   = argv[2];
  config->drawCount      = NATIVE_DEFAULT_DRAWS;
  config->warmupFrames   = NATIVE_DEFAULT_WARMUP;
  config->measuredFrames = NATIVE_DEFAULT_FRAMES;
  config->repeats        = NATIVE_DEFAULT_REPEATS;
  return native_parseMode(argv[1], &config->mode) &&
         (argc <= 3 || bench_parseU32(argv[3], 1u, &config->drawCount)) &&
         (argc <= 4 || bench_parseU32(argv[4], 0u, &config->warmupFrames)) &&
         (argc <= 5 || bench_parseU32(argv[5],
                                     1u,
                                     &config->measuredFrames)) &&
         (argc <= 6 || bench_parseU32(argv[6], 1u, &config->repeats));
}

static bool
native_selectPhysicalDevice(NativeVulkanBench *bench) {
  VkPhysicalDevice *devices;
  uint32_t          deviceCount;

  deviceCount = 0u;
  if (vkEnumeratePhysicalDevices(bench->instance,
                                 &deviceCount,
                                 NULL) != VK_SUCCESS ||
      deviceCount == 0u) {
    return false;
  }
  devices = calloc(deviceCount, sizeof(*devices));
  if (!devices ||
      vkEnumeratePhysicalDevices(bench->instance,
                                 &deviceCount,
                                 devices) != VK_SUCCESS) {
    free(devices);
    return false;
  }

  bench->physicalDevice = devices[0];
  for (uint32_t i = 0u; i < deviceCount; i++) {
    VkPhysicalDeviceProperties properties;

    vkGetPhysicalDeviceProperties(devices[i], &properties);
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      bench->physicalDevice = devices[i];
      break;
    }
  }
  free(devices);
  vkGetPhysicalDeviceProperties(bench->physicalDevice, &bench->properties);
  return true;
}

static bool
native_selectQueueFamily(NativeVulkanBench *bench) {
  VkQueueFamilyProperties *families;
  uint32_t                 familyCount;
  bool                     found;

  familyCount = 0u;
  vkGetPhysicalDeviceQueueFamilyProperties(bench->physicalDevice,
                                           &familyCount,
                                           NULL);
  if (familyCount == 0u) {
    return false;
  }
  families = calloc(familyCount, sizeof(*families));
  if (!families) {
    return false;
  }
  vkGetPhysicalDeviceQueueFamilyProperties(bench->physicalDevice,
                                           &familyCount,
                                           families);
  found = false;
  for (uint32_t i = 0u; i < familyCount; i++) {
    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0u) {
      bench->queueFamilyIndex = i;
      found = true;
      break;
    }
  }
  free(families);
  return found;
}

static bool
native_hasInstanceExtension(const char *name) {
  VkExtensionProperties *extensions;
  uint32_t               count;
  bool                   found;

  count = 0u;
  if (vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) !=
      VK_SUCCESS) {
    return false;
  }
  extensions = count ? calloc(count, sizeof(*extensions)) : NULL;
  if (count > 0u && !extensions) {
    return false;
  }
  if (count > 0u &&
      vkEnumerateInstanceExtensionProperties(NULL, &count, extensions) !=
        VK_SUCCESS) {
    free(extensions);
    return false;
  }
  found = false;
  for (uint32_t i = 0u; i < count; i++) {
    if (strcmp(extensions[i].extensionName, name) == 0) {
      found = true;
      break;
    }
  }
  free(extensions);
  return found;
}

#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
static bool
native_hasDeviceExtension(NativeVulkanBench *bench, const char *name) {
  VkExtensionProperties *extensions;
  uint32_t               count;
  bool                   found;

  count = 0u;
  if (vkEnumerateDeviceExtensionProperties(bench->physicalDevice,
                                           NULL,
                                           &count,
                                           NULL) != VK_SUCCESS) {
    return false;
  }
  extensions = count ? calloc(count, sizeof(*extensions)) : NULL;
  if (count > 0u && !extensions) {
    return false;
  }
  if (count > 0u &&
      vkEnumerateDeviceExtensionProperties(bench->physicalDevice,
                                           NULL,
                                           &count,
                                           extensions) != VK_SUCCESS) {
    free(extensions);
    return false;
  }
  found = false;
  for (uint32_t i = 0u; i < count; i++) {
    if (strcmp(extensions[i].extensionName, name) == 0) {
      found = true;
      break;
    }
  }
  free(extensions);
  return found;
}
#endif

static bool
native_createDevice(NativeVulkanBench *bench) {
  VkDeviceQueueCreateInfo queueInfo;
  VkDeviceCreateInfo      deviceInfo;
  const char             *extensions[1];
  float                   priority;
  uint32_t                extensionCount;

  memset(&queueInfo, 0, sizeof(queueInfo));
  memset(&deviceInfo, 0, sizeof(deviceInfo));
  extensionCount              = 0u;
  priority                    = 1.0f;
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
  if (native_hasDeviceExtension(
        bench,
        VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
    extensions[extensionCount++] =
      VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME;
  }
#endif
  queueInfo.sType             = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex  = bench->queueFamilyIndex;
  queueInfo.queueCount        = 1u;
  queueInfo.pQueuePriorities  = &priority;
  deviceInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.queueCreateInfoCount = 1u;
  deviceInfo.pQueueCreateInfos     = &queueInfo;
  deviceInfo.enabledExtensionCount = extensionCount;
  deviceInfo.ppEnabledExtensionNames = extensions;
  if (vkCreateDevice(bench->physicalDevice,
                     &deviceInfo,
                     NULL,
                     &bench->device) != VK_SUCCESS) {
    return false;
  }
  vkGetDeviceQueue(bench->device,
                   bench->queueFamilyIndex,
                   0u,
                   &bench->queue);
  return bench->queue != VK_NULL_HANDLE;
}

static bool
native_createContext(NativeVulkanBench *bench) {
  VkApplicationInfo applicationInfo;
  VkInstanceCreateInfo instanceInfo;
  const char          *extensions[1];
  uint32_t             extensionCount;

  memset(&applicationInfo, 0, sizeof(applicationInfo));
  memset(&instanceInfo, 0, sizeof(instanceInfo));
  extensionCount                         = 0u;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
  if (native_hasInstanceExtension(
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
    extensions[extensionCount++] =
      VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
    instanceInfo.flags |=
      VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
#endif
  applicationInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  applicationInfo.pApplicationName   = "gpu-native-vulkan-benchmark";
  applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  applicationInfo.pEngineName        = "gpu-benchmark";
  applicationInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
  applicationInfo.apiVersion         = VK_API_VERSION_1_0;
  instanceInfo.sType                 = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.pApplicationInfo      = &applicationInfo;
  instanceInfo.enabledExtensionCount = extensionCount;
  instanceInfo.ppEnabledExtensionNames = extensions;
  return vkCreateInstance(&instanceInfo,
                          NULL,
                          &bench->instance) == VK_SUCCESS &&
         native_selectPhysicalDevice(bench) &&
         native_selectQueueFamily(bench) &&
         native_createDevice(bench);
}

static bool
native_memoryType(NativeVulkanBench *bench,
                  uint32_t           typeBits,
                  VkMemoryPropertyFlags required,
                  uint32_t          *outIndex) {
  VkPhysicalDeviceMemoryProperties properties;

  vkGetPhysicalDeviceMemoryProperties(bench->physicalDevice, &properties);
  for (uint32_t i = 0u; i < properties.memoryTypeCount; i++) {
    if ((typeBits & (1u << i)) != 0u &&
        (properties.memoryTypes[i].propertyFlags & required) == required) {
      *outIndex = i;
      return true;
    }
  }
  return false;
}

static bool
native_createBuffer(NativeVulkanBench  *bench,
                    VkDeviceSize        size,
                    VkBufferUsageFlags  usage,
                    NativeVulkanBuffer *outBuffer) {
  VkBufferCreateInfo    bufferInfo;
  VkMemoryAllocateInfo  allocationInfo;
  VkMemoryRequirements  requirements;
  uint32_t              memoryType;

  memset(outBuffer, 0, sizeof(*outBuffer));
  memset(&bufferInfo, 0, sizeof(bufferInfo));
  memset(&allocationInfo, 0, sizeof(allocationInfo));
  bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size        = size;
  bufferInfo.usage       = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(bench->device,
                     &bufferInfo,
                     NULL,
                     &outBuffer->buffer) != VK_SUCCESS) {
    return false;
  }
  vkGetBufferMemoryRequirements(bench->device,
                                outBuffer->buffer,
                                &requirements);
  if (!native_memoryType(bench,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         &memoryType)) {
    return false;
  }
  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryType;
  if (vkAllocateMemory(bench->device,
                       &allocationInfo,
                       NULL,
                       &outBuffer->memory) != VK_SUCCESS ||
      vkBindBufferMemory(bench->device,
                         outBuffer->buffer,
                         outBuffer->memory,
                         0u) != VK_SUCCESS ||
      vkMapMemory(bench->device,
                  outBuffer->memory,
                  0u,
                  size,
                  0u,
                  &outBuffer->mapped) != VK_SUCCESS) {
    return false;
  }
  outBuffer->size = size;
  return true;
}

static void
native_destroyBuffer(NativeVulkanBench  *bench,
                     NativeVulkanBuffer *buffer) {
  if (buffer->mapped) {
    vkUnmapMemory(bench->device, buffer->memory);
  }
  vkDestroyBuffer(bench->device, buffer->buffer, NULL);
  vkFreeMemory(bench->device, buffer->memory, NULL);
  memset(buffer, 0, sizeof(*buffer));
}

static bool
native_createCommands(NativeVulkanBench *bench) {
  VkCommandPoolCreateInfo      poolInfo;
  VkCommandBufferAllocateInfo commandInfo;
  VkFenceCreateInfo            fenceInfo;
  VkQueryPoolCreateInfo        queryInfo;

  memset(&poolInfo, 0, sizeof(poolInfo));
  memset(&commandInfo, 0, sizeof(commandInfo));
  memset(&fenceInfo, 0, sizeof(fenceInfo));
  memset(&queryInfo, 0, sizeof(queryInfo));
  poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = bench->queueFamilyIndex;
  if (vkCreateCommandPool(bench->device,
                          &poolInfo,
                          NULL,
                          &bench->commandPool) != VK_SUCCESS) {
    return false;
  }
  commandInfo.sType              =
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandInfo.commandPool        = bench->commandPool;
  commandInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandInfo.commandBufferCount = 1u;
  fenceInfo.sType                = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  queryInfo.sType                = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  queryInfo.queryType            = VK_QUERY_TYPE_TIMESTAMP;
  queryInfo.queryCount           = 2u;
  return vkAllocateCommandBuffers(bench->device,
                                  &commandInfo,
                                  &bench->commandBuffer) == VK_SUCCESS &&
         vkCreateFence(bench->device,
                       &fenceInfo,
                       NULL,
                       &bench->fence) == VK_SUCCESS &&
         vkCreateQueryPool(bench->device,
                           &queryInfo,
                           NULL,
                           &bench->queryPool) == VK_SUCCESS;
}

static bool
native_createRenderTarget(NativeVulkanBench *bench) {
  VkAttachmentDescription attachment;
  VkAttachmentReference   attachmentReference;
  VkSubpassDescription    subpass;
  VkRenderPassCreateInfo  renderPassInfo;
  VkImageCreateInfo       imageInfo;
  VkMemoryRequirements    requirements;
  VkMemoryAllocateInfo    allocationInfo;
  VkImageViewCreateInfo   viewInfo;
  VkFramebufferCreateInfo framebufferInfo;
  uint32_t                memoryType;

  memset(&attachment, 0, sizeof(attachment));
  memset(&attachmentReference, 0, sizeof(attachmentReference));
  memset(&subpass, 0, sizeof(subpass));
  memset(&renderPassInfo, 0, sizeof(renderPassInfo));
  memset(&imageInfo, 0, sizeof(imageInfo));
  memset(&allocationInfo, 0, sizeof(allocationInfo));
  memset(&viewInfo, 0, sizeof(viewInfo));
  memset(&framebufferInfo, 0, sizeof(framebufferInfo));

  attachment.format         = VK_FORMAT_B8G8R8A8_UNORM;
  attachment.samples        = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachmentReference.attachment = 0u;
  attachmentReference.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount    = 1u;
  subpass.pColorAttachments       = &attachmentReference;
  renderPassInfo.sType            = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount  = 1u;
  renderPassInfo.pAttachments     = &attachment;
  renderPassInfo.subpassCount     = 1u;
  renderPassInfo.pSubpasses       = &subpass;
  if (vkCreateRenderPass(bench->device,
                         &renderPassInfo,
                         NULL,
                         &bench->renderPass) != VK_SUCCESS) {
    return false;
  }

  imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType     = VK_IMAGE_TYPE_2D;
  imageInfo.format        = VK_FORMAT_B8G8R8A8_UNORM;
  imageInfo.extent.width  = bench->targetSize;
  imageInfo.extent.height = bench->targetSize;
  imageInfo.extent.depth  = 1u;
  imageInfo.mipLevels     = 1u;
  imageInfo.arrayLayers   = 1u;
  imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage         = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(bench->device,
                    &imageInfo,
                    NULL,
                    &bench->target) != VK_SUCCESS) {
    return false;
  }
  vkGetImageMemoryRequirements(bench->device, bench->target, &requirements);
  if (!native_memoryType(bench,
                         requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         &memoryType)) {
    return false;
  }
  allocationInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocationInfo.allocationSize  = requirements.size;
  allocationInfo.memoryTypeIndex = memoryType;
  if (vkAllocateMemory(bench->device,
                       &allocationInfo,
                       NULL,
                       &bench->targetMemory) != VK_SUCCESS ||
      vkBindImageMemory(bench->device,
                        bench->target,
                        bench->targetMemory,
                        0u) != VK_SUCCESS) {
    return false;
  }

  viewInfo.sType                           =
    VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image                           = bench->target;
  viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format                          = VK_FORMAT_B8G8R8A8_UNORM;
  viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.levelCount     = 1u;
  viewInfo.subresourceRange.layerCount     = 1u;
  if (vkCreateImageView(bench->device,
                        &viewInfo,
                        NULL,
                        &bench->targetView) != VK_SUCCESS) {
    return false;
  }
  framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass      = bench->renderPass;
  framebufferInfo.attachmentCount = 1u;
  framebufferInfo.pAttachments    = &bench->targetView;
  framebufferInfo.width           = bench->targetSize;
  framebufferInfo.height          = bench->targetSize;
  framebufferInfo.layers          = 1u;
  return vkCreateFramebuffer(bench->device,
                             &framebufferInfo,
                             NULL,
                             &bench->framebuffer) == VK_SUCCESS;
}

static bool
native_createModeBuffers(NativeVulkanBench        *bench,
                         const NativeVulkanConfig *config) {
  if (!native_createBuffer(bench,
                           sizeof(nativeVertices),
                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           &bench->vertexBuffer)) {
    return false;
  }
  memcpy(bench->vertexBuffer.mapped, nativeVertices, sizeof(nativeVertices));

  if (config->mode == NativeVulkanModeBinding) {
    for (uint32_t i = 0u; i < NATIVE_BINDING_COUNT; i++) {
      if (!native_createBuffer(bench,
                               sizeof(nativeBindingColors[i]),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                               &bench->bindingBuffers[i])) {
        return false;
      }
      memcpy(bench->bindingBuffers[i].mapped,
             nativeBindingColors[i],
             sizeof(nativeBindingColors[i]));
    }
  } else if (config->mode == NativeVulkanModeUpload) {
    VkDeviceSize totalBytes;

    bench->uploadBytesPerFrame =
      (uint64_t)config->drawCount * NATIVE_UPLOAD_ALIGNMENT;
    if (bench->uploadBytesPerFrame == 0u ||
        bench->uploadBytesPerFrame > UINT32_MAX ||
        bench->uploadBytesPerFrame > UINT64_MAX / NATIVE_UPLOAD_FRAMES) {
      return false;
    }
    totalBytes = bench->uploadBytesPerFrame * NATIVE_UPLOAD_FRAMES;
    if (!native_createBuffer(bench,
                             totalBytes,
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             &bench->uploadBuffer)) {
      return false;
    }
  }
  return true;
}

static bool
native_createDescriptors(NativeVulkanBench *bench) {
  VkDescriptorSetLayoutBinding layoutBinding;
  VkDescriptorSetLayoutCreateInfo layoutInfo;
  VkDescriptorPoolSize         poolSize;
  VkDescriptorPoolCreateInfo   poolInfo;
  VkDescriptorSetAllocateInfo  allocateInfo;
  VkDescriptorSetLayout        layouts[NATIVE_BINDING_COUNT];
  VkDescriptorType             descriptorType;
  uint32_t                     setCount;

  if (bench->mode != NativeVulkanModeBinding &&
      bench->mode != NativeVulkanModeUpload) {
    return true;
  }
  descriptorType = bench->mode == NativeVulkanModeBinding
                     ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                     : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
  setCount = bench->mode == NativeVulkanModeBinding
               ? NATIVE_BINDING_COUNT
               : 1u;
  memset(&layoutBinding, 0, sizeof(layoutBinding));
  memset(&layoutInfo, 0, sizeof(layoutInfo));
  memset(&poolSize, 0, sizeof(poolSize));
  memset(&poolInfo, 0, sizeof(poolInfo));
  memset(&allocateInfo, 0, sizeof(allocateInfo));
  layoutBinding.binding         = 0u;
  layoutBinding.descriptorType  = descriptorType;
  layoutBinding.descriptorCount = 1u;
  layoutBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
  layoutInfo.sType              =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount       = 1u;
  layoutInfo.pBindings          = &layoutBinding;
  if (vkCreateDescriptorSetLayout(bench->device,
                                  &layoutInfo,
                                  NULL,
                                  &bench->descriptorSetLayout) != VK_SUCCESS) {
    return false;
  }

  poolSize.type            = descriptorType;
  poolSize.descriptorCount = setCount;
  poolInfo.sType           = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.maxSets         = setCount;
  poolInfo.poolSizeCount   = 1u;
  poolInfo.pPoolSizes      = &poolSize;
  if (vkCreateDescriptorPool(bench->device,
                             &poolInfo,
                             NULL,
                             &bench->descriptorPool) != VK_SUCCESS) {
    return false;
  }
  for (uint32_t i = 0u; i < setCount; i++) {
    layouts[i] = bench->descriptorSetLayout;
  }
  allocateInfo.sType              =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocateInfo.descriptorPool     = bench->descriptorPool;
  allocateInfo.descriptorSetCount = setCount;
  allocateInfo.pSetLayouts        = layouts;
  if (vkAllocateDescriptorSets(bench->device,
                               &allocateInfo,
                               bench->descriptorSets) != VK_SUCCESS) {
    return false;
  }

  for (uint32_t i = 0u; i < setCount; i++) {
    VkDescriptorBufferInfo bufferInfo;
    VkWriteDescriptorSet   write;

    memset(&bufferInfo, 0, sizeof(bufferInfo));
    memset(&write, 0, sizeof(write));
    bufferInfo.buffer = bench->mode == NativeVulkanModeBinding
                          ? bench->bindingBuffers[i].buffer
                          : bench->uploadBuffer.buffer;
    bufferInfo.range  = 4u * sizeof(float);
    write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet           = bench->descriptorSets[i];
    write.dstBinding       = 0u;
    write.descriptorCount  = 1u;
    write.descriptorType   = descriptorType;
    write.pBufferInfo      = &bufferInfo;
    vkUpdateDescriptorSets(bench->device, 1u, &write, 0u, NULL);
  }
  return true;
}

static bool
native_compileShader(NativeVulkanBench        *bench,
                     const NativeVulkanConfig *config) {
  USLCompileOptions options;
  USLTargetSpec     target;
  USCompileInput    input;
  USCompileOutput   output;
  const char       *entries[2];
  void             *artifact;
  uint64_t          artifactSize;
  VkShaderModuleCreateInfo moduleInfo;
  bool              ok;

  artifact = bench_read(config->artifactPath, &artifactSize);
  if (!artifact || artifactSize > SIZE_MAX ||
      us_target_init(&target,
                     USL_BACKEND_SPIRV,
                     USL_TARGET_PROFILE_VULKAN_1_0) != USLOk ||
      us_compile_options_default(&options) != USLOk) {
    free(artifact);
    return false;
  }
  entries[0] = config->mode == NativeVulkanModeStatic ||
                       config->mode == NativeVulkanModeState
                 ? "api_vs"
                 : "tri_vs";
  entries[1] = config->mode == NativeVulkanModeStatic ||
                       config->mode == NativeVulkanModeState
                 ? "api_fs"
                 : "tri_fs";
  memset(&input, 0, sizeof(input));
  memset(&output, 0, sizeof(output));
  input.abi_version       = US_COMPILE_INPUT_VERSION;
  input.artifact          = artifact;
  input.artifact_size     = (size_t)artifactSize;
  input.target            = &target;
  input.entry_point_names = entries;
  input.entry_point_count = 2u;
  input.options           = &options;
  ok = us_compile(&input, &output) == USLOk &&
       output.backend == USL_BACKEND_SPIRV &&
       output.backend_data && output.backend_size > 0u &&
       output.backend_size % sizeof(uint32_t) == 0u;
  free(artifact);
  if (!ok) {
    us_free_compile_output(&output);
    return false;
  }

  memset(&moduleInfo, 0, sizeof(moduleInfo));
  moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  moduleInfo.codeSize = output.backend_size;
  moduleInfo.pCode    = output.backend_data;
  ok = vkCreateShaderModule(bench->device,
                            &moduleInfo,
                            NULL,
                            &bench->shaderModule) == VK_SUCCESS;
  us_free_compile_output(&output);
  return ok;
}

static bool
native_createPipelineLayout(NativeVulkanBench *bench) {
  VkPipelineLayoutCreateInfo layoutInfo;

  memset(&layoutInfo, 0, sizeof(layoutInfo));
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  if (bench->descriptorSetLayout != VK_NULL_HANDLE) {
    layoutInfo.setLayoutCount = 1u;
    layoutInfo.pSetLayouts    = &bench->descriptorSetLayout;
  }
  return vkCreatePipelineLayout(bench->device,
                                &layoutInfo,
                                NULL,
                                &bench->pipelineLayout) == VK_SUCCESS;
}

static bool
native_createPipeline(NativeVulkanBench        *bench,
                      const NativeVulkanConfig *config,
                      uint32_t                  index) {
  VkPipelineShaderStageCreateInfo stages[2];
  VkVertexInputBindingDescription vertexBinding;
  VkVertexInputAttributeDescription vertexAttribute;
  VkPipelineVertexInputStateCreateInfo vertexInput;
  VkPipelineInputAssemblyStateCreateInfo inputAssembly;
  VkPipelineViewportStateCreateInfo viewportState;
  VkPipelineRasterizationStateCreateInfo rasterization;
  VkPipelineMultisampleStateCreateInfo multisample;
  VkPipelineDepthStencilStateCreateInfo depthStencil;
  VkPipelineColorBlendAttachmentState colorAttachment;
  VkPipelineColorBlendStateCreateInfo colorBlend;
  VkDynamicState dynamicStates[4];
  VkPipelineDynamicStateCreateInfo dynamicState;
  VkGraphicsPipelineCreateInfo pipelineInfo;

  memset(stages, 0, sizeof(stages));
  memset(&vertexBinding, 0, sizeof(vertexBinding));
  memset(&vertexAttribute, 0, sizeof(vertexAttribute));
  memset(&vertexInput, 0, sizeof(vertexInput));
  memset(&inputAssembly, 0, sizeof(inputAssembly));
  memset(&viewportState, 0, sizeof(viewportState));
  memset(&rasterization, 0, sizeof(rasterization));
  memset(&multisample, 0, sizeof(multisample));
  memset(&depthStencil, 0, sizeof(depthStencil));
  memset(&colorAttachment, 0, sizeof(colorAttachment));
  memset(&colorBlend, 0, sizeof(colorBlend));
  memset(&dynamicState, 0, sizeof(dynamicState));
  memset(&pipelineInfo, 0, sizeof(pipelineInfo));

  stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = bench->shaderModule;
  stages[0].pName  = config->mode == NativeVulkanModeStatic ||
                              config->mode == NativeVulkanModeState
                       ? "api_vs"
                       : "tri_vs";
  stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = bench->shaderModule;
  stages[1].pName  = config->mode == NativeVulkanModeStatic ||
                              config->mode == NativeVulkanModeState
                       ? "api_fs"
                       : "tri_fs";

  vertexBinding.binding   = 0u;
  vertexBinding.stride    = 2u * sizeof(float);
  vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  vertexAttribute.location = 0u;
  vertexAttribute.binding  = 0u;
  vertexAttribute.format   = VK_FORMAT_R32G32_SFLOAT;
  vertexInput.sType        =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInput.vertexBindingDescriptionCount   = 1u;
  vertexInput.pVertexBindingDescriptions      = &vertexBinding;
  vertexInput.vertexAttributeDescriptionCount = 1u;
  vertexInput.pVertexAttributeDescriptions    = &vertexAttribute;
  inputAssembly.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  viewportState.sType    =
    VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1u;
  viewportState.scissorCount  = 1u;
  rasterization.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterization.polygonMode = VK_POLYGON_MODE_FILL;
  rasterization.cullMode    = VK_CULL_MODE_NONE;
  rasterization.frontFace   = index == 1u
                                ? VK_FRONT_FACE_CLOCKWISE
                                : VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterization.lineWidth   = 1.0f;
  multisample.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  depthStencil.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                   VK_COLOR_COMPONENT_G_BIT |
                                   VK_COLOR_COMPONENT_B_BIT |
                                   VK_COLOR_COMPONENT_A_BIT;
  if (index == 1u) {
    colorAttachment.blendEnable         = VK_TRUE;
    colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttachment.dstColorBlendFactor =
      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
    colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
  }
  colorBlend.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = 1u;
  colorBlend.pAttachments    = &colorAttachment;
  dynamicStates[0] = VK_DYNAMIC_STATE_VIEWPORT;
  dynamicStates[1] = VK_DYNAMIC_STATE_SCISSOR;
  dynamicStates[2] = VK_DYNAMIC_STATE_BLEND_CONSTANTS;
  dynamicStates[3] = VK_DYNAMIC_STATE_STENCIL_REFERENCE;
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 4u;
  dynamicState.pDynamicStates    = dynamicStates;

  pipelineInfo.sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.stageCount = 2u;
  pipelineInfo.pStages    = stages;
  pipelineInfo.pVertexInputState   = &vertexInput;
  pipelineInfo.pInputAssemblyState = &inputAssembly;
  pipelineInfo.pViewportState      = &viewportState;
  pipelineInfo.pRasterizationState = &rasterization;
  pipelineInfo.pMultisampleState   = &multisample;
  pipelineInfo.pDepthStencilState  = &depthStencil;
  pipelineInfo.pColorBlendState    = &colorBlend;
  pipelineInfo.pDynamicState       = &dynamicState;
  pipelineInfo.layout              = bench->pipelineLayout;
  pipelineInfo.renderPass          = bench->renderPass;
  pipelineInfo.subpass             = 0u;
  return vkCreateGraphicsPipelines(bench->device,
                                   VK_NULL_HANDLE,
                                   1u,
                                   &pipelineInfo,
                                   NULL,
                                   &bench->pipelines[index]) == VK_SUCCESS;
}

static bool
native_init(NativeVulkanBench        *bench,
            const NativeVulkanConfig *config) {
  memset(bench, 0, sizeof(*bench));
  (void)bench_processMemory(&bench->baselineMemory);
  bench->mode       = config->mode;
  bench->targetSize = config->mode == NativeVulkanModeState
                        ? NATIVE_STATE_TARGET
                        : 1u;
  if (!native_createContext(bench) ||
      !native_createCommands(bench) ||
      !native_createRenderTarget(bench) ||
      !native_createModeBuffers(bench, config) ||
      !native_createDescriptors(bench) ||
      !native_compileShader(bench, config) ||
      !native_createPipelineLayout(bench) ||
      !native_createPipeline(bench, config, 0u)) {
    return false;
  }
  return config->mode != NativeVulkanModeState ||
         native_createPipeline(bench, config, 1u);
}

static void
native_destroy(NativeVulkanBench *bench) {
  if (bench->device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(bench->device);
    native_destroyBuffer(bench, &bench->uploadBuffer);
    native_destroyBuffer(bench, &bench->vertexBuffer);
    for (uint32_t i = 0u; i < NATIVE_BINDING_COUNT; i++) {
      native_destroyBuffer(bench, &bench->bindingBuffers[i]);
      vkDestroyPipeline(bench->device, bench->pipelines[i], NULL);
    }
    vkDestroyPipelineLayout(bench->device, bench->pipelineLayout, NULL);
    vkDestroyDescriptorPool(bench->device, bench->descriptorPool, NULL);
    vkDestroyDescriptorSetLayout(bench->device,
                                 bench->descriptorSetLayout,
                                 NULL);
    vkDestroyShaderModule(bench->device, bench->shaderModule, NULL);
    vkDestroyFramebuffer(bench->device, bench->framebuffer, NULL);
    vkDestroyImageView(bench->device, bench->targetView, NULL);
    vkDestroyImage(bench->device, bench->target, NULL);
    vkFreeMemory(bench->device, bench->targetMemory, NULL);
    vkDestroyRenderPass(bench->device, bench->renderPass, NULL);
    vkDestroyQueryPool(bench->device, bench->queryPool, NULL);
    vkDestroyFence(bench->device, bench->fence, NULL);
    vkDestroyCommandPool(bench->device, bench->commandPool, NULL);
    vkDestroyDevice(bench->device, NULL);
  }
  vkDestroyInstance(bench->instance, NULL);
}

static void
native_draw(VkCommandBuffer commandBuffer) {
  vkCmdDraw(commandBuffer, 3u, 1u, 0u, 0u);
}

static void
native_bindPipeline(NativeVulkanBench *bench, uint32_t index) {
  vkCmdBindPipeline(bench->commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    bench->pipelines[index]);
}

static bool
native_encode(NativeVulkanBench        *bench,
              const NativeVulkanConfig *config) {
  VkCommandBuffer commandBuffer;

  commandBuffer = bench->commandBuffer;
  if (bench->mode == NativeVulkanModeStatic) {
    native_bindPipeline(bench, 0u);
    for (uint32_t draw = 0u; draw < config->drawCount; draw++) {
      native_draw(commandBuffer);
    }
    return true;
  }

  if (bench->mode == NativeVulkanModeState) {
    uint32_t previousState;

    previousState = UINT32_MAX;
    for (uint32_t draw = 0u; draw < config->drawCount; draw++) {
      uint32_t stateIndex;

      stateIndex = (draw >> 1u) & 1u;
      if (stateIndex != previousState) {
        VkViewport viewport;
        VkRect2D   scissor;
        float      blendConstants[4];

        memset(&viewport, 0, sizeof(viewport));
        memset(&scissor, 0, sizeof(scissor));
        native_bindPipeline(bench, stateIndex);
        viewport.x        = stateIndex == 0u ? 0.0f : 1.0f;
        viewport.y        = stateIndex == 0u ? 0.0f : 1.0f;
        viewport.width    = stateIndex == 0u
                              ? NATIVE_STATE_TARGET
                              : NATIVE_STATE_TARGET - 2u;
        viewport.height   = viewport.width;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        scissor.offset.x  = stateIndex == 0u ? 0 : 1;
        scissor.offset.y  = stateIndex == 0u ? 0 : 1;
        scissor.extent.width  = stateIndex == 0u
                                  ? NATIVE_STATE_TARGET
                                  : NATIVE_STATE_TARGET - 2u;
        scissor.extent.height = scissor.extent.width;
        blendConstants[0] = stateIndex == 0u ? 0.0f : 1.0f;
        blendConstants[1] = stateIndex == 0u ? 0.0f : 0.5f;
        blendConstants[2] = stateIndex == 0u ? 0.0f : 0.25f;
        blendConstants[3] = stateIndex == 0u ? 0.0f : 1.0f;
        vkCmdSetViewport(commandBuffer, 0u, 1u, &viewport);
        vkCmdSetScissor(commandBuffer, 0u, 1u, &scissor);
        vkCmdSetBlendConstants(commandBuffer, blendConstants);
        vkCmdSetStencilReference(commandBuffer,
                                 VK_STENCIL_FACE_FRONT_AND_BACK,
                                 stateIndex);
        previousState = stateIndex;
      }
      native_draw(commandBuffer);
    }
    return true;
  }

  if (bench->mode == NativeVulkanModeBinding) {
    uint32_t previousGroup;

    native_bindPipeline(bench, 0u);
    previousGroup = UINT32_MAX;
    for (uint32_t draw = 0u; draw < config->drawCount; draw++) {
      uint32_t groupIndex;

      groupIndex = (draw >> 1u) & 1u;
      if (groupIndex != previousGroup) {
        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                bench->pipelineLayout,
                                0u,
                                1u,
                                &bench->descriptorSets[groupIndex],
                                0u,
                                NULL);
        previousGroup = groupIndex;
      }
      native_draw(commandBuffer);
    }
    return true;
  }

  if (bench->mode == NativeVulkanModeUpload) {
    uint64_t frameBase;
    uint8_t *bytes;

    frameBase = (uint64_t)(bench->frameIndex % NATIVE_UPLOAD_FRAMES) *
                bench->uploadBytesPerFrame;
    bytes = bench->uploadBuffer.mapped;
    native_bindPipeline(bench, 0u);
    for (uint32_t draw = 0u; draw < config->drawCount; draw++) {
      VkDeviceSize vertexOffset;
      float        tint[4];
      uint64_t     uniformOffset;
      uint32_t     dynamicOffset;

      tint[0]       = (draw & 1u) ? 0.2f : 1.0f;
      tint[1]       = (draw & 2u) ? 1.0f : 0.3f;
      tint[2]       = (draw & 4u) ? 0.4f : 1.0f;
      tint[3]       = 1.0f;
      uniformOffset = frameBase + (uint64_t)draw * NATIVE_UPLOAD_ALIGNMENT;
      vertexOffset  = uniformOffset + sizeof(tint);
      memcpy(bytes + uniformOffset, tint, sizeof(tint));
      memcpy(bytes + vertexOffset, nativeVertices, sizeof(nativeVertices));
      dynamicOffset = (uint32_t)uniformOffset;
      vkCmdBindVertexBuffers(commandBuffer,
                             0u,
                             1u,
                             &bench->uploadBuffer.buffer,
                             &vertexOffset);
      vkCmdBindDescriptorSets(commandBuffer,
                              VK_PIPELINE_BIND_POINT_GRAPHICS,
                              bench->pipelineLayout,
                              0u,
                              1u,
                              &bench->descriptorSets[0],
                              1u,
                              &dynamicOffset);
      native_draw(commandBuffer);
    }
    bench->frameIndex++;
    return true;
  }
  return false;
}

static bool
native_frame(NativeVulkanBench        *bench,
             const NativeVulkanConfig *config,
             double                   *outEncodeNs,
             double                   *outGpuNs) {
  VkCommandBufferBeginInfo beginInfo;
  VkRenderPassBeginInfo    renderPassInfo;
  VkClearValue             clearValue;
  VkViewport               viewport;
  VkRect2D                 scissor;
  VkDeviceSize             vertexOffset;
  VkSubmitInfo             submitInfo;
  uint64_t                 timestamps[2];
  double                   begin;
  double                   end;

  memset(&beginInfo, 0, sizeof(beginInfo));
  memset(&renderPassInfo, 0, sizeof(renderPassInfo));
  memset(&clearValue, 0, sizeof(clearValue));
  memset(&viewport, 0, sizeof(viewport));
  memset(&scissor, 0, sizeof(scissor));
  memset(&submitInfo, 0, sizeof(submitInfo));
  begin = bench_now();
  if (vkResetFences(bench->device, 1u, &bench->fence) != VK_SUCCESS ||
      vkResetCommandBuffer(bench->commandBuffer, 0u) != VK_SUCCESS) {
    return false;
  }
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(bench->commandBuffer, &beginInfo) != VK_SUCCESS) {
    return false;
  }
  vkCmdResetQueryPool(bench->commandBuffer, bench->queryPool, 0u, 2u);
  vkCmdWriteTimestamp(bench->commandBuffer,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      bench->queryPool,
                      0u);

  clearValue.color.float32[3]      = 1.0f;
  renderPassInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass        = bench->renderPass;
  renderPassInfo.framebuffer       = bench->framebuffer;
  renderPassInfo.renderArea.extent.width  = bench->targetSize;
  renderPassInfo.renderArea.extent.height = bench->targetSize;
  renderPassInfo.clearValueCount   = 1u;
  renderPassInfo.pClearValues      = &clearValue;
  vkCmdBeginRenderPass(bench->commandBuffer,
                       &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);
  viewport.width    = (float)bench->targetSize;
  viewport.height   = (float)bench->targetSize;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  scissor.extent.width  = bench->targetSize;
  scissor.extent.height = bench->targetSize;
  vkCmdSetViewport(bench->commandBuffer, 0u, 1u, &viewport);
  vkCmdSetScissor(bench->commandBuffer, 0u, 1u, &scissor);
  vertexOffset = 0u;
  vkCmdBindVertexBuffers(bench->commandBuffer,
                         0u,
                         1u,
                         &bench->vertexBuffer.buffer,
                         &vertexOffset);
  if (!native_encode(bench, config)) {
    return false;
  }
  vkCmdEndRenderPass(bench->commandBuffer);
  vkCmdWriteTimestamp(bench->commandBuffer,
                      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                      bench->queryPool,
                      1u);
  if (vkEndCommandBuffer(bench->commandBuffer) != VK_SUCCESS) {
    return false;
  }
  submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1u;
  submitInfo.pCommandBuffers    = &bench->commandBuffer;
  if (vkQueueSubmit(bench->queue,
                    1u,
                    &submitInfo,
                    bench->fence) != VK_SUCCESS) {
    return false;
  }
  end = bench_now();
  if (vkWaitForFences(bench->device,
                      1u,
                      &bench->fence,
                      VK_TRUE,
                      UINT64_MAX) != VK_SUCCESS ||
      vkGetQueryPoolResults(bench->device,
                            bench->queryPool,
                            0u,
                            2u,
                            sizeof(timestamps),
                            timestamps,
                            sizeof(timestamps[0]),
                            VK_QUERY_RESULT_64_BIT |
                              VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS ||
      timestamps[1] <= timestamps[0]) {
    return false;
  }
  *outEncodeNs = (end - begin) * 1e9;
  *outGpuNs    = (double)(timestamps[1] - timestamps[0]) *
                 bench->properties.limits.timestampPeriod;
  return true;
}

static bool
native_run(NativeVulkanBench        *bench,
           const NativeVulkanConfig *config,
           NativeVulkanMetrics      *metrics) {
  size_t sampleCount;

  if ((size_t)config->measuredFrames > SIZE_MAX / config->repeats) {
    return false;
  }
  sampleCount = (size_t)config->measuredFrames * config->repeats;
  memset(metrics, 0, sizeof(*metrics));
  metrics->encodeSamples = calloc(sampleCount,
                                  sizeof(*metrics->encodeSamples));
  metrics->encodeRepeatMedians = calloc(
    config->repeats,
    sizeof(*metrics->encodeRepeatMedians)
  );
  metrics->gpuSamples = calloc(sampleCount, sizeof(*metrics->gpuSamples));
  metrics->gpuRepeatMedians = calloc(
    config->repeats,
    sizeof(*metrics->gpuRepeatMedians)
  );
  metrics->sampleCount = sampleCount;
  if (!metrics->encodeSamples || !metrics->encodeRepeatMedians ||
      !metrics->gpuSamples || !metrics->gpuRepeatMedians) {
    return false;
  }

  for (uint32_t repeat = 0u; repeat < config->repeats; repeat++) {
    size_t base;

    for (uint32_t frame = 0u; frame < config->warmupFrames; frame++) {
      double ignoredEncode;
      double ignoredGpu;

      if (!native_frame(bench,
                        config,
                        &ignoredEncode,
                        &ignoredGpu)) {
        return false;
      }
    }
    base = (size_t)repeat * config->measuredFrames;
    for (uint32_t frame = 0u; frame < config->measuredFrames; frame++) {
      if (!native_frame(bench,
                        config,
                        &metrics->encodeSamples[base + frame],
                        &metrics->gpuSamples[base + frame])) {
        return false;
      }
    }
    metrics->encodeRepeatMedians[repeat] = bench_percentile(
      &metrics->encodeSamples[base],
      config->measuredFrames,
      0.5
    );
    metrics->gpuRepeatMedians[repeat] = bench_percentile(
      &metrics->gpuSamples[base],
      config->measuredFrames,
      0.5
    );
  }
  return true;
}

static void
native_print(const NativeVulkanBench   *bench,
             const NativeVulkanConfig  *config,
             NativeVulkanMetrics       *metrics) {
  BenchProcessMemory memory;
  double             encodeMedian;
  double             encodeP95;
  double             encodeP99;
  double             gpuMedian;
  double             gpuP95;
  double             gpuP99;

  encodeMedian = bench_percentile(metrics->encodeRepeatMedians,
                                  config->repeats,
                                  0.5);
  encodeP95 = bench_percentile(metrics->encodeSamples,
                               metrics->sampleCount,
                               0.95);
  encodeP99 = bench_percentile(metrics->encodeSamples,
                               metrics->sampleCount,
                               0.99);
  gpuMedian = bench_percentile(metrics->gpuRepeatMedians,
                               config->repeats,
                               0.5);
  gpuP95 = bench_percentile(metrics->gpuSamples,
                            metrics->sampleCount,
                            0.95);
  gpuP99 = bench_percentile(metrics->gpuSamples,
                            metrics->sampleCount,
                            0.99);
  printf("Native Vulkan %s benchmark\n", native_modeName(config->mode));
  printf("adapter: %s, api: vulkan %u.%u\n",
         bench->properties.deviceName,
         VK_VERSION_MAJOR(bench->properties.apiVersion),
         VK_VERSION_MINOR(bench->properties.apiVersion));
  printf("draws/frame: %u, warmup: %u, frames: %u, repeats: %u\n",
         config->drawCount,
         config->warmupFrames,
         config->measuredFrames,
         config->repeats);
  printf("encode+submit: median %.3f us, p95 %.3f us, p99 %.3f us\n",
         encodeMedian / 1e3,
         encodeP95 / 1e3,
         encodeP99 / 1e3);
  printf("median per draw: %.3f ns\n", encodeMedian / config->drawCount);
  printf("GPU frame: median %.3f us, p95 %.3f us, p99 %.3f us\n",
         gpuMedian / 1e3,
         gpuP95 / 1e3,
         gpuP99 / 1e3);
  if (bench_processMemory(&memory) && bench->baselineMemory.residentBytes > 0u) {
    double residentDelta;
    double peakDelta;

    residentDelta = memory.residentBytes > bench->baselineMemory.residentBytes
                      ? (double)(memory.residentBytes -
                                 bench->baselineMemory.residentBytes)
                      : 0.0;
    peakDelta = memory.peakResidentBytes > bench->baselineMemory.peakResidentBytes
                  ? (double)(memory.peakResidentBytes -
                             bench->baselineMemory.peakResidentBytes)
                  : 0.0;
    printf("process memory: resident %.2f MiB (+%.2f), peak %.2f MiB "
           "(+%.2f)\n",
           (double)memory.residentBytes / (1024.0 * 1024.0),
           residentDelta / (1024.0 * 1024.0),
           (double)memory.peakResidentBytes / (1024.0 * 1024.0),
           peakDelta / (1024.0 * 1024.0));
  }
}

static void
native_freeMetrics(NativeVulkanMetrics *metrics) {
  free(metrics->gpuRepeatMedians);
  free(metrics->gpuSamples);
  free(metrics->encodeRepeatMedians);
  free(metrics->encodeSamples);
}

int
main(int argc, char *argv[]) {
  NativeVulkanConfig  config;
  NativeVulkanBench   bench;
  NativeVulkanMetrics metrics;
  int                 result;

  memset(&bench, 0, sizeof(bench));
  memset(&metrics, 0, sizeof(metrics));
  result = EXIT_FAILURE;
  if (!native_parseConfig(argc, argv, &config) ||
      !native_init(&bench, &config) ||
      !native_run(&bench, &config, &metrics)) {
    fprintf(stderr, "native Vulkan render benchmark failed\n");
  } else {
    native_print(&bench, &config, &metrics);
    result = EXIT_SUCCESS;
  }
  native_freeMetrics(&metrics);
  native_destroy(&bench);
  return result;
}
