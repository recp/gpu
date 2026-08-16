#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  TextureWidth  = 2u,
  TextureHeight = 2u,
  TexelWidth    = 4u,
  OutputCount   = 20u
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

static int
values_match(const float output[OutputCount], const float input[16]) {
  for (uint32_t i = 0u; i < TexelWidth; i++) {
    if (fabsf(output[i] - input[i]) > 0.0001f ||
        fabsf(output[TexelWidth + i] - input[12u + i]) > 0.0001f ||
        fabsf(output[2u * TexelWidth + i] - input[i]) > 0.0001f ||
        fabsf(output[3u * TexelWidth + i] - input[TexelWidth + i]) > 0.0001f) {
      return 0;
    }
  }
  return fabsf(output[4u * TexelWidth] - 1.0f) <= 0.0001f &&
         fabsf(output[4u * TexelWidth + 1u]) <= 0.0001f &&
         fabsf(output[4u * TexelWidth + 2u]) <= 0.0001f &&
         fabsf(output[4u * TexelWidth + 3u] - 1.0f) <= 0.0001f;
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

int
main(int argc, char **argv) {
  GPUInstance           *instance;
  GPUAdapter            *adapter;
  GPUDevice             *device;
  GPUQueue              *queue;
  GPUShaderLibrary      *library;
  GPUShaderLayout       *shaderLayout;
  GPUComputePipeline    *dynamicPipeline;
  GPUComputePipeline    *staticPipeline;
  GPUComputePipeline    *secondStaticPipeline;
  GPUComputePipeline    *fetchPipeline;
  GPUComputePipeline    *queryPipeline;
  GPUTexture            *texture;
  GPUTextureView        *view;
  GPUSampler            *sampler;
  GPUBuffer             *outputBuffer;
  GPUBindGroup          *textureGroup;
  GPUBindGroup          *samplerGroup;
  GPUBindGroup          *outputGroup;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUInstanceCreateInfo        instanceInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUTextureCreateInfo         textureInfo = {0};
  GPUTextureViewCreateInfo     viewInfo = {0};
  GPUTextureWriteRegion        writeRegion = {0};
  GPUSamplerCreateInfo         samplerInfo = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUBindGroupEntry            textureEntry = {0};
  GPUBindGroupEntry            samplerEntry = {0};
  GPUBindGroupEntry            outputEntry = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUQueueSubmitInfo           submitInfo = {0};
  const float input[16] = {
    1.0f,  2.0f,  3.0f,  4.0f,
    5.0f,  6.0f,  7.0f,  8.0f,
    9.0f, 10.0f, 11.0f, 12.0f,
   13.0f, 14.0f, 15.0f, 16.0f
  };
  float     output[OutputCount] = {0};
  uint64_t  artifactSize;
  uint32_t  adapterCount;
  GPUResult result;
  int       status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-sampled-texture-cuda-usl artifact.us\n");
    return 1;
  }

  instance             = NULL;
  adapter              = NULL;
  device               = NULL;
  queue                = NULL;
  library              = NULL;
  shaderLayout         = NULL;
  dynamicPipeline      = NULL;
  staticPipeline       = NULL;
  secondStaticPipeline = NULL;
  fetchPipeline        = NULL;
  queryPipeline        = NULL;
  texture              = NULL;
  view                 = NULL;
  sampler              = NULL;
  outputBuffer         = NULL;
  textureGroup         = NULL;
  samplerGroup         = NULL;
  outputGroup          = NULL;
  cmdb                 = NULL;
  pass                 = NULL;
  artifactSize         = 0u;
  artifact             = read_file(argv[1], &artifactSize);
  status               = 1;
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }
  if (!validate_ptx_metadata(artifact, artifactSize)) {
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

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
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
  if (result != GPU_OK || !library) {
    fprintf(stderr,
            "CUDA sampled-texture shader library creation failed (%d)\n",
            result);
    goto cleanup;
  }
  result = GPUCreateShaderLayout(device, library, &shaderLayout);
  if (result != GPU_OK || !shaderLayout) {
    fprintf(stderr,
            "CUDA sampled-texture shader layout creation failed (%d)\n",
            result);
    goto cleanup;
  }
  if (shaderLayout->bindGroupLayoutCount != 3u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->bindGroupLayouts[2] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr,
            "CUDA sampled-texture shader layout mismatch: %u groups\n",
            shaderLayout->bindGroupLayoutCount);
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "cuda-sampled-dynamic";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "sample_dynamic";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &dynamicPipeline) != GPU_OK ||
      !dynamicPipeline) {
    fprintf(stderr, "CUDA dynamic-sampler pipeline creation failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "cuda-sampled-static";
  pipelineInfo.entryPoint = "sample_static";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &staticPipeline) != GPU_OK ||
      !staticPipeline) {
    fprintf(stderr, "CUDA static-sampler pipeline creation failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "cuda-sampled-static-again";
  pipelineInfo.entryPoint = "sample_static_again";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &secondStaticPipeline) != GPU_OK ||
      !secondStaticPipeline) {
    fprintf(stderr, "CUDA second static-sampler pipeline creation failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "cuda-texture-fetch";
  pipelineInfo.entryPoint = "fetch_exact";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &fetchPipeline) != GPU_OK ||
      !fetchPipeline) {
    fprintf(stderr, "CUDA exact-fetch pipeline creation failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "cuda-texture-query";
  pipelineInfo.entryPoint = "query_metadata";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &queryPipeline) != GPU_OK ||
      !queryPipeline) {
    fprintf(stderr, "CUDA texture-query pipeline creation failed\n");
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "cuda-sampled-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  textureInfo.width            = TextureWidth;
  textureInfo.height           = TextureHeight;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "CUDA sampled texture creation failed\n");
    goto cleanup;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = TextureWidth;
  writeRegion.height       = TextureHeight;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = TextureWidth * TexelWidth * sizeof(float);
  writeRegion.rowsPerImage = TextureHeight;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           input,
                           sizeof(input)) != GPU_OK) {
    fprintf(stderr, "CUDA sampled texture upload failed\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "cuda-sampled-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    fprintf(stderr, "CUDA sampled texture view creation failed\n");
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "cuda-nearest-sampler";
  samplerInfo.desc.minFilter     = GPU_FILTER_NEAREST;
  samplerInfo.desc.magFilter     = GPU_FILTER_NEAREST;
  samplerInfo.desc.mipFilter     = GPU_MIP_FILTER_NEAREST;
  samplerInfo.desc.addressU      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressV      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.addressW      = GPU_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.desc.maxAnisotropy = 1u;
  if (GPUCreateSampler(device,
                       &samplerInfo,
                       false,
                       &sampler) != GPU_OK ||
      !sampler) {
    fprintf(stderr, "CUDA sampler creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "cuda-sampled-output";
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
    fprintf(stderr, "CUDA sampled output buffer creation failed\n");
    goto cleanup;
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.entryCount       = 1u;

  textureEntry.binding       = 3u;
  textureEntry.bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  textureEntry.textureView   = view;
  groupInfo.label            = "cuda-sampled-texture-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &textureEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &textureGroup) != GPU_OK ||
      !textureGroup) {
    fprintf(stderr, "CUDA sampled texture bind group creation failed\n");
    goto cleanup;
  }

  samplerEntry.binding     = 7u;
  samplerEntry.bindingType = GPU_BINDING_SAMPLER;
  samplerEntry.sampler     = sampler;
  groupInfo.label          = "cuda-sampled-sampler-group";
  groupInfo.layout         = shaderLayout->bindGroupLayouts[1];
  groupInfo.pEntries       = &samplerEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &samplerGroup) != GPU_OK ||
      !samplerGroup) {
    fprintf(stderr, "CUDA sampler bind group creation failed\n");
    goto cleanup;
  }

  outputEntry.binding       = 2u;
  outputEntry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  outputEntry.buffer.buffer = outputBuffer;
  outputEntry.buffer.size   = sizeof(output);
  groupInfo.label           = "cuda-sampled-output-group";
  groupInfo.layout          = shaderLayout->bindGroupLayouts[2];
  groupInfo.pEntries        = &outputEntry;
  if (GPUCreateBindGroup(device, &groupInfo, &outputGroup) != GPU_OK ||
      !outputGroup) {
    fprintf(stderr, "CUDA sampled output bind group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "cuda-sampled", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "sampled-roundtrip"))) {
    fprintf(stderr, "CUDA sampled command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, dynamicPipeline);
  GPUBindComputeGroup(pass, 0u, textureGroup, 0u, NULL);
  GPUBindComputeGroup(pass, 1u, samplerGroup, 0u, NULL);
  GPUBindComputeGroup(pass, 2u, outputGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUBindComputePipeline(pass, staticPipeline);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUBindComputePipeline(pass, secondStaticPipeline);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUBindComputePipeline(pass, fetchPipeline);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUBindComputePipeline(pass, queryPipeline);
  GPUDispatch(pass, 1u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK) {
    fprintf(stderr, "CUDA sampled command submission failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !values_match(output, input)) {
    fprintf(stderr, "CUDA sampled texture readback validation failed\n");
    goto cleanup;
  }
  status = 0;

cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) (void)GPUDiscardCommandBuffer(cmdb);
  GPUDestroyBindGroup(outputGroup);
  GPUDestroyBindGroup(samplerGroup);
  GPUDestroyBindGroup(textureGroup);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroySampler(sampler);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  GPUDestroyComputePipeline(queryPipeline);
  GPUDestroyComputePipeline(fetchPipeline);
  GPUDestroyComputePipeline(secondStaticPipeline);
  GPUDestroyComputePipeline(staticPipeline);
  GPUDestroyComputePipeline(dynamicPipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL sampled texture validation passed");
  }
  return status;
}
