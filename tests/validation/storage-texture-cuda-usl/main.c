#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  TextureWidth     = 8u,
  TextureHeight    = 8u,
  ChannelCount     = 4u,
  ValueCount       = TextureWidth * TextureHeight * ChannelCount,
  StorageCaseCount = 5u,
  PipelineCount    = StorageCaseCount * 2u
};

typedef struct StorageCase {
  GPUTexture     *texture;
  GPUTextureView *view;
  GPUBuffer      *buffer;
  const char     *label;
  const void     *input;
  void           *output;
  uint64_t        inputSize;
  uint64_t        outputSize;
  GPUFormat       format;
  uint32_t        bytesPerRow;
  uint32_t        imageBinding;
  uint32_t        bufferBinding;
} StorageCase;

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

static int
create_storage_case(GPUDevice *device, GPUQueue *queue, StorageCase *test) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};
  GPUBufferCreateInfo      bufferInfo = {0};

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = test->label;
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = test->format;
  textureInfo.width            = TextureWidth;
  textureInfo.height           = TextureHeight;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_STORAGE |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &test->texture) != GPU_OK ||
      !test->texture) {
    return 0;
  }

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = TextureWidth;
  writeRegion.height       = TextureHeight;
  writeRegion.depth        = 1u;
  writeRegion.layerCount   = 1u;
  writeRegion.bytesPerRow  = test->bytesPerRow;
  writeRegion.rowsPerImage = TextureHeight;
  if (GPUQueueWriteTexture(queue,
                           test->texture,
                           &writeRegion,
                           test->input,
                           test->inputSize) != GPU_OK) {
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = test->label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = test->format;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(test->texture, &viewInfo, &test->view) != GPU_OK ||
      !test->view) {
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = test->label;
  bufferInfo.sizeBytes        = test->outputSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC;
  return GPUCreateBuffer(device, &bufferInfo, &test->buffer) == GPU_OK &&
         test->buffer;
}

