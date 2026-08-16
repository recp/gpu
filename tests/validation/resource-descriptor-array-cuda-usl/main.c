#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  TextureCount     = 2u,
  MaxSampledTexels = 4u,
  MaxStorageTexels = 3u,
  ColorWidth       = 4u,
  OutputCount      = 5u * ColorWidth
};

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) fclose(file);
    return NULL;
  }

  data = malloc((size_t)size);
  if (!data || fread(data, 1u, (size_t)size, file) != (size_t)size) {
    free(data);
    fclose(file);
    return NULL;
  }
  fclose(file);
  *outSize = (uint64_t)size;
  return data;
}

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userData) {
  (void)device;
  (void)userData;
  fprintf(stderr,
          "CUDA device error: %s\n",
          error && error->message ? error->message : "unknown error");
}

static int
layout_has_entry(const GPUBindGroupLayoutEntry *entries,
                 uint32_t                       entryCount,
                 uint32_t                       binding,
                 GPUBindingType                 bindingType,
                 uint32_t                       arrayCount) {
  for (uint32_t i = 0u; i < entryCount; i++) {
    if (entries[i].binding == binding &&
        entries[i].bindingType == bindingType &&
        entries[i].arrayCount == arrayCount) {
      return 1;
    }
  }
  return 0;
}

static int
create_texture(GPUDevice           *device,
               GPUQueue            *queue,
               const char          *label,
               GPUTextureUsageFlags usage,
               uint32_t             width,
               const float         *data,
               GPUTexture         **outTexture,
               GPUTextureView     **outView) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  textureInfo.width            = width;
  textureInfo.height           = 1u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = usage | GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, outTexture) != GPU_OK ||
      !*outTexture) {
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = width;
  writeRegion.height       = 1u;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = width * ColorWidth * sizeof(float);
  writeRegion.rowsPerImage = 1u;
  if (GPUQueueWriteTexture(queue,
                           *outTexture,
                           &writeRegion,
                           data,
                           (uint64_t)width * ColorWidth * sizeof(float)) !=
      GPU_OK) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  return GPUCreateTextureView(*outTexture, &viewInfo, outView) == GPU_OK &&
         *outView;
}

static int
create_sampler(GPUDevice     *device,
               const char    *label,
               GPUAddressMode addressMode,
               GPUSampler   **outSampler) {
  GPUSamplerCreateInfo info = {0};

  info.chain.sType        = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.chain.structSize   = sizeof(info);
  info.label              = label;
  info.desc.minFilter     = GPU_FILTER_NEAREST;
  info.desc.magFilter     = GPU_FILTER_NEAREST;
  info.desc.mipFilter     = GPU_MIP_FILTER_NEAREST;
  info.desc.addressU      = addressMode;
  info.desc.addressV      = addressMode;
  info.desc.addressW      = addressMode;
  info.desc.maxAnisotropy = 1u;
  return GPUCreateSampler(device, &info, false, outSampler) == GPU_OK &&
         *outSampler;
}

static int
colors_match(const float output[OutputCount]) {
  const float expected[OutputCount] = {
    1.0f, 1.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 1.0f, 1.0f,
    1.0f, 1.0f, 0.0f, 1.0f,
    4.0f, 4.0f, 4.0f, 4.0f,
    3.0f, 3.0f, 3.0f, 3.0f
  };

  for (uint32_t i = 0u; i < OutputCount; i++) {
    if (fabsf(output[i] - expected[i]) > 0.0001f) {
      return 0;
    }
  }
  return 1;
}

