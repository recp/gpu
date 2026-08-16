#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
  TextureWidth    = 2u,
  TextureHeight   = 2u,
  ChannelCount    = 4u,
  ValueCount      = TextureWidth * TextureHeight * ChannelCount,
  SampleOffset    = ChannelCount,
  SampleCaseCount = 5u,
  FloatCaseCount  = 3u
};

typedef struct SampleCase {
  GPUTexture     *texture;
  GPUTextureView *view;
  const char     *label;
  const void     *input;
  uint64_t        inputSize;
  GPUFormat       format;
  uint32_t        bytesPerRow;
  uint32_t        binding;
} SampleCase;

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
create_sample_case(GPUDevice *device, GPUQueue *queue, SampleCase *test) {
  GPUTextureCreateInfo     textureInfo = {0};
  GPUTextureViewCreateInfo viewInfo = {0};
  GPUTextureWriteRegion    writeRegion = {0};

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
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
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
  return GPUCreateTextureView(test->texture,
                              &viewInfo,
                              &test->view) == GPU_OK &&
         test->view;
}

static int
create_output_buffer(GPUDevice  *device,
                     const char *label,
                     uint64_t    sizeBytes,
                     GPUBuffer **outBuffer) {
  GPUBufferCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = label;
  info.sizeBytes        = sizeBytes;
  info.usage            = GPU_BUFFER_USAGE_STORAGE |
                          GPU_BUFFER_USAGE_COPY_SRC;
  return GPUCreateBuffer(device, &info, outBuffer) == GPU_OK && *outBuffer;
}

