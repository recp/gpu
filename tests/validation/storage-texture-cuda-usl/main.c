#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  TextureWidth = 8u,
  TextureHeight = 8u,
  ValueCount = TextureWidth * TextureHeight * 4u
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

static int
values_match(const float *actual, const float *input) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    if (fabsf(actual[i] - input[i] * 2.0f) > 0.0001f) {
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
  GPUTexture            *texture;
  GPUTextureView        *view;
  GPUBuffer             *outputBuffer;
  GPUBindGroup          *group;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUInstanceCreateInfo         instanceInfo = {0};
  GPUComputePipelineCreateInfo  pipelineInfo = {0};
  GPUTextureCreateInfo          textureInfo = {0};
  GPUTextureViewCreateInfo      viewInfo = {0};
  GPUTextureWriteRegion         writeRegion = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUBindGroupEntry             entries[2] = {0};
  GPUBindGroupCreateInfo        groupInfo = {0};
  GPUQueueSubmitInfo            submitInfo = {0};
  float                         input[ValueCount];
  float                         output[ValueCount];
  uint64_t                      artifactSize;
  uint32_t                      adapterCount;
  GPUResult                     result;
  int                           status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-storage-texture-cuda-usl artifact.us\n");
    return 1;
  }

  instance      = NULL;
  adapter       = NULL;
  device        = NULL;
  queue         = NULL;
  library       = NULL;
  shaderLayout  = NULL;
  writePipeline = NULL;
  readPipeline  = NULL;
  texture       = NULL;
  view          = NULL;
  outputBuffer  = NULL;
  group         = NULL;
  cmdb          = NULL;
  pass          = NULL;
  artifactSize  = 0u;
  artifact      = read_file(argv[1], &artifactSize);
  status        = 1;
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

  device = GPUCreateDeviceWithDefaultQueues(adapter);
  queue  = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!device || !queue) {
    fprintf(stderr, "CUDA compute device creation failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(device,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(device, library, &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 1u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] || !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA storage shader layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "cuda-storage-write";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "storage_write";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &writePipeline) != GPU_OK ||
      !writePipeline) {
    fprintf(stderr, "CUDA storage write pipeline creation failed\n");
    goto cleanup;
  }
  pipelineInfo.label      = "cuda-storage-readback";
  pipelineInfo.entryPoint = "storage_readback";
  if (GPUCreateComputePipeline(device,
                               &pipelineInfo,
                               &readPipeline) != GPU_OK ||
      !readPipeline) {
    fprintf(stderr, "CUDA storage readback pipeline creation failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < ValueCount; i++) {
    input[i]  = (float)i * 0.25f;
    output[i] = 0.0f;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "cuda-storage-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  textureInfo.width            = TextureWidth;
  textureInfo.height           = TextureHeight;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_STORAGE |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "CUDA storage texture creation failed\n");
    goto cleanup;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = TextureWidth;
  writeRegion.height       = TextureHeight;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = TextureWidth * 4u * sizeof(float);
  writeRegion.rowsPerImage = TextureHeight;
  if (GPUQueueWriteTexture(queue,
                           texture,
                           &writeRegion,
                           input,
                           sizeof(input)) != GPU_OK) {
    fprintf(stderr, "CUDA storage texture upload failed\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "cuda-storage-texture-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    fprintf(stderr, "CUDA storage texture view creation failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "cuda-storage-output";
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
    fprintf(stderr, "CUDA storage output buffer creation failed\n");
    goto cleanup;
  }

  entries[0].binding       = 0u;
  entries[0].bindingType   = GPU_BINDING_STORAGE_TEXTURE;
  entries[0].textureView   = view;
  entries[1].binding       = 1u;
  entries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entries[1].buffer.buffer = outputBuffer;
  entries[1].buffer.size   = sizeof(output);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-storage-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = entries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "CUDA storage bind group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "cuda-storage", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "storage-roundtrip"))) {
    fprintf(stderr, "CUDA storage command encoding failed\n");
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
    fprintf(stderr, "CUDA storage command submission failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffer,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      !values_match(output, input)) {
    fprintf(stderr, "CUDA storage texture readback validation failed\n");
    goto cleanup;
  }
  status = 0;

cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) (void)GPUDiscardCommandBuffer(cmdb);
  GPUDestroyBindGroup(group);
  GPUDestroyBuffer(outputBuffer);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  GPUDestroyComputePipeline(readPipeline);
  GPUDestroyComputePipeline(writePipeline);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL storage texture validation passed");
  }
  return status;
}