int
main(int argc, char **argv) {
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUQueue              *queue;
  GPUShaderLibrary      *library;
  GPUShaderLayout       *shaderLayout;
  GPUComputePipeline    *writePipeline;
  GPUComputePipeline    *readPipeline;
  GPUTexture            *sampledTextures[TextureCount];
  GPUTextureView        *sampledViews[TextureCount];
  GPUTexture            *storageTextures[TextureCount];
  GPUTextureView        *storageViews[TextureCount];
  GPUSampler            *samplers[TextureCount];
  GPUBuffer             *selectionBuffer;
  GPUBuffer             *outputBuffer;
  GPUBindGroup          *group;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  const GPUBindGroupLayoutEntry *layoutEntries;
  GPUInstanceCreateInfo         instanceInfo = {0};
  GPUDeviceCreateInfo           deviceInfo = {0};
  GPUComputePipelineCreateInfo  pipelineInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUBindGroupEntry             entries[8] = {0};
  GPUBindGroupCreateInfo        groupInfo = {0};
  GPUQueueSubmitInfo            submitInfo = {0};
  const float sampledData[TextureCount][MaxSampledTexels * ColorWidth] = {
    {
      1.0f, 0.0f, 0.0f, 1.0f,
      1.0f, 0.0f, 1.0f, 1.0f
    },
    {
      0.0f, 1.0f, 0.0f, 1.0f,
      0.0f, 1.0f, 0.0f, 1.0f,
      0.0f, 1.0f, 0.0f, 1.0f,
      1.0f, 1.0f, 0.0f, 1.0f
    }
  };
  const float storageData[TextureCount][MaxStorageTexels * ColorWidth] = {
    {
      0.0f, 0.0f, 1.0f, 1.0f,
      0.0f, 0.0f, 1.0f, 1.0f
    },
    {
      0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 0.0f
    }
  };
  const uint32_t sampledWidths[TextureCount] = {2u, 4u};
  const uint32_t storageWidths[TextureCount] = {2u, 3u};
  float       output[OutputCount] = {0};
  GPUFeature  feature;
  uint64_t    artifactSize;
  uint32_t    adapterCount;
  uint32_t    layoutEntryCount;
  GPUResult   result;
  int         status;

  if (argc != 2) {
    fprintf(stderr,
            "usage: gpu-resource-descriptor-array-cuda-usl artifact.us\n");
    return 1;
  }

  instance         = NULL;
  adapter          = NULL;
  device           = NULL;
  queue            = NULL;
  library          = NULL;
  shaderLayout     = NULL;
  writePipeline    = NULL;
  readPipeline     = NULL;
  selectionBuffer  = NULL;
  outputBuffer     = NULL;
  group            = NULL;
  cmdb             = NULL;
  pass             = NULL;
  artifactSize     = 0u;
  layoutEntryCount = 0u;
  status           = 1;
  for (uint32_t i = 0u; i < TextureCount; i++) {
    sampledTextures[i] = NULL;
    sampledViews[i]    = NULL;
    storageTextures[i] = NULL;
    storageViews[i]    = NULL;
    samplers[i]        = NULL;
  }

  artifact = read_file(argv[1], &artifactSize);
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_CUDA;
  instanceInfo.enableValidation = true;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    puts("CUDA Driver backend unavailable");
    status = 77;
    goto cleanup;
  }

  adapterCount = 1u;
  result = GPUEnumerateAdapters(instance, &adapterCount, &adapter);
  if ((result != GPU_OK && result != GPU_ERROR_INSUFFICIENT_CAPACITY) ||
      !adapter) {
    puts("CUDA adapter unavailable");
    status = 77;
    goto cleanup;
  }
  if (!GPUIsFeatureSupported(adapter, GPU_FEATURE_DESCRIPTOR_INDEXING)) {
    puts("CUDA descriptor indexing unavailable");
    status = 77;
    goto cleanup;
  }

  feature                          = GPU_FEATURE_DESCRIPTOR_INDEXING;
  deviceInfo.chain.sType           = GPU_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.chain.structSize      = sizeof(deviceInfo);
  deviceInfo.required.pFeatures    = &feature;
  deviceInfo.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &deviceInfo, &device) != GPU_OK || !device) {
    fprintf(stderr, "CUDA descriptor-array device creation failed\n");
    goto cleanup;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!queue) {
    fprintf(stderr, "CUDA compute queue unavailable\n");
    goto cleanup;
  }
  if (GPUSetDeviceErrorCallback(device, device_error, NULL) != GPU_OK) {
    fprintf(stderr, "CUDA device error callback setup failed\n");
    goto cleanup;
  }

  result = GPUCreateShaderLibraryFromUSL(device,
                                         artifact,
                                         artifactSize,
                                         &library);
  if (result != GPU_OK || !library) {
    fprintf(stderr,
            "CUDA resource descriptor-array library creation failed (%d)\n",
            result);
    goto cleanup;
  }
  result = GPUCreateShaderLayout(device, library, &shaderLayout);
  if (result != GPU_OK || !shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr,
            "CUDA resource descriptor-array layout creation failed (%d)\n",
            result);
    goto cleanup;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    shaderLayout->bindGroupLayouts[0], &layoutEntryCount);
  if (!layoutEntries || layoutEntryCount != 5u ||
      !layout_has_entry(layoutEntries,
                        layoutEntryCount,
                        0u,
                        GPU_BINDING_SAMPLED_TEXTURE,
                        2u) ||
      !layout_has_entry(layoutEntries,
                        layoutEntryCount,
                        2u,
                        GPU_BINDING_SAMPLER,
                        2u) ||
      !layout_has_entry(layoutEntries,
                        layoutEntryCount,
                        4u,
                        GPU_BINDING_UNIFORM_BUFFER,
                        1u) ||
      !layout_has_entry(layoutEntries,
                        layoutEntryCount,
                        5u,
                        GPU_BINDING_STORAGE_BUFFER,
                        1u) ||
      !layout_has_entry(layoutEntries,
                        layoutEntryCount,
                        6u,
                        GPU_BINDING_STORAGE_TEXTURE,
                        2u)) {
    fprintf(stderr,
            "CUDA resource descriptor-array layout mismatch: %u entries\n",
            layoutEntryCount);
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "cuda-resource-descriptor-array-write";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "descriptor_array_write";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &writePipeline) != GPU_OK ||
      !writePipeline) {
    fprintf(stderr, "CUDA resource descriptor-array write pipeline failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "cuda-resource-descriptor-array-read";
  pipelineInfo.entryPoint = "descriptor_array_read";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &readPipeline) != GPU_OK ||
      !readPipeline) {
    fprintf(stderr, "CUDA resource descriptor-array read pipeline failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < TextureCount; i++) {
    if (!create_texture(device,
                        queue,
                        "cuda-resource-array-sampled",
                        GPU_TEXTURE_USAGE_SAMPLED,
                        sampledWidths[i],
                        sampledData[i],
                        &sampledTextures[i],
                        &sampledViews[i]) ||
        !create_texture(device,
                        queue,
                        "cuda-resource-array-storage",
                        GPU_TEXTURE_USAGE_STORAGE,
                        storageWidths[i],
                        storageData[i],
                        &storageTextures[i],
                        &storageViews[i]) ||
        !create_sampler(device,
                        "cuda-resource-array-sampler",
                        i == 0u ? GPU_ADDRESS_MODE_REPEAT
                                : GPU_ADDRESS_MODE_CLAMP_TO_EDGE,
                        &samplers[i])) {
      fprintf(stderr, "CUDA resource descriptor-array resource setup failed\n");
      goto cleanup;
    }
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "cuda-resource-array-selection";
  bufferInfo.sizeBytes        = sizeof(uint32_t);
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  {
    const uint32_t selection = 1u;

    if (GPUCreateBuffer(device, &bufferInfo, &selectionBuffer) != GPU_OK ||
        !selectionBuffer ||
        GPUQueueWriteBuffer(queue,
                            selectionBuffer,
                            0u,
                            &selection,
                            sizeof(selection)) != GPU_OK) {
      fprintf(stderr, "CUDA resource descriptor-array selection failed\n");
      goto cleanup;
    }
  }

  bufferInfo.label     = "cuda-resource-array-output";
  bufferInfo.sizeBytes = sizeof(output);
  bufferInfo.usage     = GPU_BUFFER_USAGE_STORAGE |
                         GPU_BUFFER_USAGE_COPY_SRC |
                         GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "CUDA resource descriptor-array output failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < TextureCount; i++) {
    entries[i].binding     = 0u;
    entries[i].arrayIndex  = i;
    entries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    entries[i].textureView = sampledViews[i];

    entries[2u + i].binding     = 2u;
    entries[2u + i].arrayIndex  = i;
    entries[2u + i].bindingType = GPU_BINDING_SAMPLER;
    entries[2u + i].sampler     = samplers[i];

    entries[6u + i].binding     = 6u;
    entries[6u + i].arrayIndex  = i;
    entries[6u + i].bindingType = GPU_BINDING_STORAGE_TEXTURE;
    entries[6u + i].textureView = storageViews[i];
  }
  entries[4].binding       = 4u;
  entries[4].bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  entries[4].buffer.buffer = selectionBuffer;
  entries[4].buffer.size   = sizeof(uint32_t);
  entries[5].binding       = 5u;
  entries[5].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[5].buffer.buffer = outputBuffer;
  entries[5].buffer.size   = sizeof(output);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-resource-descriptor-array";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = entries;
  groupInfo.entryCount       = (uint32_t)(sizeof(entries) / sizeof(entries[0]));
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "CUDA resource descriptor-array bind group failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "cuda-resource-descriptor-array",
                              &cmdb) != GPU_OK ||
      !cmdb ||
      !(pass = GPUBeginComputePass(cmdb,
                                   "resource-descriptor-array-roundtrip"))) {
    fprintf(stderr, "CUDA resource descriptor-array encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, writePipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUBindComputePipeline(pass, readPipeline);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    fprintf(stderr, "CUDA resource descriptor-array submission failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !colors_match(output)) {
    fprintf(stderr,
            "CUDA resource descriptor-array mismatch: "
            "sample %.3f %.3f %.3f %.3f, "
            "image0 %.3f %.3f %.3f %.3f, "
            "image1 %.3f %.3f %.3f %.3f, "
            "texture width %.3f %.3f %.3f %.3f, "
            "image width %.3f %.3f %.3f %.3f\n",
            output[0], output[1], output[2], output[3],
            output[4], output[5], output[6], output[7],
            output[8], output[9], output[10], output[11],
            output[12], output[13], output[14], output[15],
            output[16], output[17], output[18], output[19]);
    goto cleanup;
  }
  status = 0;

cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) (void)GPUDiscardCommandBuffer(cmdb);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyBuffer(selectionBuffer);
  for (uint32_t i = TextureCount; i > 0u; i--) {
    uint32_t index;

    index = i - 1u;
    GPUDestroySampler(samplers[index]);
    GPUDestroyTextureView(storageViews[index]);
    GPUDestroyTexture(storageTextures[index]);
    GPUDestroyTextureView(sampledViews[index]);
    GPUDestroyTexture(sampledTextures[index]);
  }
  GPUDestroyComputePipeline(readPipeline);
  GPUDestroyComputePipeline(writePipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL resource descriptor-array validation passed");
  }
  return status;
}