static int
values_match(const SampleCase cases[SampleCaseCount],
             const float      floatOutput[FloatCaseCount * ChannelCount],
             const uint32_t   uintOutput[ChannelCount],
             const int32_t    sintOutput[ChannelCount]) {
  const float   *floatInput;
  const uint8_t *unormInput;
  const int8_t  *snormInput;
  const uint8_t *uintInput;
  const int8_t  *sintInput;
  uint32_t       source;

  floatInput = cases[0].input;
  unormInput = cases[1].input;
  snormInput = cases[2].input;
  uintInput  = cases[3].input;
  sintInput  = cases[4].input;
  source     = SampleOffset;
  for (uint32_t i = 0u; i < ChannelCount; i++) {
    float unormExpected;
    float snormExpected;

    unormExpected = (float)unormInput[source + i] / 255.0f;
    snormExpected = (float)snormInput[source + i] / 127.0f;
    if (snormExpected < -1.0f) {
      snormExpected = -1.0f;
    }
    if (fabsf(floatOutput[i] - floatInput[source + i]) > 0.0001f ||
        fabsf(floatOutput[ChannelCount + i] - unormExpected) > 0.0001f ||
        fabsf(floatOutput[2u * ChannelCount + i] - snormExpected) >
          0.0001f ||
        uintOutput[i] != (uint32_t)uintInput[source + i] ||
        sintOutput[i] != (int32_t)sintInput[source + i]) {
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
  GPUBindGroup          *textureGroup;
  GPUBindGroup          *outputGroup;
  GPUCommandBuffer      *cmdb;
  GPUComputePassEncoder *pass;
  void                  *artifact;
  GPUComputePipeline    *pipelines[SampleCaseCount] = {0};
  GPUBuffer             *outputBuffers[3] = {0};
  GPUInstanceCreateInfo  instanceInfo = {0};
  GPUBindGroupEntry      textureEntries[SampleCaseCount] = {0};
  GPUBindGroupEntry      outputEntries[3] = {0};
  GPUBindGroupCreateInfo groupInfo = {0};
  GPUQueueSubmitInfo     submitInfo = {0};
  float                  floatInput[ValueCount];
  uint8_t                unormInput[ValueCount];
  int8_t                 snormInput[ValueCount];
  uint8_t                uintInput[ValueCount];
  int8_t                 sintInput[ValueCount];
  float                  floatOutput[FloatCaseCount * ChannelCount] = {0};
  uint32_t               uintOutput[ChannelCount] = {0};
  int32_t                sintOutput[ChannelCount] = {0};
  SampleCase             cases[SampleCaseCount] = {
    {
      .label       = "cuda-sampled-float",
      .input       = floatInput,
      .inputSize   = sizeof(floatInput),
      .format      = GPU_FORMAT_RGBA32_FLOAT,
      .bytesPerRow = TextureWidth * ChannelCount * sizeof(float),
      .binding     = 0u
    },
    {
      .label       = "cuda-sampled-unorm",
      .input       = unormInput,
      .inputSize   = sizeof(unormInput),
      .format      = GPU_FORMAT_RGBA8_UNORM,
      .bytesPerRow = TextureWidth * ChannelCount * sizeof(uint8_t),
      .binding     = 1u
    },
    {
      .label       = "cuda-sampled-snorm",
      .input       = snormInput,
      .inputSize   = sizeof(snormInput),
      .format      = GPU_FORMAT_RGBA8_SNORM,
      .bytesPerRow = TextureWidth * ChannelCount * sizeof(int8_t),
      .binding     = 2u
    },
    {
      .label       = "cuda-sampled-uint",
      .input       = uintInput,
      .inputSize   = sizeof(uintInput),
      .format      = GPU_FORMAT_RGBA8_UINT,
      .bytesPerRow = TextureWidth * ChannelCount * sizeof(uint8_t),
      .binding     = 3u
    },
    {
      .label       = "cuda-sampled-sint",
      .input       = sintInput,
      .inputSize   = sizeof(sintInput),
      .format      = GPU_FORMAT_RGBA8_SINT,
      .bytesPerRow = TextureWidth * ChannelCount * sizeof(int8_t),
      .binding     = 4u
    }
  };
  static const char * const PipelineEntries[SampleCaseCount] = {
    "fetch_float",
    "fetch_unorm",
    "fetch_snorm",
    "fetch_uint",
    "fetch_sint"
  };
  uint64_t  artifactSize;
  uint32_t  adapterCount;
  GPUResult result;
  int       status;

  if (argc != 2) {
    fprintf(stderr, "usage: gpu-sampled-format-cuda-usl artifact.us\n");
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

  for (uint32_t i = 0u; i < ValueCount; i++) {
    floatInput[i] = (float)i + 0.25f;
    unormInput[i] = (uint8_t)((i * 29u + 3u) & 255u);
    snormInput[i] = (int8_t)((int32_t)(i % 253u) - 126);
    uintInput[i]  = (uint8_t)((i * 17u + 1u) & 255u);
    sintInput[i]  = (int8_t)((int32_t)(i % 201u) - 100);
  }
  floatInput[SampleOffset]      = -2.0f;
  floatInput[SampleOffset + 1u] = -0.5f;
  floatInput[SampleOffset + 2u] = 0.25f;
  floatInput[SampleOffset + 3u] = 4.0f;
  unormInput[SampleOffset]      = 0u;
  unormInput[SampleOffset + 1u] = 85u;
  unormInput[SampleOffset + 2u] = 170u;
  unormInput[SampleOffset + 3u] = UINT8_MAX;
  snormInput[SampleOffset]      = INT8_MIN;
  snormInput[SampleOffset + 1u] = -64;
  snormInput[SampleOffset + 2u] = 0;
  snormInput[SampleOffset + 3u] = INT8_MAX;
  uintInput[SampleOffset]       = 0u;
  uintInput[SampleOffset + 1u]  = 1u;
  uintInput[SampleOffset + 2u]  = 128u;
  uintInput[SampleOffset + 3u]  = UINT8_MAX;
  sintInput[SampleOffset]       = INT8_MIN;
  sintInput[SampleOffset + 1u]  = -1;
  sintInput[SampleOffset + 2u]  = 1;
  sintInput[SampleOffset + 3u]  = INT8_MAX;

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
  if (!device) {
    fprintf(stderr, "CUDA compute device creation failed\n");
    goto cleanup;
  }
  queue = GPUGetQueue(device, GPU_QUEUE_COMPUTE, 0u);
  if (!queue) {
    fprintf(stderr, "CUDA compute queue acquisition failed\n");
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
    fprintf(stderr, "CUDA sampled-format shader creation failed (%d)\n", result);
    goto cleanup;
  }
  result = GPUCreateShaderLayout(device, library, &shaderLayout);
  if (result != GPU_OK || !shaderLayout ||
      shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA sampled-format layout creation failed (%d)\n", result);
    goto cleanup;
  }

  for (uint32_t i = 0u; i < SampleCaseCount; i++) {
    if (!create_pipeline(device,
                         library,
                         shaderLayout->pipelineLayout,
                         PipelineEntries[i],
                         &pipelines[i]) ||
        !create_sample_case(device, queue, &cases[i])) {
      fprintf(stderr,
              "CUDA sampled-format resource setup failed: %s\n",
              cases[i].label);
      goto cleanup;
    }
    textureEntries[i].binding     = cases[i].binding;
    textureEntries[i].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    textureEntries[i].textureView = cases[i].view;
  }

  if (!create_output_buffer(device,
                            "cuda-sampled-float-output",
                            sizeof(floatOutput),
                            &outputBuffers[0]) ||
      !create_output_buffer(device,
                            "cuda-sampled-uint-output",
                            sizeof(uintOutput),
                            &outputBuffers[1]) ||
      !create_output_buffer(device,
                            "cuda-sampled-sint-output",
                            sizeof(sintOutput),
                            &outputBuffers[2])) {
    fprintf(stderr, "CUDA sampled-format output setup failed\n");
    goto cleanup;
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "cuda-sampled-format-textures";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = textureEntries;
  groupInfo.entryCount       = SampleCaseCount;
  if (GPUCreateBindGroup(device, &groupInfo, &textureGroup) != GPU_OK ||
      !textureGroup) {
    fprintf(stderr, "CUDA sampled-format texture group failed\n");
    goto cleanup;
  }

  for (uint32_t i = 0u; i < 3u; i++) {
    outputEntries[i].binding       = i;
    outputEntries[i].bindingType   = GPU_BINDING_STORAGE_BUFFER;
    outputEntries[i].buffer.buffer = outputBuffers[i];
    outputEntries[i].buffer.size   = i == 0u
                                       ? sizeof(floatOutput)
                                       : i == 1u
                                           ? sizeof(uintOutput)
                                           : sizeof(sintOutput);
  }
  groupInfo.label      = "cuda-sampled-format-outputs";
  groupInfo.layout     = shaderLayout->bindGroupLayouts[1];
  groupInfo.pEntries   = outputEntries;
  groupInfo.entryCount = 3u;
  if (GPUCreateBindGroup(device, &groupInfo, &outputGroup) != GPU_OK ||
      !outputGroup) {
    fprintf(stderr, "CUDA sampled-format output group failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "cuda-sampled-format", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "sampled-format-fetch"))) {
    fprintf(stderr, "CUDA sampled-format command encoding failed\n");
    goto cleanup;
  }
  GPUBindComputePipeline(pass, pipelines[0]);
  GPUBindComputeGroup(pass, 0u, textureGroup, 0u, NULL);
  GPUBindComputeGroup(pass, 1u, outputGroup, 0u, NULL);
  GPUDispatch(pass, 1u, 1u, 1u);
  for (uint32_t i = 1u; i < SampleCaseCount; i++) {
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
    fprintf(stderr, "CUDA sampled-format submission failed\n");
    goto cleanup;
  }
  cmdb = NULL;

  if (GPUQueueReadBuffer(queue,
                         outputBuffers[0],
                         0u,
                         floatOutput,
                         sizeof(floatOutput)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         outputBuffers[1],
                         0u,
                         uintOutput,
                         sizeof(uintOutput)) != GPU_OK ||
      GPUQueueReadBuffer(queue,
                         outputBuffers[2],
                         0u,
                         sintOutput,
                         sizeof(sintOutput)) != GPU_OK ||
      !values_match(cases, floatOutput, uintOutput, sintOutput)) {
    fprintf(stderr, "CUDA sampled-format readback validation failed\n");
    goto cleanup;
  }
  status = 0;

cleanup:
  if (pass) GPUEndComputePass(pass);
  if (cmdb) (void)GPUDiscardCommandBuffer(cmdb);
  GPUDestroyBindGroup(outputGroup);
  GPUDestroyBindGroup(textureGroup);
  for (uint32_t i = 0u; i < 3u; i++) {
    GPUDestroyBuffer(outputBuffers[i]);
  }
  for (uint32_t i = 0u; i < SampleCaseCount; i++) {
    GPUDestroyTextureView(cases[i].view);
    GPUDestroyTexture(cases[i].texture);
    GPUDestroyComputePipeline(pipelines[i]);
  }
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  free(artifact);

  if (status == 0) {
    puts("CUDA USL sampled texture format validation passed");
  }
  return status;
}