static int
values_match(const StorageCase *test) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    switch (test->format) {
      case GPU_FORMAT_RGBA32_FLOAT: {
        const float *input;
        const float *output;

        input  = test->input;
        output = test->output;
        if (fabsf(output[i] - input[i] * 2.0f) > 0.0001f) {
          return 0;
        }
        break;
      }
      case GPU_FORMAT_RGBA8_UNORM: {
        const uint8_t *input;
        const float   *output;
        float          expected;

        input    = test->input;
        output   = test->output;
        expected = (float)(255u - input[i]) / 255.0f;
        if (fabsf(output[i] - expected) > 0.5f / 255.0f + 0.000001f) {
          return 0;
        }
        break;
      }
      case GPU_FORMAT_RGBA8_SNORM: {
        const int8_t *input;
        const float  *output;
        float         expected;

        input    = test->input;
        output   = test->output;
        expected = -(float)input[i] / 127.0f;
        if (fabsf(output[i] - expected) > 0.5f / 127.0f + 0.000001f) {
          return 0;
        }
        break;
      }
      case GPU_FORMAT_RGBA8_UINT: {
        const uint8_t  *input;
        const uint32_t *output;

        input  = test->input;
        output = test->output;
        if (output[i] != (uint32_t)input[i] + 1u) {
          return 0;
        }
        break;
      }
      case GPU_FORMAT_RGBA8_SINT: {
        const int8_t  *input;
        const int32_t *output;

        input  = test->input;
        output = test->output;
        if (output[i] != (int32_t)input[i] + 7) {
          return 0;
        }
        break;
      }
      default:
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
  GPUBindGroup          *group;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUComputePipeline    *pipelines[PipelineCount] = {0};
  GPUInstanceCreateInfo  instanceInfo = {0};
  GPUBindGroupEntry      entries[StorageCaseCount * 2u] = {0};
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  float                  floatInput[ValueCount];
  float                  floatOutput[ValueCount] = {0};
  float                  unormOutput[ValueCount] = {0};
  float                  snormOutput[ValueCount] = {0};
  uint32_t               uintOutput[ValueCount] = {0};
  int32_t                sintOutput[ValueCount] = {0};
  uint8_t                unormInput[ValueCount];
  int8_t                 snormInput[ValueCount];
  uint8_t                uintInput[ValueCount];
  int8_t                 sintInput[ValueCount];
  StorageCase            cases[StorageCaseCount] = {
    {
      .label         = "cuda-storage-float",
      .input         = floatInput,
      .output        = floatOutput,
      .inputSize     = sizeof(floatInput),
      .outputSize    = sizeof(floatOutput),
      .format        = GPU_FORMAT_RGBA32_FLOAT,
      .bytesPerRow   = TextureWidth * ChannelCount * sizeof(float),
      .imageBinding  = 0u,
      .bufferBinding = 1u
    },
    {
      .label         = "cuda-storage-unorm",
      .input         = unormInput,
      .output        = unormOutput,
      .inputSize     = sizeof(unormInput),
      .outputSize    = sizeof(unormOutput),
      .format        = GPU_FORMAT_RGBA8_UNORM,
      .bytesPerRow   = TextureWidth * ChannelCount * sizeof(uint8_t),
      .imageBinding  = 2u,
      .bufferBinding = 3u
    },
    {
      .label         = "cuda-storage-snorm",
      .input         = snormInput,
      .output        = snormOutput,
      .inputSize     = sizeof(snormInput),
      .outputSize    = sizeof(snormOutput),
      .format        = GPU_FORMAT_RGBA8_SNORM,
      .bytesPerRow   = TextureWidth * ChannelCount * sizeof(int8_t),
      .imageBinding  = 4u,
      .bufferBinding = 5u
    },
    {
      .label         = "cuda-storage-uint",
      .input         = uintInput,
      .output        = uintOutput,
      .inputSize     = sizeof(uintInput),
      .outputSize    = sizeof(uintOutput),
      .format        = GPU_FORMAT_RGBA8_UINT,
      .bytesPerRow   = TextureWidth * ChannelCount * sizeof(uint8_t),
      .imageBinding  = 6u,
      .bufferBinding = 7u
    },
    {
      .label         = "cuda-storage-sint",
      .input         = sintInput,
      .output        = sintOutput,
      .inputSize     = sizeof(sintInput),
      .outputSize    = sizeof(sintOutput),
      .format        = GPU_FORMAT_RGBA8_SINT,
      .bytesPerRow   = TextureWidth * ChannelCount * sizeof(int8_t),
      .imageBinding  = 8u,
      .bufferBinding = 9u
    }
  };
  static const char * const PipelineEntries[PipelineCount] = {
    "storage_float_write",
    "storage_float_readback",
    "storage_unorm_write",
    "storage_unorm_readback",
    "storage_snorm_write",
    "storage_snorm_readback",
    "storage_uint_write",
    "storage_uint_readback",
    "storage_sint_write",
    "storage_sint_readback"
  };
  uint64_t  artifactSize;
  uint32_t  adapterCount;
  GPUResult result;
  int       status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-storage-texture-cuda-usl artifact.us\n");
    return 1;
  }

  instance     = NULL;
  adapter      = NULL;
  device       = NULL;
  queue        = NULL;
  library      = NULL;
  shaderLayout = NULL;
  group        = NULL;
  cmdb         = NULL;
  pass         = NULL;
  artifactSize = 0u;
  artifact     = read_file(argv[1], &artifactSize);
  status       = 1;
  if (!artifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }
  if (!validate_ptx_metadata(artifact, artifactSize)) {
    goto cleanup;
  }

  for (uint32_t i = 0u; i < ValueCount; i++) {
    floatInput[i] = (float)i * 0.25f;
    unormInput[i] = (uint8_t)((i * 37u + 11u) & 255u);
    snormInput[i] = (int8_t)((int32_t)(i % 253u) - 126);
    uintInput[i]  = (uint8_t)(i % 251u);
    sintInput[i]  = (int8_t)((int32_t)(i % 201u) - 100);
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

  for (uint32_t i = 0u; i < PipelineCount; i++) {
    if (!create_pipeline(device,
                         library,
                         shaderLayout->pipelineLayout,
                         PipelineEntries[i],
                         &pipelines[i])) {
      fprintf(stderr,
              "CUDA storage pipeline creation failed: %s\n",
              PipelineEntries[i]);
      goto cleanup;
    }
  }

  for (uint32_t i = 0u; i < StorageCaseCount; i++) {
    if (!create_storage_case(device, queue, &cases[i])) {
      fprintf(stderr,
              "CUDA storage resource creation failed: %s\n",
              cases[i].label);
      goto cleanup;
    }
    entries[i * 2u].binding       = cases[i].imageBinding;
    entries[i * 2u].bindingType   = GPU_BINDING_STORAGE_TEXTURE;
    entries[i * 2u].textureView   = cases[i].view;
    entries[i * 2u + 1u].binding       = cases[i].bufferBinding;
    entries[i * 2u + 1u].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    entries[i * 2u + 1u].buffer.buffer = cases[i].buffer;
    entries[i * 2u + 1u].buffer.size   = cases[i].outputSize;
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-storage-formats";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = StorageCaseCount * 2u;
  groupInfo.pEntries         = entries;
  if (GPUCreateBindGroup(device, &groupInfo, &group) != GPU_OK || !group) {
    fprintf(stderr, "CUDA storage bind group creation failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "cuda-storage", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "storage-formats"))) {
    fprintf(stderr, "CUDA storage command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipelines[0]);
  GPUBindComputeGroup(pass, 0u, group, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  for (uint32_t i = 1u; i < PipelineCount; i++) {
    GPUBindComputePipeline(pass, pipelines[i]);
    GPUDispatch(pass, 1u, 1u, 1u);
  }
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

  for (uint32_t i = 0u; i < StorageCaseCount; i++) {
    if (GPUQueueReadBuffer(queue,
                          cases[i].buffer,
                          0u,
                          cases[i].output,
                          cases[i].outputSize) != GPU_OK) {
      fprintf(stderr, "CUDA storage readback failed: %s\n", cases[i].label);
      goto cleanup;
    }
    if (!values_match(&cases[i])) {
      fprintf(stderr,
              "CUDA storage value mismatch: %s\n",
              cases[i].label);
      goto cleanup;
    }
  }
  status = 0;

cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) (void)GPUDiscardCommandBuffer(cmdb);
  GPUDestroyBindGroup(group);
  for (uint32_t i = 0u; i < StorageCaseCount; i++) {
    GPUDestroyBuffer(cases[i].buffer);
    GPUDestroyTextureView(cases[i].view);
    GPUDestroyTexture(cases[i].texture);
  }
  for (uint32_t i = 0u; i < PipelineCount; i++) {
    GPUDestroyComputePipeline(pipelines[i]);
  }
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL storage texture format validation passed");
  }
  return status;
}
