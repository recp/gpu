#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  ChannelCount = 4u,
  TextureCount = 5u,
  OutputCount  = 5u
};

int
validate_ptx_metadata(const void *artifact, uint64_t artifactSize);

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
fill_values(float *values, uint32_t count, float base) {
  for (uint32_t i = 0u; i < count; i++) {
    values[i] = base + (float)i;
  }
}

static int
create_texture(GPUDevice            *device,
               const char           *label,
               GPUTextureDimension  dimension,
               uint32_t             width,
               uint32_t             height,
               uint32_t             depthOrLayers,
               uint32_t             mipLevelCount,
               GPUTexture         **outTexture) {
  GPUTextureCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.dimension        = dimension;
  info.format           = GPU_FORMAT_RGBA32_FLOAT;
  info.width            = width;
  info.height           = height;
  info.depthOrLayers    = depthOrLayers;
  info.mipLevelCount    = mipLevelCount;
  info.sampleCount      = 1u;
  info.usage            = GPU_TEXTURE_USAGE_STORAGE |
                          GPU_TEXTURE_USAGE_COPY_DST;
  return GPUCreateTexture(device, &info, outTexture) == GPU_OK && *outTexture;
}

static int
create_view(GPUTexture         *texture,
            const char         *label,
            GPUTextureViewType  viewType,
            uint32_t            baseMipLevel,
            uint32_t            arrayLayerCount,
            GPUTextureView    **outView) {
  GPUTextureViewCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.viewType         = viewType;
  info.format           = GPU_FORMAT_RGBA32_FLOAT;
  info.baseMipLevel     = baseMipLevel;
  info.mipLevelCount    = 1u;
  info.arrayLayerCount  = arrayLayerCount;
  return GPUCreateTextureView(texture, &info, outView) == GPU_OK && *outView;
}

static int
write_texture(GPUQueue   *queue,
              GPUTexture *texture,
              uint32_t    width,
              uint32_t    height,
              uint32_t    depth,
              uint32_t    mipLevel,
              uint32_t    layerCount,
              const void *data,
              uint64_t    sizeBytes) {
  GPUTextureWriteRegion region = {0};

  region.aspect         = GPU_TEXTURE_ASPECT_ALL;
  region.width          = width;
  region.height         = height;
  region.depth          = depth;
  region.mipLevel       = mipLevel;
  region.layerCount     = layerCount;
  region.bytesPerRow    = width * ChannelCount * sizeof(float);
  region.rowsPerImage   = height;
  return GPUQueueWriteTexture(queue,
                              texture,
                              &region,
                              data,
                              sizeBytes) == GPU_OK;
}

static int
values_match(const float output[OutputCount * ChannelCount]) {
  static const float Bases[OutputCount] = {
    100.0f, 200.0f, 300.0f, 400.0f, 600.0f
  };
  static const float Increments[OutputCount] = {
    10.0f, 20.0f, 30.0f, 40.0f, 50.0f
  };
  static const uint32_t Offsets[OutputCount] = {
    8u, 36u, 36u, 20u, 4u
  };

  for (uint32_t value = 0u; value < OutputCount; value++) {
    for (uint32_t channel = 0u; channel < ChannelCount; channel++) {
      float expected;
      float actual;

      expected = Bases[value] + (float)(Offsets[value] + channel) +
                 Increments[value];
      actual = output[value * ChannelCount + channel];
      if (fabsf(actual - expected) > 0.0001f) {
        fprintf(stderr,
                "CUDA storage geometry mismatch at %u.%u: %.3f != %.3f\n",
                value,
                channel,
                actual,
                expected);
        return 0;
      }
    }
  }
  return 1;
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
create_pipeline(GPUDevice            *device,
                GPUShaderLibrary     *library,
                GPUPipelineLayout    *layout,
                const char           *entryPoint,
                GPUComputePipeline  **outPipeline) {
  GPUComputePipelineCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = entryPoint;
  info.layout           = layout;
  info.library          = library;
  info.entryPoint       = entryPoint;
  return GPUCreateComputePipeline(device, &info, outPipeline) == GPU_OK &&
         *outPipeline;
}

int
main(int argc, char **argv) {
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUQueue              *queue;
  GPUShaderLibrary      *library;
  GPUShaderLayout       *shaderLayout;
  GPUBindGroup          *textureGroup;
  GPUBindGroup          *outputGroup;
  GPUBuffer             *outputBuffer;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUTexture            *textures[TextureCount] = {0};
  GPUTextureView        *views[TextureCount] = {0};
  GPUComputePipeline    *pipelines[2] = {0};
  GPUInstanceCreateInfo  instanceInfo = {0};
  GPUBindGroupEntry      textureEntries[TextureCount] = {0};
  GPUBindGroupEntry      outputEntry = {0};
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  float                  line[4u * ChannelCount];
  float                  lines[4u * 3u * ChannelCount];
  float                  images[2u * 2u * 3u * ChannelCount];
  float                  volume[2u * 2u * 2u * ChannelCount];
  float                  mip0[4u * 2u * ChannelCount];
  float                  mip1[2u * ChannelCount];
  float                  mip2[ChannelCount];
  float                  output[OutputCount * ChannelCount] = {0};
  uint64_t               artifactSize;
  uint32_t               adapterCount;
  GPUResult              result;
  int                    status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-storage-geometry-cuda-usl artifact.us\n");
    return 1;
  }

  instance      = NULL;
  adapter       = NULL;
  device        = NULL;
  queue         = NULL;
  library       = NULL;
  shaderLayout  = NULL;
  textureGroup  = NULL;
  outputGroup   = NULL;
  outputBuffer  = NULL;
  cmdb          = NULL;
  pass          = NULL;
  artifactSize  = 0u;
  artifact      = read_file(argv[1], &artifactSize);
  status        = 1;
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }
  if (!validate_ptx_metadata(artifact, artifactSize)) {
    goto cleanup;
  }

  fill_values(line, 4u * ChannelCount, 100.0f);
  fill_values(lines, 4u * 3u * ChannelCount, 200.0f);
  fill_values(images, 2u * 2u * 3u * ChannelCount, 300.0f);
  fill_values(volume, 2u * 2u * 2u * ChannelCount, 400.0f);
  fill_values(mip0, 4u * 2u * ChannelCount, 500.0f);
  fill_values(mip1, 2u * ChannelCount, 600.0f);
  fill_values(mip2, ChannelCount, 700.0f);

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

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = device ? GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u) : NULL;
  if (!device || !queue) {
    fprintf(stderr, "CUDA compute device creation failed\n");
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
  if (result != GPU_OK || !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA storage-geometry shader creation failed (%d)\n", result);
    goto cleanup;
  }

  if (!create_pipeline(device,
                       library,
                       shaderLayout->pipelineLayout,
                       "mutate_geometry",
                       &pipelines[0]) ||
      !create_pipeline(device,
                       library,
                       shaderLayout->pipelineLayout,
                       "read_geometry",
                       &pipelines[1])) {
    fprintf(stderr, "CUDA storage-geometry pipeline creation failed\n");
    goto cleanup;
  }

  if (!create_texture(device,
                      "cuda-storage-line",
                      GPU_TEXTURE_DIMENSION_1D,
                      4u,
                      1u,
                      1u,
                      1u,
                      &textures[0]) ||
      !create_texture(device,
                      "cuda-storage-line-array",
                      GPU_TEXTURE_DIMENSION_1D,
                      4u,
                      1u,
                      3u,
                      1u,
                      &textures[1]) ||
      !create_texture(device,
                      "cuda-storage-image-array",
                      GPU_TEXTURE_DIMENSION_2D,
                      2u,
                      2u,
                      3u,
                      1u,
                      &textures[2]) ||
      !create_texture(device,
                      "cuda-storage-volume",
                      GPU_TEXTURE_DIMENSION_3D,
                      2u,
                      2u,
                      2u,
                      1u,
                      &textures[3]) ||
      !create_texture(device,
                      "cuda-storage-mipped",
                      GPU_TEXTURE_DIMENSION_2D,
                      4u,
                      2u,
                      1u,
                      3u,
                      &textures[4])) {
    fprintf(stderr, "CUDA storage-geometry allocation failed\n");
    goto cleanup;
  }

  if (!write_texture(queue, textures[0], 4u, 1u, 1u, 0u, 1u,
                     line, sizeof(line)) ||
      !write_texture(queue, textures[1], 4u, 1u, 1u, 0u, 3u,
                     lines, sizeof(lines)) ||
      !write_texture(queue, textures[2], 2u, 2u, 1u, 0u, 3u,
                     images, sizeof(images)) ||
      !write_texture(queue, textures[3], 2u, 2u, 2u, 0u, 1u,
                     volume, sizeof(volume)) ||
      !write_texture(queue, textures[4], 4u, 2u, 1u, 0u, 1u,
                     mip0, sizeof(mip0)) ||
      !write_texture(queue, textures[4], 2u, 1u, 1u, 1u, 1u,
                     mip1, sizeof(mip1)) ||
      !write_texture(queue, textures[4], 1u, 1u, 1u, 2u, 1u,
                     mip2, sizeof(mip2))) {
    fprintf(stderr, "CUDA storage-geometry upload failed\n");
    goto cleanup;
  }

  if (!create_view(textures[0],
                   "cuda-storage-line-view",
                   GPU_TEXTURE_VIEW_1D,
                   0u,
                   1u,
                   &views[0]) ||
      !create_view(textures[1],
                   "cuda-storage-line-array-view",
                   GPU_TEXTURE_VIEW_1D_ARRAY,
                   0u,
                   3u,
                   &views[1]) ||
      !create_view(textures[2],
                   "cuda-storage-image-array-view",
                   GPU_TEXTURE_VIEW_2D_ARRAY,
                   0u,
                   3u,
                   &views[2]) ||
      !create_view(textures[3],
                   "cuda-storage-volume-view",
                   GPU_TEXTURE_VIEW_3D,
                   0u,
                   1u,
                   &views[3]) ||
      !create_view(textures[4],
                   "cuda-storage-mip-view",
                   GPU_TEXTURE_VIEW_2D,
                   1u,
                   1u,
                   &views[4])) {
    fprintf(stderr, "CUDA storage-geometry view creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "cuda-storage-geometry-output";
  bufferInfo.sizeBytes        = sizeof(output);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(device, &bufferInfo, &outputBuffer) != GPU_OK ||
      !outputBuffer ||
      GPUQueueWriteBuffer(queue,
                          outputBuffer,
                          0u,
                          output,
                          sizeof(output)) != GPU_OK) {
    fprintf(stderr, "CUDA storage-geometry output creation failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < TextureCount; i++) {
    textureEntries[i].binding     = i;
    textureEntries[i].bindingType = GPU_BINDING_STORAGE_TEXTURE;
    textureEntries[i].textureView = views[i];
  }
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-storage-geometry-resources";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = TextureCount;
  groupInfo.pEntries         = textureEntries;
  if (GPUCreateBindGroup(device, &groupInfo, &textureGroup) != GPU_OK ||
      !textureGroup) {
    fprintf(stderr, "CUDA storage-geometry bind group creation failed\n");
    goto cleanup;
  }

  outputEntry.binding       = 0u;
  outputEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  outputEntry.buffer.buffer = outputBuffer;
  outputEntry.buffer.size   = sizeof(output);
  groupInfo.label           = "cuda-storage-geometry-output";
  groupInfo.layout          = shaderLayout->bindGroupLayouts[1];
  groupInfo.entryCount      = 1u;
  groupInfo.pEntries        = &outputEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &outputGroup) != GPU_OK ||
      !outputGroup) {
    fprintf(stderr, "CUDA storage-geometry output group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "cuda-storage-geometry", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "storage-geometry"))) {
    fprintf(stderr, "CUDA storage-geometry command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipelines[0]);
  GPUBindComputeGroup(pass, 0u, textureGroup, 0u, NULL);
  GPUBindComputeGroup(pass, 1u, outputGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUBindComputePipeline(pass, pipelines[1]);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    fprintf(stderr, "CUDA storage-geometry submission failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !values_match(output)) {
    fprintf(stderr, "CUDA storage-geometry readback failed\n");
    goto cleanup;
  }
  status = 0;

cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) (void)GPUDiscardCommandBuffer(cmdb);
  GPUDestroyBindGroup(outputGroup);
  GPUDestroyBindGroup(textureGroup);
  GPUDestroyBuffer(outputBuffer);
  for (uint32_t i = 0u; i < TextureCount; i++) {
    GPUDestroyTextureView(views[i]);
    GPUDestroyTexture(textures[i]);
  }
  for (uint32_t i = 0u; i < 2u; i++) {
    GPUDestroyComputePipeline(pipelines[i]);
  }
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL storage geometry validation passed");
  }
  return status;
}
