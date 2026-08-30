#include <gpu/gpu.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  ValueCount                    = 512u,
  RoundtripCount                = 4u,
  TextureBaseWidth              = 16u,
  TextureBaseHeight             = 16u,
  TextureWidth                  = 8u,
  TextureHeight                 = 8u,
  TextureLayers                 = 2u,
  CubeLayers                    = 6u,
  CubeArrayLayers               = 12u,
  TextureMipLevel               = 1u,
  TextureMipCount               = 4u,
  TextureTexelCount             = TextureWidth * TextureHeight * TextureLayers,
  TextureValueCount             = TextureTexelCount * 4u,
  CubeFaceValueCount            = TextureWidth * TextureHeight * 4u,
  CubeValueCount                = CubeFaceValueCount * CubeLayers,
  CubeOutputValueCount          = CubeLayers * 4u,
  CubeArrayValueCount           = CubeFaceValueCount * CubeArrayLayers,
  CubeArrayOutputValueCount     = CubeArrayLayers * 4u,
  HalfTextureValueCount         = TextureValueCount,
  HalfFloatOutputValueCount     = HalfTextureValueCount * 2u,
  ByteTextureValueCount         = TextureValueCount,
  ByteFloatOutputValueCount     = ByteTextureValueCount * 2u,
  FormatFloatOutputValueCount   = HalfFloatOutputValueCount +
                                  ByteFloatOutputValueCount * 3u,
  TextureOutputValueCount       = TextureValueCount * 2u +
                                  CubeOutputValueCount +
                                  CubeArrayOutputValueCount +
                                  FormatFloatOutputValueCount
};

typedef enum InteropFormatCase {
  InteropFormatHalf,
  InteropFormatUnorm,
  InteropFormatUint,
  InteropFormatSint,
  InteropFormatCount
} InteropFormatCase;

typedef struct Params {
  float scale;
  float bias;
} Params;

typedef struct AdapterList {
  GPUAdapter **items;
  uint32_t     count;
} AdapterList;

typedef struct InteropFormatTexture {
  GPUTexture     *graphicsTexture;
  GPUTexture     *cudaTexture;
  GPUTextureView *storageView;
  GPUTextureView *sampledView;
  GPUBuffer      *readback;
} InteropFormatTexture;

typedef struct InteropFormatTransfer {
  const void *input;
  void       *output;
  uint64_t    size;
  uint32_t    bytesPerRow;
} InteropFormatTransfer;

typedef struct RoundtripState {
  GPUDeviceInteropEXT *interop;
  GPUDevice           *graphicsDevice;
  GPUDevice           *cudaDevice;
  GPUQueue            *graphicsQueue;
  GPUQueue            *cudaQueue;
  GPUBuffer           *graphicsBuffer;
  GPUBuffer           *cudaBuffer;
  GPUBuffer           *paramsBuffer;
  GPUBuffer           *textureReadback;
  GPUBuffer           *textureCudaReadback;
  GPUTexture          *graphicsTexture;
  GPUTexture          *cudaTexture;
  GPUTexture          *graphicsCubeTexture;
  GPUTexture          *cudaCubeTexture;
  GPUTexture          *graphicsCubeArrayTexture;
  GPUTexture          *cudaCubeArrayTexture;
  GPUTextureView      *cudaTextureView;
  GPUTextureView      *cudaSampledTextureView;
  GPUTextureView      *cudaCubeTextureView;
  GPUTextureView      *cudaCubeArrayTextureView;
  GPUSemaphore        *graphicsSemaphore;
  GPUSemaphore        *cudaSemaphore;
  GPUFence            *releaseFence;
  GPUFence            *acquireFence;
  GPUComputePipeline  *pipeline;
  GPUComputePipeline  *texturePipeline;
  GPUBindGroup        *paramsGroup;
  GPUBindGroup        *dataGroup;
  GPUBindGroup        *textureGroup;
  uint32_t             textureMipLevel;
  InteropFormatTexture formatTextures[InteropFormatCount];
} RoundtripState;

static void
device_error(GPUDevice                *device,
             const GPUDeviceErrorInfo *error,
             void                     *userdata) {
  (void)device;
  (void)userdata;
  fprintf(stderr,
          "CUDA interop device error: %s\n",
          error && error->message ? error->message : "unknown error");
}

static void *
read_file(const char *path, uint64_t *outSize) {
  FILE *file;
  void *data;
  long  size;

  file = path ? fopen(path, "rb") : NULL;
  if (!file || fseek(file, 0, SEEK_END) != 0 ||
      (size = ftell(file)) <= 0 || fseek(file, 0, SEEK_SET) != 0) {
    if (file) {
      fclose(file);
    }
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

static GPUResult
enumerate_adapters(GPUInstance *instance, AdapterList *outList) {
  GPUResult result;
  uint32_t  count;

  if (!instance || !outList) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  outList->items = NULL;
  outList->count = 0u;
  count          = 0u;
  result = GPUEnumerateAdapters(instance, &count, NULL);
  if (result != GPU_OK || count == 0u) {
    return result != GPU_OK ? result : GPU_ERROR_UNSUPPORTED;
  }

  outList->items = calloc(count, sizeof(*outList->items));
  if (!outList->items) {
    return GPU_ERROR_OUT_OF_MEMORY;
  }
  outList->count = count;
  result = GPUEnumerateAdapters(instance, &count, outList->items);
  if (result != GPU_OK || count != outList->count) {
    free(outList->items);
    outList->items = NULL;
    outList->count = 0u;
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  return GPU_OK;
}

static int
find_matching_adapters(const AdapterList *graphicsAdapters,
                       const AdapterList *cudaAdapters,
                       GPUAdapter       **outGraphicsAdapter,
                       GPUAdapter       **outCudaAdapter) {
  for (uint32_t graphicsIndex = 0u;
       graphicsIndex < graphicsAdapters->count;
       graphicsIndex++) {
    for (uint32_t cudaIndex = 0u;
         cudaIndex < cudaAdapters->count;
         cudaIndex++) {
      bool sameDevice;

      sameDevice = false;
      if (GPUAdaptersSharePhysicalDevice(
            graphicsAdapters->items[graphicsIndex],
            cudaAdapters->items[cudaIndex],
            &sameDevice
          ) == GPU_OK && sameDevice) {
        *outGraphicsAdapter = graphicsAdapters->items[graphicsIndex];
        *outCudaAdapter     = cudaAdapters->items[cudaIndex];
        return 1;
      }
    }
  }
  return 0;
}

static int
create_interop_format_texture(RoundtripState            *state,
                              InteropFormatCase          formatCase,
                              const GPUTextureCreateInfo *graphicsTemplate,
                              GPUFormat                  format,
                              const char                *label,
                              uint64_t                   readbackSize,
                              bool                       cudaFirst) {
  InteropFormatTexture     *texture;
  GPUTextureCreateInfo      graphicsInfo, cudaInfo;
  GPUTextureViewCreateInfo  viewInfo = {0};
  GPUBufferCreateInfo       bufferInfo = {0};
  GPUMemoryRequirements     requirements;
  GPUResult                 requirementsResult, createResult;

  if (!state || formatCase >= InteropFormatCount || !graphicsTemplate ||
      !label || readbackSize == 0u) {
    return 0;
  }
  texture                 = &state->formatTextures[formatCase];
  graphicsInfo            = *graphicsTemplate;
  graphicsInfo.label      = label;
  graphicsInfo.format     = format;
  cudaInfo                = graphicsInfo;
  cudaInfo.usage          = GPU_TEXTURE_USAGE_SAMPLED |
                            GPU_TEXTURE_USAGE_STORAGE;
  requirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state->interop,
    cudaFirst ? &cudaInfo : &graphicsInfo,
    cudaFirst ? &graphicsInfo : &cudaInfo,
    &requirements
  );
  createResult = requirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(state->interop,
                                cudaFirst ? &cudaInfo : &graphicsInfo,
                                cudaFirst ? &graphicsInfo : &cudaInfo,
                                cudaFirst
                                  ? &texture->cudaTexture
                                  : &texture->graphicsTexture,
                                cudaFirst
                                  ? &texture->graphicsTexture
                                  : &texture->cudaTexture)
    : requirementsResult;
  if (requirementsResult != GPU_OK || requirements.sizeBytes == 0u ||
      createResult != GPU_OK || !texture->graphicsTexture ||
      !texture->cudaTexture) {
    fprintf(stderr,
            "shared graphics/CUDA %s creation failed (%d, %d)\n",
            label,
            requirementsResult,
            createResult);
    return 0;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = label;
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  viewInfo.format           = format;
  viewInfo.baseMipLevel     = state->textureMipLevel;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = TextureLayers;
  if (GPUCreateTextureView(texture->cudaTexture,
                           &viewInfo,
                           &texture->storageView) != GPU_OK ||
      !texture->storageView) {
    fprintf(stderr, "shared graphics/CUDA %s storage view failed\n", label);
    return 0;
  }
  viewInfo.mipLevelCount = graphicsInfo.mipLevelCount -
                           state->textureMipLevel;
  if (GPUCreateTextureView(texture->cudaTexture,
                           &viewInfo,
                           &texture->sampledView) != GPU_OK ||
      !texture->sampledView) {
    fprintf(stderr, "shared graphics/CUDA %s sampled view failed\n", label);
    return 0;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = label;
  bufferInfo.sizeBytes        = readbackSize;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(state->graphicsDevice,
                      &bufferInfo,
                      &texture->readback) != GPU_OK ||
      !texture->readback) {
    fprintf(stderr, "shared graphics/CUDA %s readback failed\n", label);
    return 0;
  }
  return 1;
}

static int
values_match(const float values[ValueCount]) {
  for (uint32_t i = 0u; i < ValueCount; i++) {
    const float expected = (float)i * 2.0f + 1.0f;

    if (fabsf(values[i] - expected) > 0.0001f) {
      return 0;
    }
  }
  return 1;
}

static int
run_roundtrip(RoundtripState *state, uint32_t iteration) {
  GPUCommandBuffer          *releaseCmdb, *cudaCmdb, *acquireCmdb;
  GPUComputePassEncoder     *pass;
  GPUSharedBufferBarrierEXT  toCuda = {0}, toGraphics = {0};
  GPUSharedBarrierBatchEXT   acquireCuda = {0}, acquireGraphics = {0};
  GPUQueueSemaphoreWait      wait = {0};
  GPUQueueSemaphoreSignal    signal = {0};
  GPUQueueSubmitExInfo       submit = {0};
  float                      values[ValueCount];
  uint64_t                   cudaValue, graphicsValue;
  uint32_t                   dynamicOffset;
  int                        releaseSubmitted, cudaSubmitted, ok;

  releaseCmdb      = NULL;
  cudaCmdb         = NULL;
  acquireCmdb      = NULL;
  pass             = NULL;
  releaseSubmitted = 0;
  cudaSubmitted    = 0;
  ok               = 0;
  cudaValue        = (uint64_t)iteration * 2u + 1u;
  graphicsValue    = cudaValue + 1u;
  for (uint32_t i = 0u; i < ValueCount; i++) {
    values[i] = (float)i;
  }

  if (GPUQueueWriteBuffer(state->graphicsQueue,
                          state->graphicsBuffer,
                          0u,
                          values,
                          sizeof(values)) != GPU_OK ||
      GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-release",
                              &releaseCmdb) != GPU_OK ||
      !releaseCmdb) {
    goto cleanup;
  }

  toCuda.sourceBuffer      = state->graphicsBuffer;
  toCuda.destinationBuffer = state->cudaBuffer;
  toCuda.sizeBytes         = sizeof(values);
  toCuda.srcAccess         = GPU_ACCESS_TRANSFER_WRITE;
  toCuda.dstAccess         = GPU_ACCESS_SHADER_WRITE;
  acquireCuda.pBufferBarriers    = &toCuda;
  acquireCuda.srcStages          = GPU_STAGE_TRANSFER;
  acquireCuda.dstStages          = GPU_STAGE_COMPUTE;
  acquireCuda.bufferBarrierCount = 1u;
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                releaseCmdb,
                                &acquireCuda) != GPU_OK) {
    goto cleanup;
  }

  signal.semaphore          = state->graphicsSemaphore;
  signal.value              = cudaValue;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = &releaseCmdb;
  submit.pSignals           = &signal;
  submit.fence              = state->releaseFence;
  submit.commandBufferCount = 1u;
  submit.signalCount        = 1u;
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  releaseCmdb      = NULL;
  releaseSubmitted = 1;

  if (GPUAcquireCommandBuffer(state->cudaQueue,
                              "cuda-graphics-roundtrip",
                              &cudaCmdb) != GPU_OK ||
      !cudaCmdb ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                cudaCmdb,
                                &acquireCuda) != GPU_OK ||
      !(pass = GPUBeginComputePass(cudaCmdb, "cuda-saxpy"))) {
    goto cleanup;
  }
  GPUBindComputePipeline(pass, state->pipeline);
  dynamicOffset = 0u;
  GPUBindComputeGroup(pass, 0u, state->paramsGroup, 1u, &dynamicOffset);
  GPUBindComputeGroup(pass, 1u, state->dataGroup, 0u, NULL);
  GPUDispatch(pass, ValueCount / 256u, 1u, 1u);
  GPUEndComputePass(pass);
  pass = NULL;

  toGraphics.sourceBuffer      = state->cudaBuffer;
  toGraphics.destinationBuffer = state->graphicsBuffer;
  toGraphics.sizeBytes         = sizeof(values);
  toGraphics.srcAccess         = GPU_ACCESS_SHADER_WRITE;
  toGraphics.dstAccess         = GPU_ACCESS_TRANSFER_READ;
  acquireGraphics.pBufferBarriers    = &toGraphics;
  acquireGraphics.srcStages          = GPU_STAGE_COMPUTE;
  acquireGraphics.dstStages          = GPU_STAGE_TRANSFER;
  acquireGraphics.bufferBarrierCount = 1u;
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                cudaCmdb,
                                &acquireGraphics) != GPU_OK) {
    goto cleanup;
  }

  wait.semaphore            = state->cudaSemaphore;
  wait.value                = cudaValue;
  wait.waitStages           = GPU_STAGE_COMPUTE;
  signal.semaphore          = state->cudaSemaphore;
  signal.value              = graphicsValue;
  submit.ppCommandBuffers   = &cudaCmdb;
  submit.pWaits             = &wait;
  submit.pSignals           = &signal;
  submit.fence              = NULL;
  submit.waitCount          = 1u;
  submit.signalCount        = 1u;
  if (GPUQueueSubmitEx(state->cudaQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  cudaCmdb      = NULL;
  cudaSubmitted = 1;

  if (GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-acquire",
                              &acquireCmdb) != GPU_OK ||
      !acquireCmdb ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                acquireCmdb,
                                &acquireGraphics) != GPU_OK) {
    goto cleanup;
  }

  wait.semaphore            = state->graphicsSemaphore;
  wait.value                = graphicsValue;
  wait.waitStages           = GPU_STAGE_TRANSFER;
  submit.ppCommandBuffers   = &acquireCmdb;
  submit.pWaits             = &wait;
  submit.pSignals           = NULL;
  submit.fence              = state->acquireFence;
  submit.signalCount        = 0u;
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  acquireCmdb = NULL;
  if (GPUWaitFence(state->acquireFence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(state->graphicsQueue,
                         state->graphicsBuffer,
                         0u,
                         values,
                         sizeof(values)) != GPU_OK ||
      !values_match(values)) {
    goto cleanup;
  }
  ok = 1;

cleanup:
  if (pass) {
    GPUEndComputePass(pass);
  }
  if (acquireCmdb) {
    (void)GPUDiscardCommandBuffer(acquireCmdb);
  }
  if (cudaCmdb) {
    (void)GPUDiscardCommandBuffer(cudaCmdb);
  }
  if (releaseCmdb) {
    (void)GPUDiscardCommandBuffer(releaseCmdb);
  }
  if (!ok && cudaSubmitted) {
    (void)GPUQueueReadBuffer(state->cudaQueue,
                             state->cudaBuffer,
                             0u,
                             values,
                             sizeof(values));
  }
  if (!ok && releaseSubmitted) {
    (void)GPUWaitFence(state->releaseFence, UINT64_MAX);
  }
  GPUResetFence(state->acquireFence);
  GPUResetFence(state->releaseFence);
  return ok;
}

static int
run_texture_roundtrip(RoundtripState *state) {
  GPUCommandBuffer           *releaseCmdb, *cudaCmdb, *acquireCmdb;
  GPUComputePassEncoder      *computePass;
  GPUTransferPassEncoder     *transferPass;
  GPUSharedTextureBarrierEXT  toCuda[3 + InteropFormatCount] = {0};
  GPUSharedTextureBarrierEXT  toGraphics[3 + InteropFormatCount] = {0};
  GPUSharedBarrierBatchEXT    acquireCuda = {0}, acquireGraphics = {0};
  GPUBufferTextureCopyRegion  copyRegion = {0};
  GPUTextureWriteRegion       writeRegion = {0};
  GPUQueueSemaphoreWait       wait = {0};
  GPUQueueSemaphoreSignal     signal = {0};
  GPUQueueSubmitExInfo        submit = {0};
  GPUFence                   *cudaFence;
  float                       input[TextureValueCount];
  float                       cubeInput[CubeValueCount];
  float                       cubeArrayInput[CubeArrayValueCount];
  uint16_t                    halfInput[HalfTextureValueCount];
  uint16_t                    halfOutput[HalfTextureValueCount];
  uint8_t                     unormInput[ByteTextureValueCount];
  uint8_t                     unormOutput[ByteTextureValueCount];
  uint8_t                     uintInput[ByteTextureValueCount];
  uint8_t                     uintOutput[ByteTextureValueCount];
  int8_t                      sintInput[ByteTextureValueCount];
  int8_t                      sintOutput[ByteTextureValueCount];
  InteropFormatTransfer       formatTransfers[InteropFormatCount];
  float                       output[TextureValueCount];
  float                       cudaOutput[TextureOutputValueCount];
  const char                 *failure;
  uint32_t                    halfBase, unormBase, uintBase, sintBase;
  int                         releaseSubmitted, cudaSubmitted;
  int                         acquireSubmitted, ok;
  static const uint16_t       halfInputBits[8] = {
    0x0000u, 0x3800u, 0x3c00u, 0xb800u,
    0x4000u, 0xbc00u, 0x3400u, 0xb400u
  };
  static const uint16_t       halfOutputBits[8] = {
    0x3c00u, 0x4000u, 0x4200u, 0x0000u,
    0x4500u, 0xbc00u, 0x3e00u, 0x3800u
  };
  static const float          halfInputValues[8] = {
    0.0f, 0.5f, 1.0f, -0.5f,
    2.0f, -1.0f, 0.25f, -0.25f
  };
  static const float          halfOutputValues[8] = {
    1.0f, 2.0f, 3.0f, 0.0f,
    5.0f, -1.0f, 1.5f, 0.5f
  };
  static const uint8_t        unormInputValues[8] = {
    0u, 64u, 128u, 255u, 16u, 32u, 192u, 240u
  };
  static const uint8_t        uintInputValues[8] = {
    0u, 1u, 2u, 3u, 250u, 251u, 252u, 253u
  };
  static const int8_t         sintInputValues[8] = {
    -10, -1, 0, 1, 10, 20, 30, 100
  };

  releaseCmdb       = NULL;
  cudaCmdb          = NULL;
  acquireCmdb       = NULL;
  computePass       = NULL;
  transferPass      = NULL;
  cudaFence         = NULL;
  releaseSubmitted  = 0;
  cudaSubmitted     = 0;
  acquireSubmitted  = 0;
  ok                = 0;
  failure           = "setup";
  for (uint32_t i = 0u; i < TextureOutputValueCount; i++) {
    cudaOutput[i] = 0.0f;
  }
  for (uint32_t i = 0u; i < TextureValueCount; i++) {
    input[i]  = (float)i * 0.125f;
    output[i] = 0.0f;
  }
  for (uint32_t i = 0u; i < CubeValueCount; i++) {
    uint32_t face, channel;

    face         = i / CubeFaceValueCount;
    channel      = i % 4u;
    cubeInput[i] = (float)(face * 8u + channel) + 0.25f;
  }
  for (uint32_t i = 0u; i < CubeArrayValueCount; i++) {
    uint32_t layer, channel;

    layer             = i / CubeFaceValueCount;
    channel           = i % 4u;
    cubeArrayInput[i] = (float)(layer * 8u + channel) + 0.5f;
  }
  for (uint32_t i = 0u; i < HalfTextureValueCount; i++) {
    uint32_t pattern;

    pattern       = (i / CubeFaceValueCount) * 4u + i % 4u;
    halfInput[i]  = halfInputBits[pattern];
    halfOutput[i] = 0u;
  }
  for (uint32_t i = 0u; i < ByteTextureValueCount; i++) {
    uint32_t pattern;

    pattern        = (i / CubeFaceValueCount) * 4u + i % 4u;
    unormInput[i]  = unormInputValues[pattern];
    unormOutput[i] = 0u;
    uintInput[i]   = uintInputValues[pattern];
    uintOutput[i]  = 0u;
    sintInput[i]   = sintInputValues[pattern];
    sintOutput[i]  = 0;
  }
  formatTransfers[InteropFormatHalf] = (InteropFormatTransfer){
    halfInput,
    halfOutput,
    sizeof(halfInput),
    TextureWidth * 4u * sizeof(uint16_t)
  };
  formatTransfers[InteropFormatUnorm] = (InteropFormatTransfer){
    unormInput,
    unormOutput,
    sizeof(unormInput),
    TextureWidth * 4u
  };
  formatTransfers[InteropFormatUint] = (InteropFormatTransfer){
    uintInput,
    uintOutput,
    sizeof(uintInput),
    TextureWidth * 4u
  };
  formatTransfers[InteropFormatSint] = (InteropFormatTransfer){
    sintInput,
    sintOutput,
    sizeof(sintInput),
    TextureWidth * 4u
  };

  writeRegion.aspect       = GPU_TEXTURE_ASPECT_ALL;
  writeRegion.width        = TextureWidth;
  writeRegion.height       = TextureHeight;
  writeRegion.depth        = 1u;
  writeRegion.mipLevel     = state->textureMipLevel;
  writeRegion.layerCount   = TextureLayers;
  writeRegion.bytesPerRow  = TextureWidth * 4u * sizeof(float);
  writeRegion.rowsPerImage = TextureHeight;
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsTexture,
                           &writeRegion,
                           input,
                           sizeof(input)) != GPU_OK) {
    goto cleanup;
  }
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    writeRegion.bytesPerRow = formatTransfers[i].bytesPerRow;
    if (GPUQueueWriteTexture(state->graphicsQueue,
                             state->formatTextures[i].graphicsTexture,
                             &writeRegion,
                             formatTransfers[i].input,
                             formatTransfers[i].size) != GPU_OK) {
      goto cleanup;
    }
  }
  writeRegion.bytesPerRow = TextureWidth * 4u * sizeof(float);
  writeRegion.layerCount = CubeLayers;
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsCubeTexture,
                           &writeRegion,
                           cubeInput,
                           sizeof(cubeInput)) != GPU_OK) {
    goto cleanup;
  }
  writeRegion.layerCount = CubeArrayLayers;
  if (GPUQueueWriteTexture(state->graphicsQueue,
                           state->graphicsCubeArrayTexture,
                           &writeRegion,
                           cubeArrayInput,
                           sizeof(cubeArrayInput)) != GPU_OK ||
      GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-texture-release",
                              &releaseCmdb) != GPU_OK ||
      !releaseCmdb ||
      GPUAcquireCommandBuffer(state->cudaQueue,
                              "cuda-texture-roundtrip",
                              &cudaCmdb) != GPU_OK ||
      !cudaCmdb ||
      GPUAcquireCommandBuffer(state->graphicsQueue,
                              "graphics-cuda-texture-acquire",
                              &acquireCmdb) != GPU_OK ||
      !acquireCmdb ||
      GPUCreateFence(state->cudaDevice, NULL, &cudaFence) != GPU_OK ||
      !cudaFence) {
    goto cleanup;
  }

  toCuda[0].sourceTexture      = state->graphicsTexture;
  toCuda[0].destinationTexture = state->cudaTexture;
  toCuda[0].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[0].dstAccess          = GPU_ACCESS_SHADER_READ |
                                 GPU_ACCESS_SHADER_WRITE;
  toCuda[0].baseMip            = state->textureMipLevel;
  toCuda[0].mipCount           = 1u;
  toCuda[0].layerCount         = TextureLayers;
  toCuda[1].sourceTexture      = state->graphicsCubeTexture;
  toCuda[1].destinationTexture = state->cudaCubeTexture;
  toCuda[1].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[1].dstAccess          = GPU_ACCESS_SHADER_READ;
  toCuda[1].baseMip            = state->textureMipLevel;
  toCuda[1].mipCount           = 1u;
  toCuda[1].layerCount         = CubeLayers;
  toCuda[2].sourceTexture      = state->graphicsCubeArrayTexture;
  toCuda[2].destinationTexture = state->cudaCubeArrayTexture;
  toCuda[2].srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
  toCuda[2].dstAccess          = GPU_ACCESS_SHADER_READ;
  toCuda[2].baseMip            = state->textureMipLevel;
  toCuda[2].mipCount           = 1u;
  toCuda[2].layerCount         = CubeArrayLayers;
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    GPUSharedTextureBarrierEXT *barrier;

    barrier                     = &toCuda[3u + i];
    barrier->sourceTexture      = state->formatTextures[i].graphicsTexture;
    barrier->destinationTexture = state->formatTextures[i].cudaTexture;
    barrier->srcAccess          = GPU_ACCESS_TRANSFER_WRITE;
    barrier->dstAccess          = GPU_ACCESS_SHADER_READ |
                                  GPU_ACCESS_SHADER_WRITE;
    barrier->baseMip            = state->textureMipLevel;
    barrier->mipCount           = 1u;
    barrier->layerCount         = TextureLayers;
  }
  acquireCuda.pTextureBarriers    = toCuda;
  acquireCuda.srcStages           = GPU_STAGE_TRANSFER;
  acquireCuda.dstStages           = GPU_STAGE_COMPUTE;
  acquireCuda.textureBarrierCount = 3u + InteropFormatCount;
  failure = "release/acquire encoding";
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                releaseCmdb,
                                &acquireCuda) != GPU_OK ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                cudaCmdb,
                                &acquireCuda) != GPU_OK ||
      !(computePass = GPUBeginComputePass(cudaCmdb,
                                          "cuda-texture-update"))) {
    goto cleanup;
  }
  GPUBindComputePipeline(computePass, state->texturePipeline);
  GPUBindComputeGroup(computePass, 0u, state->textureGroup, 0u, NULL);
  GPUDispatch(computePass,
              TextureWidth / 8u,
              TextureHeight / 8u,
              CubeArrayLayers);
  GPUEndComputePass(computePass);
  computePass = NULL;

  toGraphics[0].sourceTexture      = state->cudaTexture;
  toGraphics[0].destinationTexture = state->graphicsTexture;
  toGraphics[0].srcAccess          = GPU_ACCESS_SHADER_WRITE;
  toGraphics[0].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[0].baseMip            = state->textureMipLevel;
  toGraphics[0].mipCount           = 1u;
  toGraphics[0].layerCount         = TextureLayers;
  toGraphics[1].sourceTexture      = state->cudaCubeTexture;
  toGraphics[1].destinationTexture = state->graphicsCubeTexture;
  toGraphics[1].srcAccess          = GPU_ACCESS_SHADER_READ;
  toGraphics[1].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[1].baseMip            = state->textureMipLevel;
  toGraphics[1].mipCount           = 1u;
  toGraphics[1].layerCount         = CubeLayers;
  toGraphics[2].sourceTexture      = state->cudaCubeArrayTexture;
  toGraphics[2].destinationTexture = state->graphicsCubeArrayTexture;
  toGraphics[2].srcAccess          = GPU_ACCESS_SHADER_READ;
  toGraphics[2].dstAccess          = GPU_ACCESS_TRANSFER_READ;
  toGraphics[2].baseMip            = state->textureMipLevel;
  toGraphics[2].mipCount           = 1u;
  toGraphics[2].layerCount         = CubeArrayLayers;
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    GPUSharedTextureBarrierEXT *barrier;

    barrier                     = &toGraphics[3u + i];
    barrier->sourceTexture      = state->formatTextures[i].cudaTexture;
    barrier->destinationTexture = state->formatTextures[i].graphicsTexture;
    barrier->srcAccess          = GPU_ACCESS_SHADER_WRITE;
    barrier->dstAccess          = GPU_ACCESS_TRANSFER_READ;
    barrier->baseMip            = state->textureMipLevel;
    barrier->mipCount           = 1u;
    barrier->layerCount         = TextureLayers;
  }
  acquireGraphics.pTextureBarriers    = toGraphics;
  acquireGraphics.srcStages           = GPU_STAGE_COMPUTE;
  acquireGraphics.dstStages           = GPU_STAGE_TRANSFER;
  acquireGraphics.textureBarrierCount = 3u + InteropFormatCount;
  failure = "return/copy encoding";
  if (GPUEncodeSharedReleaseEXT(state->interop,
                                cudaCmdb,
                                &acquireGraphics) != GPU_OK ||
      GPUEncodeSharedAcquireEXT(state->interop,
                                acquireCmdb,
                                &acquireGraphics) != GPU_OK ||
      !(transferPass = GPUBeginTransferPass(
          acquireCmdb,
          "graphics-cuda-texture-readback"
        ))) {
    goto cleanup;
  }
  copyRegion.bytesPerRow        = writeRegion.bytesPerRow;
  copyRegion.rowsPerImage       = TextureHeight;
  copyRegion.texture.texture.mipLevel = state->textureMipLevel;
  copyRegion.texture.width      = TextureWidth;
  copyRegion.texture.height     = TextureHeight;
  copyRegion.texture.depth      = 1u;
  copyRegion.texture.layerCount = TextureLayers;
  GPUCopyTextureToBuffer(transferPass,
                         state->graphicsTexture,
                         state->textureReadback,
                         &copyRegion);
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    copyRegion.bytesPerRow = formatTransfers[i].bytesPerRow;
    GPUCopyTextureToBuffer(transferPass,
                           state->formatTextures[i].graphicsTexture,
                           state->formatTextures[i].readback,
                           &copyRegion);
  }
  GPUEndTransferPass(transferPass);
  transferPass = NULL;

  signal.semaphore          = state->graphicsSemaphore;
  signal.value              = RoundtripCount * 2u + 1u;
  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.ppCommandBuffers   = &releaseCmdb;
  submit.pSignals           = &signal;
  submit.fence              = state->releaseFence;
  submit.commandBufferCount = 1u;
  submit.signalCount        = 1u;
  failure = "graphics release submit";
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  releaseCmdb      = NULL;
  releaseSubmitted = 1;

  wait.semaphore          = state->cudaSemaphore;
  wait.value              = signal.value;
  wait.waitStages         = GPU_STAGE_COMPUTE;
  signal.semaphore        = state->cudaSemaphore;
  signal.value++;
  submit.ppCommandBuffers = &cudaCmdb;
  submit.pWaits           = &wait;
  submit.pSignals         = &signal;
  submit.fence            = cudaFence;
  submit.waitCount        = 1u;
  failure = "CUDA submit";
  if (GPUQueueSubmitEx(state->cudaQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  cudaCmdb      = NULL;
  cudaSubmitted = 1;

  wait.semaphore          = state->graphicsSemaphore;
  wait.value              = signal.value;
  wait.waitStages         = GPU_STAGE_TRANSFER;
  submit.ppCommandBuffers = &acquireCmdb;
  submit.pWaits           = &wait;
  submit.pSignals         = NULL;
  submit.fence            = state->acquireFence;
  submit.signalCount      = 0u;
  failure = "graphics acquire submit";
  if (GPUQueueSubmitEx(state->graphicsQueue, &submit) != GPU_OK) {
    goto cleanup;
  }
  acquireCmdb      = NULL;
  acquireSubmitted = 1;
  failure = "graphics readback";
  if (GPUWaitFence(state->acquireFence, UINT64_MAX) != GPU_OK ||
      GPUQueueReadBuffer(state->graphicsQueue,
                         state->textureReadback,
                         0u,
                         output,
                         sizeof(output)) != GPU_OK ||
      GPUQueueReadBuffer(state->cudaQueue,
                         state->textureCudaReadback,
                         0u,
                         cudaOutput,
                         sizeof(cudaOutput)) != GPU_OK) {
    goto cleanup;
  }
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    if (GPUQueueReadBuffer(state->graphicsQueue,
                           state->formatTextures[i].readback,
                           0u,
                           formatTransfers[i].output,
                           formatTransfers[i].size) != GPU_OK) {
      goto cleanup;
    }
  }
  failure = "result validation";
  for (uint32_t i = 0u; i < TextureValueCount; i++) {
    float expected;

    expected = input[i] * 2.0f + 1.0f;
    if (fabsf(cudaOutput[i] - input[i]) > 0.0001f) {
      fprintf(stderr,
              "CUDA sampled texture mismatch at %u: %.9g != %.9g\n",
              i,
              cudaOutput[i],
              input[i]);
      goto cleanup;
    }
    if (fabsf(cudaOutput[TextureValueCount + i] - expected) > 0.0001f) {
      fprintf(stderr,
              "CUDA storage texture mismatch at %u: %.9g != %.9g\n",
              i,
              cudaOutput[TextureValueCount + i],
              expected);
      goto cleanup;
    }
    if (fabsf(output[i] - expected) > 0.0001f) {
      fprintf(stderr,
              "graphics/CUDA texture mismatch at %u: %.9g != %.9g\n",
              i,
              output[i],
              expected);
      goto cleanup;
    }
  }
  for (uint32_t face = 0u; face < CubeLayers; face++) {
    for (uint32_t channel = 0u; channel < 4u; channel++) {
      uint32_t index, inputIndex;
      float    expected;

      index      = TextureValueCount * 2u + face * 4u + channel;
      inputIndex = face * CubeFaceValueCount + channel;
      expected   = cubeInput[inputIndex];
      if (fabsf(cudaOutput[index] - expected) > 0.0001f) {
        fprintf(stderr,
                "CUDA cube texture mismatch at %u/%u: %.9g != %.9g\n",
                face,
                channel,
                cudaOutput[index],
                expected);
        goto cleanup;
      }
    }
  }
  for (uint32_t layer = 0u; layer < CubeArrayLayers; layer++) {
    for (uint32_t channel = 0u; channel < 4u; channel++) {
      uint32_t index, inputIndex;
      float    expected;

      index = TextureValueCount * 2u + CubeOutputValueCount +
              layer * 4u + channel;
      inputIndex = layer * CubeFaceValueCount + channel;
      expected   = cubeArrayInput[inputIndex];
      if (fabsf(cudaOutput[index] - expected) > 0.0001f) {
        fprintf(stderr,
                "CUDA cube-array mismatch at %u/%u: %.9g != %.9g\n",
                layer,
                channel,
                cudaOutput[index],
                expected);
        goto cleanup;
      }
    }
  }
  halfBase = TextureValueCount * 2u + CubeOutputValueCount +
             CubeArrayOutputValueCount;
  unormBase = halfBase + HalfFloatOutputValueCount;
  uintBase  = unormBase + ByteFloatOutputValueCount;
  sintBase  = uintBase + ByteFloatOutputValueCount;
  for (uint32_t i = 0u; i < HalfTextureValueCount; i++) {
    uint32_t pattern;

    pattern = (i / CubeFaceValueCount) * 4u + i % 4u;
    if (fabsf(cudaOutput[halfBase + i] - halfInputValues[pattern]) >
          0.0001f ||
        fabsf(cudaOutput[halfBase + HalfTextureValueCount + i] -
               halfOutputValues[pattern]) > 0.0001f ||
        halfOutput[i] != halfOutputBits[pattern]) {
      fprintf(stderr,
              "CUDA half texture mismatch at %u: %.9g/%.9g/%04x\n",
              i,
              cudaOutput[halfBase + i],
              cudaOutput[halfBase + HalfTextureValueCount + i],
              (unsigned)halfOutput[i]);
      goto cleanup;
    }
  }
  for (uint32_t i = 0u; i < ByteTextureValueCount; i++) {
    uint32_t pattern;
    float    unormOriginal, unormExpected;

    pattern       = (i / CubeFaceValueCount) * 4u + i % 4u;
    unormOriginal = (float)unormInputValues[pattern] / 255.0f;
    unormExpected = (float)(255u - unormInputValues[pattern]) / 255.0f;
    if (fabsf(cudaOutput[unormBase + i] - unormOriginal) >
          0.5f / 255.0f + 0.000001f ||
        fabsf(cudaOutput[unormBase + ByteTextureValueCount + i] -
               unormExpected) > 0.5f / 255.0f + 0.000001f ||
        unormOutput[i] != 255u - unormInputValues[pattern] ||
        cudaOutput[uintBase + i] != (float)uintInputValues[pattern] ||
        cudaOutput[uintBase + ByteTextureValueCount + i] !=
          (float)(uintInputValues[pattern] + 1u) ||
        uintOutput[i] != (uint8_t)(uintInputValues[pattern] + 1u) ||
        cudaOutput[sintBase + i] != (float)sintInputValues[pattern] ||
        cudaOutput[sintBase + ByteTextureValueCount + i] !=
          (float)(sintInputValues[pattern] + 7) ||
        sintOutput[i] != (int8_t)(sintInputValues[pattern] + 7)) {
      fprintf(stderr,
              "CUDA byte texture mismatch at %u: %.9g/%.9g/%u/%d\n",
              i,
              cudaOutput[unormBase + i],
              cudaOutput[unormBase + ByteTextureValueCount + i],
              (unsigned)uintOutput[i],
              (int)sintOutput[i]);
      goto cleanup;
    }
  }
  ok = 1;

cleanup:
  if (transferPass) {
    GPUEndTransferPass(transferPass);
  }
  if (computePass) {
    GPUEndComputePass(computePass);
  }
  if (acquireCmdb) {
    (void)GPUDiscardCommandBuffer(acquireCmdb);
  }
  if (cudaCmdb) {
    (void)GPUDiscardCommandBuffer(cudaCmdb);
  }
  if (releaseCmdb) {
    (void)GPUDiscardCommandBuffer(releaseCmdb);
  }
  if (!acquireSubmitted && cudaSubmitted) {
    (void)GPUWaitFence(cudaFence, UINT64_MAX);
  }
  if (!cudaSubmitted && releaseSubmitted) {
    (void)GPUWaitFence(state->releaseFence, UINT64_MAX);
  }
  GPUDestroyFence(cudaFence);
  GPUResetFence(state->acquireFence);
  GPUResetFence(state->releaseFence);
  if (!ok) {
    fprintf(stderr, "graphics/CUDA texture stage failed: %s\n", failure);
  }
  return ok;
}

int
main(int argc, char **argv) {
  GPUInstance                 *graphicsInstance, *cudaInstance;
  GPUAdapter                  *graphicsAdapter, *cudaAdapter;
  GPUShaderLibrary            *library;
  GPUShaderLibrary            *textureLibrary;
  GPUShaderLayout             *shaderLayout;
  GPUShaderLayout             *textureLayout;
  const char                  *graphicsBackend;
  void                        *artifact;
  void                        *textureArtifact;
  GPUInstanceCreateInfo        graphicsInstanceInfo = {0};
  GPUInstanceCreateInfo        cudaInstanceInfo = {0};
  GPUBufferCreateInfo          graphicsBufferInfo = {0};
  GPUBufferCreateInfo          cudaBufferInfo = {0};
  GPUBufferCreateInfo          paramsBufferInfo = {0};
  GPUBufferCreateInfo          textureReadbackInfo = {0};
  GPUBufferCreateInfo          textureCudaReadbackInfo = {0};
  GPUTextureCreateInfo         graphicsTextureInfo = {0};
  GPUTextureCreateInfo         cudaTextureInfo = {0};
  GPUTextureCreateInfo         graphicsCubeTextureInfo = {0};
  GPUTextureCreateInfo         cudaCubeTextureInfo = {0};
  GPUTextureCreateInfo         graphicsCubeArrayTextureInfo = {0};
  GPUTextureCreateInfo         cudaCubeArrayTextureInfo = {0};
  GPUTextureCreateInfo         layeredMipGraphicsInfo = {0};
  GPUTextureCreateInfo         layeredMipCudaInfo = {0};
  GPUTextureViewCreateInfo     textureViewInfo = {0};
  GPUSemaphoreCreateInfo       semaphoreInfo = {0};
  GPUComputePipelineCreateInfo pipelineInfo = {0};
  GPUBindGroupEntry            paramsEntry = {0};
  GPUBindGroupEntry            dataEntries[2] = {0};
  GPUBindGroupEntry            textureEntries[13] = {0};
  GPUBindGroupCreateInfo       groupInfo = {0};
  GPUMemoryRequirements        memoryRequirements;
  GPUResult                    textureRequirementsResult;
  GPUResult                    textureCreateResult;
  GPUResult                    cubeRequirementsResult;
  GPUResult                    cubeCreateResult;
  GPUResult                    cubeArrayRequirementsResult;
  GPUResult                    cubeArrayCreateResult;
  AdapterList                  graphicsAdapters = {0}, cudaAdapters = {0};
  RoundtripState               state = {0};
  uint64_t                     artifactSize;
  uint64_t                     textureArtifactSize;
  Params                       params = {2.0f, 1.0f};
  bool                         cudaFirst;
  bool                         vulkanGraphics;
  int                          status;

  if (argc != 3) {
    fprintf(stderr,
            "usage: gpu-graphics-cuda-interop-usl buffer.us texture.us\n");
    return 1;
  }

  graphicsInstance = NULL;
  cudaInstance     = NULL;
  graphicsAdapter  = NULL;
  cudaAdapter      = NULL;
  library          = NULL;
  textureLibrary   = NULL;
  shaderLayout     = NULL;
  textureLayout    = NULL;
  graphicsBackend  = getenv("GPU_GRAPHICS_BACKEND");
  cudaFirst        = getenv("GPU_CUDA_FIRST") != NULL;
  artifactSize        = 0u;
  textureArtifactSize = 0u;
  artifact            = read_file(argv[1], &artifactSize);
  textureArtifact     = read_file(argv[2], &textureArtifactSize);
  status              = 1;
  if (!artifact || !textureArtifact) {
    fprintf(stderr, "USL artifact read failed\n");
    goto cleanup;
  }

  graphicsInstanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  graphicsInstanceInfo.chain.structSize = sizeof(graphicsInstanceInfo);
#if defined(_WIN32) || defined(WIN32)
  if (!graphicsBackend || strcmp(graphicsBackend, "dx12") == 0) {
    graphicsInstanceInfo.preferredBackend = GPU_BACKEND_DX12;
    vulkanGraphics = false;
  } else if (strcmp(graphicsBackend, "vulkan") == 0) {
    graphicsInstanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
    vulkanGraphics = true;
  } else {
    fprintf(stderr, "unsupported graphics backend: %s\n", graphicsBackend);
    goto cleanup;
  }
#else
  (void)graphicsBackend;
  graphicsInstanceInfo.preferredBackend = GPU_BACKEND_VULKAN;
  vulkanGraphics = true;
#endif
  graphicsInstanceInfo.enableValidation = true;
  cudaInstanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  cudaInstanceInfo.chain.structSize = sizeof(cudaInstanceInfo);
  cudaInstanceInfo.preferredBackend = GPU_BACKEND_CUDA;
  cudaInstanceInfo.enableValidation = true;
  if (GPUCreateInstance(&graphicsInstanceInfo, &graphicsInstance) != GPU_OK ||
      !graphicsInstance ||
      GPUCreateInstance(&cudaInstanceInfo, &cudaInstance) != GPU_OK ||
      !cudaInstance ||
      enumerate_adapters(graphicsInstance, &graphicsAdapters) != GPU_OK ||
      enumerate_adapters(cudaInstance, &cudaAdapters) != GPU_OK ||
      !find_matching_adapters(&graphicsAdapters,
                              &cudaAdapters,
                              &graphicsAdapter,
                              &cudaAdapter)) {
    puts("matching graphics/CUDA adapters unavailable");
    status = 77;
    goto cleanup;
  }

  state.graphicsDevice = GPUCreateDeviceWithDefaultQueues(graphicsAdapter);
  state.cudaDevice     = GPUCreateDeviceWithDefaultQueues(cudaAdapter);
  state.graphicsQueue  = GPUGetQueue(state.graphicsDevice,
                                     GPU_QUEUE_GRAPHICS,
                                     0u);
  state.cudaQueue      = GPUGetQueue(state.cudaDevice, GPU_QUEUE_COMPUTE, 0u);
  if (!state.graphicsDevice || !state.cudaDevice ||
      !state.graphicsQueue || !state.cudaQueue ||
      GPUCreateDeviceInteropEXT(cudaFirst
                                  ? state.cudaDevice
                                  : state.graphicsDevice,
                                cudaFirst
                                  ? state.graphicsDevice
                                  : state.cudaDevice,
                                &state.interop) != GPU_OK ||
      !state.interop) {
    fprintf(stderr, "graphics/CUDA device interop creation failed\n");
    goto cleanup;
  }
  if (GPUSetDeviceErrorCallback(state.cudaDevice,
                                device_error,
                                NULL) != GPU_OK) {
    fprintf(stderr, "CUDA interop error callback setup failed\n");
    goto cleanup;
  }

  graphicsBufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  graphicsBufferInfo.chain.structSize = sizeof(graphicsBufferInfo);
  graphicsBufferInfo.label       = "graphics-cuda-buffer";
  graphicsBufferInfo.sizeBytes   = sizeof(float) * ValueCount;
  graphicsBufferInfo.usage       = GPU_BUFFER_USAGE_COPY_SRC |
                                   GPU_BUFFER_USAGE_COPY_DST;
  cudaBufferInfo                 = graphicsBufferInfo;
  cudaBufferInfo.label           = "cuda-graphics-buffer";
  cudaBufferInfo.usage           = GPU_BUFFER_USAGE_STORAGE |
                                   GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUGetSharedBufferMemoryRequirementsEXT(state.interop,
                                                cudaFirst
                                                  ? &cudaBufferInfo
                                                  : &graphicsBufferInfo,
                                                cudaFirst
                                                  ? &graphicsBufferInfo
                                                  : &cudaBufferInfo,
                                                &memoryRequirements) != GPU_OK ||
      memoryRequirements.sizeBytes < graphicsBufferInfo.sizeBytes ||
      GPUCreateSharedBufferEXT(state.interop,
                               cudaFirst
                                 ? &cudaBufferInfo
                                 : &graphicsBufferInfo,
                               cudaFirst
                                 ? &graphicsBufferInfo
                                 : &cudaBufferInfo,
                               cudaFirst
                                 ? &state.cudaBuffer
                                 : &state.graphicsBuffer,
                               cudaFirst
                                 ? &state.graphicsBuffer
                                 : &state.cudaBuffer) != GPU_OK ||
      !state.graphicsBuffer || !state.cudaBuffer) {
    fprintf(stderr, "shared graphics/CUDA buffer creation failed\n");
    goto cleanup;
  }

  semaphoreInfo.chain.sType      = GPU_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphoreInfo.chain.structSize = sizeof(semaphoreInfo);
  semaphoreInfo.label            = "graphics-cuda-timeline";
  if (GPUCreateSharedSemaphoreEXT(state.interop,
                                  &semaphoreInfo,
                                  cudaFirst
                                    ? &state.cudaSemaphore
                                    : &state.graphicsSemaphore,
                                  cudaFirst
                                    ? &state.graphicsSemaphore
                                    : &state.cudaSemaphore) != GPU_OK ||
      !state.graphicsSemaphore || !state.cudaSemaphore ||
      GPUCreateFence(state.graphicsDevice, NULL, &state.releaseFence) !=
        GPU_OK ||
      GPUCreateFence(state.graphicsDevice, NULL, &state.acquireFence) !=
        GPU_OK ||
      !state.releaseFence || !state.acquireFence) {
    fprintf(stderr, "shared graphics/CUDA synchronization creation failed\n");
    goto cleanup;
  }

  graphicsTextureInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  graphicsTextureInfo.chain.structSize = sizeof(graphicsTextureInfo);
  graphicsTextureInfo.label            = "graphics-cuda-texture";
  graphicsTextureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  graphicsTextureInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  graphicsTextureInfo.width            = vulkanGraphics
                                           ? TextureWidth
                                           : TextureBaseWidth;
  graphicsTextureInfo.height           = vulkanGraphics
                                           ? TextureHeight
                                           : TextureBaseHeight;
  graphicsTextureInfo.depthOrLayers    = TextureLayers;
  graphicsTextureInfo.mipLevelCount    = vulkanGraphics
                                           ? 1u
                                           : TextureMipCount;
  graphicsTextureInfo.sampleCount      = 1u;
  graphicsTextureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                         GPU_TEXTURE_USAGE_COPY_SRC |
                                         GPU_TEXTURE_USAGE_COPY_DST;
  cudaTextureInfo                      = graphicsTextureInfo;
  cudaTextureInfo.label                = "cuda-graphics-texture";
  cudaTextureInfo.usage                = GPU_TEXTURE_USAGE_SAMPLED |
                                         GPU_TEXTURE_USAGE_STORAGE;
#if defined(_WIN32) || defined(WIN32)
  if (vulkanGraphics) {
    layeredMipGraphicsInfo               = graphicsTextureInfo;
    layeredMipGraphicsInfo.width         = TextureBaseWidth;
    layeredMipGraphicsInfo.height        = TextureBaseHeight;
    layeredMipGraphicsInfo.mipLevelCount = TextureMipCount;
    layeredMipCudaInfo                   = cudaTextureInfo;
    layeredMipCudaInfo.width             = TextureBaseWidth;
    layeredMipCudaInfo.height            = TextureBaseHeight;
    layeredMipCudaInfo.mipLevelCount     = TextureMipCount;
    if (GPUGetSharedTextureMemoryRequirementsEXT(
          state.interop,
          cudaFirst ? &layeredMipCudaInfo : &layeredMipGraphicsInfo,
          cudaFirst ? &layeredMipGraphicsInfo : &layeredMipCudaInfo,
          &memoryRequirements
        ) != GPU_ERROR_UNSUPPORTED) {
      fprintf(stderr, "layered Vulkan/CUDA mip guard failed\n");
      goto cleanup;
    }
  }
#else
  (void)layeredMipGraphicsInfo;
  (void)layeredMipCudaInfo;
#endif
  state.textureMipLevel = vulkanGraphics ? 0u : TextureMipLevel;
  textureRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaTextureInfo : &graphicsTextureInfo,
    cudaFirst ? &graphicsTextureInfo : &cudaTextureInfo,
    &memoryRequirements
  );
  textureCreateResult = textureRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(state.interop,
                                cudaFirst
                                  ? &cudaTextureInfo
                                  : &graphicsTextureInfo,
                                cudaFirst
                                  ? &graphicsTextureInfo
                                  : &cudaTextureInfo,
                                cudaFirst
                                  ? &state.cudaTexture
                                  : &state.graphicsTexture,
                                cudaFirst
                                  ? &state.graphicsTexture
                                  : &state.cudaTexture)
    : textureRequirementsResult;
  if (textureRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      textureCreateResult != GPU_OK ||
      !state.graphicsTexture || !state.cudaTexture) {
    fprintf(stderr,
            "shared graphics/CUDA texture creation failed (%d, %d)\n",
            textureRequirementsResult,
            textureCreateResult);
    goto cleanup;
  }

  graphicsCubeTextureInfo               = graphicsTextureInfo;
  graphicsCubeTextureInfo.label         = "graphics-cuda-cube-texture";
  graphicsCubeTextureInfo.depthOrLayers = CubeLayers;
  cudaCubeTextureInfo                   = graphicsCubeTextureInfo;
  cudaCubeTextureInfo.label             = "cuda-graphics-cube-texture";
  cudaCubeTextureInfo.usage             = GPU_TEXTURE_USAGE_SAMPLED;
  cubeRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaCubeTextureInfo : &graphicsCubeTextureInfo,
    cudaFirst ? &graphicsCubeTextureInfo : &cudaCubeTextureInfo,
    &memoryRequirements
  );
  cubeCreateResult = cubeRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(state.interop,
                                cudaFirst
                                  ? &cudaCubeTextureInfo
                                  : &graphicsCubeTextureInfo,
                                cudaFirst
                                  ? &graphicsCubeTextureInfo
                                  : &cudaCubeTextureInfo,
                                cudaFirst
                                  ? &state.cudaCubeTexture
                                  : &state.graphicsCubeTexture,
                                cudaFirst
                                  ? &state.graphicsCubeTexture
                                  : &state.cudaCubeTexture)
    : cubeRequirementsResult;
  if (cubeRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      cubeCreateResult != GPU_OK ||
      !state.graphicsCubeTexture || !state.cudaCubeTexture) {
    fprintf(stderr,
            "shared graphics/CUDA cube texture creation failed (%d, %d)\n",
            cubeRequirementsResult,
            cubeCreateResult);
    goto cleanup;
  }

  graphicsCubeArrayTextureInfo               = graphicsTextureInfo;
  graphicsCubeArrayTextureInfo.label         =
    "graphics-cuda-cube-array-texture";
  graphicsCubeArrayTextureInfo.depthOrLayers = CubeArrayLayers;
  cudaCubeArrayTextureInfo                   = graphicsCubeArrayTextureInfo;
  cudaCubeArrayTextureInfo.label             =
    "cuda-graphics-cube-array-texture";
  cudaCubeArrayTextureInfo.usage             = GPU_TEXTURE_USAGE_SAMPLED;
  cubeArrayRequirementsResult = GPUGetSharedTextureMemoryRequirementsEXT(
    state.interop,
    cudaFirst ? &cudaCubeArrayTextureInfo : &graphicsCubeArrayTextureInfo,
    cudaFirst ? &graphicsCubeArrayTextureInfo : &cudaCubeArrayTextureInfo,
    &memoryRequirements
  );
  cubeArrayCreateResult = cubeArrayRequirementsResult == GPU_OK
    ? GPUCreateSharedTextureEXT(
        state.interop,
        cudaFirst ? &cudaCubeArrayTextureInfo : &graphicsCubeArrayTextureInfo,
        cudaFirst ? &graphicsCubeArrayTextureInfo : &cudaCubeArrayTextureInfo,
        cudaFirst
          ? &state.cudaCubeArrayTexture
          : &state.graphicsCubeArrayTexture,
        cudaFirst
          ? &state.graphicsCubeArrayTexture
          : &state.cudaCubeArrayTexture
      )
    : cubeArrayRequirementsResult;
  if (cubeArrayRequirementsResult != GPU_OK ||
      memoryRequirements.sizeBytes == 0u ||
      cubeArrayCreateResult != GPU_OK ||
      !state.graphicsCubeArrayTexture || !state.cudaCubeArrayTexture) {
    fprintf(stderr,
            "shared graphics/CUDA cube-array creation failed (%d, %d)\n",
            cubeArrayRequirementsResult,
            cubeArrayCreateResult);
    goto cleanup;
  }

  if (!create_interop_format_texture(&state,
                                     InteropFormatHalf,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA16_FLOAT,
                                     "rgba16f-interop",
                                     sizeof(uint16_t) * HalfTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(&state,
                                     InteropFormatUnorm,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA8_UNORM,
                                     "rgba8-unorm-interop",
                                     ByteTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(&state,
                                     InteropFormatUint,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA8_UINT,
                                     "rgba8-uint-interop",
                                     ByteTextureValueCount,
                                     cudaFirst) ||
      !create_interop_format_texture(&state,
                                     InteropFormatSint,
                                     &graphicsTextureInfo,
                                     GPU_FORMAT_RGBA8_SINT,
                                     "rgba8-sint-interop",
                                     ByteTextureValueCount,
                                     cudaFirst)) {
    goto cleanup;
  }

  textureViewInfo.chain.sType      =
    GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  textureViewInfo.chain.structSize = sizeof(textureViewInfo);
  textureViewInfo.label            = "cuda-graphics-texture-view";
  textureViewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  textureViewInfo.format           = GPU_FORMAT_RGBA32_FLOAT;
  textureViewInfo.baseMipLevel     = state.textureMipLevel;
  textureViewInfo.mipLevelCount    = 1u;
  textureViewInfo.arrayLayerCount  = TextureLayers;
  textureReadbackInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  textureReadbackInfo.chain.structSize = sizeof(textureReadbackInfo);
  textureReadbackInfo.label            = "graphics-cuda-texture-readback";
  textureReadbackInfo.sizeBytes        =
    TextureValueCount * sizeof(float);
  textureReadbackInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                         GPU_BUFFER_USAGE_COPY_SRC;
  textureCudaReadbackInfo              = textureReadbackInfo;
  textureCudaReadbackInfo.label        = "cuda-texture-readback";
  textureCudaReadbackInfo.sizeBytes    =
    TextureOutputValueCount * sizeof(float);
  textureCudaReadbackInfo.usage        = GPU_BUFFER_USAGE_STORAGE |
                                         GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateTextureView(state.cudaTexture,
                           &textureViewInfo,
                           &state.cudaTextureView) != GPU_OK ||
      !state.cudaTextureView) {
    fprintf(stderr, "shared graphics/CUDA storage view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label = "cuda-graphics-sampled-texture-view";
  textureViewInfo.mipLevelCount =
    graphicsTextureInfo.mipLevelCount - state.textureMipLevel;
  if (GPUCreateTextureView(state.cudaTexture,
                           &textureViewInfo,
                           &state.cudaSampledTextureView) != GPU_OK ||
      !state.cudaSampledTextureView) {
    fprintf(stderr, "shared graphics/CUDA sampled view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label           = "cuda-graphics-cube-texture-view";
  textureViewInfo.viewType        = GPU_TEXTURE_VIEW_CUBE;
  textureViewInfo.mipLevelCount   =
    graphicsCubeTextureInfo.mipLevelCount - state.textureMipLevel;
  textureViewInfo.arrayLayerCount = CubeLayers;
  if (GPUCreateTextureView(state.cudaCubeTexture,
                           &textureViewInfo,
                           &state.cudaCubeTextureView) != GPU_OK ||
      !state.cudaCubeTextureView) {
    fprintf(stderr, "shared graphics/CUDA cube view setup failed\n");
    goto cleanup;
  }
  textureViewInfo.label           = "cuda-graphics-cube-array-view";
  textureViewInfo.viewType        = GPU_TEXTURE_VIEW_CUBE_ARRAY;
  textureViewInfo.mipLevelCount   =
    graphicsCubeArrayTextureInfo.mipLevelCount - state.textureMipLevel;
  textureViewInfo.arrayLayerCount = CubeArrayLayers;
  if (GPUCreateTextureView(state.cudaCubeArrayTexture,
                           &textureViewInfo,
                           &state.cudaCubeArrayTextureView) != GPU_OK ||
      !state.cudaCubeArrayTextureView) {
    fprintf(stderr, "shared graphics/CUDA cube-array view setup failed\n");
    goto cleanup;
  }
  if (GPUCreateBuffer(state.graphicsDevice,
                      &textureReadbackInfo,
                      &state.textureReadback) != GPU_OK ||
      !state.textureReadback ||
      GPUCreateBuffer(state.cudaDevice,
                      &textureCudaReadbackInfo,
                      &state.textureCudaReadback) != GPU_OK ||
      !state.textureCudaReadback) {
    fprintf(stderr, "shared graphics/CUDA output setup failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(state.cudaDevice,
                                    artifact,
                                    artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUCreateShaderLayout(state.cudaDevice,
                            library,
                            &shaderLayout) != GPU_OK ||
      !shaderLayout || shaderLayout->bindGroupLayoutCount != 2u ||
      !shaderLayout->bindGroupLayouts ||
      !shaderLayout->bindGroupLayouts[0] ||
      !shaderLayout->bindGroupLayouts[1] ||
      !shaderLayout->pipelineLayout) {
    fprintf(stderr, "CUDA USL shader layout creation failed\n");
    goto cleanup;
  }

  if (GPUCreateShaderLibraryFromUSL(state.cudaDevice,
                                    textureArtifact,
                                    textureArtifactSize,
                                    &textureLibrary) != GPU_OK ||
      !textureLibrary ||
      GPUCreateShaderLayout(state.cudaDevice,
                            textureLibrary,
                            &textureLayout) != GPU_OK ||
      !textureLayout || textureLayout->bindGroupLayoutCount != 1u ||
      !textureLayout->bindGroupLayouts ||
      !textureLayout->bindGroupLayouts[0] ||
      !textureLayout->pipelineLayout) {
    fprintf(stderr, "CUDA interop texture shader layout creation failed\n");
    goto cleanup;
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "graphics-cuda-saxpy";
  pipelineInfo.layout           = shaderLayout->pipelineLayout;
  pipelineInfo.library          = library;
  pipelineInfo.entryPoint       = "saxpy";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.pipeline) != GPU_OK ||
      !state.pipeline) {
    fprintf(stderr, "CUDA interop compute pipeline creation failed\n");
    goto cleanup;
  }

  pipelineInfo.label      = "graphics-cuda-texture-update";
  pipelineInfo.layout     = textureLayout->pipelineLayout;
  pipelineInfo.library    = textureLibrary;
  pipelineInfo.entryPoint = "interop_texture_cs";
  if (GPUCreateComputePipeline(state.cudaDevice,
                               &pipelineInfo,
                               &state.texturePipeline) != GPU_OK ||
      !state.texturePipeline) {
    fprintf(stderr, "CUDA interop texture pipeline creation failed\n");
    goto cleanup;
  }

  paramsBufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  paramsBufferInfo.chain.structSize = sizeof(paramsBufferInfo);
  paramsBufferInfo.label            = "graphics-cuda-params";
  paramsBufferInfo.sizeBytes        = sizeof(params);
  paramsBufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                      GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state.cudaDevice,
                      &paramsBufferInfo,
                      &state.paramsBuffer) != GPU_OK ||
      !state.paramsBuffer ||
      GPUQueueWriteBuffer(state.cudaQueue,
                          state.paramsBuffer,
                          0u,
                          &params,
                          sizeof(params)) != GPU_OK) {
    fprintf(stderr, "CUDA interop params buffer creation failed\n");
    goto cleanup;
  }

  paramsEntry.binding       = 0u;
  paramsEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  paramsEntry.buffer.buffer = state.paramsBuffer;
  paramsEntry.buffer.size   = sizeof(params);
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "graphics-cuda-params-group";
  groupInfo.layout           = shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries         = &paramsEntry;
  groupInfo.entryCount       = 1u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.paramsGroup) != GPU_OK ||
      !state.paramsGroup) {
    fprintf(stderr, "CUDA interop params bind group creation failed\n");
    goto cleanup;
  }

  textureEntries[0].binding     = 0u;
  textureEntries[0].bindingType = GPU_BINDING_STORAGE_TEXTURE;
  textureEntries[0].textureView = state.cudaTextureView;
  textureEntries[1].binding       = 1u;
  textureEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  textureEntries[1].buffer.buffer = state.textureCudaReadback;
  textureEntries[1].buffer.size   = textureCudaReadbackInfo.sizeBytes;
  textureEntries[2].binding       = 2u;
  textureEntries[2].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[2].textureView   = state.cudaSampledTextureView;
  textureEntries[3].binding       = 3u;
  textureEntries[3].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[3].textureView   = state.cudaCubeTextureView;
  textureEntries[4].binding       = 4u;
  textureEntries[4].bindingType   = GPU_BINDING_SAMPLED_TEXTURE;
  textureEntries[4].textureView   = state.cudaCubeArrayTextureView;
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    uint32_t storageBinding, sampledBinding;

    storageBinding = 5u + i * 2u;
    sampledBinding = storageBinding + 1u;
    textureEntries[storageBinding].binding     = storageBinding;
    textureEntries[storageBinding].bindingType = GPU_BINDING_STORAGE_TEXTURE;
    textureEntries[storageBinding].textureView =
      state.formatTextures[i].storageView;
    textureEntries[sampledBinding].binding     = sampledBinding;
    textureEntries[sampledBinding].bindingType = GPU_BINDING_SAMPLED_TEXTURE;
    textureEntries[sampledBinding].textureView =
      state.formatTextures[i].sampledView;
  }
  groupInfo.label      = "graphics-cuda-texture-group";
  groupInfo.layout     = textureLayout->bindGroupLayouts[0];
  groupInfo.pEntries   = textureEntries;
  groupInfo.entryCount = 5u + InteropFormatCount * 2u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.textureGroup) != GPU_OK ||
      !state.textureGroup) {
    fprintf(stderr, "CUDA interop texture bind group creation failed\n");
    goto cleanup;
  }

  dataEntries[0].binding       = 0u;
  dataEntries[0].bindingType   = GPU_BINDING_READ_ONLY_STORAGE_BUFFER;
  dataEntries[0].buffer.buffer = state.cudaBuffer;
  dataEntries[0].buffer.size   = cudaBufferInfo.sizeBytes;
  dataEntries[1].binding       = 1u;
  dataEntries[1].bindingType   = GPU_BINDING_STORAGE_BUFFER;
  dataEntries[1].buffer.buffer = state.cudaBuffer;
  dataEntries[1].buffer.size   = cudaBufferInfo.sizeBytes;
  groupInfo.label              = "graphics-cuda-data-group";
  groupInfo.layout             = shaderLayout->bindGroupLayouts[1];
  groupInfo.pEntries           = dataEntries;
  groupInfo.entryCount         = 2u;
  if (GPUCreateBindGroup(state.cudaDevice,
                         &groupInfo,
                         &state.dataGroup) != GPU_OK ||
      !state.dataGroup) {
    fprintf(stderr, "CUDA interop data bind group creation failed\n");
    goto cleanup;
  }

  for (uint32_t iteration = 0u; iteration < RoundtripCount; iteration++) {
    if (!run_roundtrip(&state, iteration)) {
      fprintf(stderr,
              "graphics/CUDA roundtrip %u failed\n",
              iteration + 1u);
      goto cleanup;
    }
  }
  if (!run_texture_roundtrip(&state)) {
    fprintf(stderr, "graphics/CUDA texture roundtrip failed\n");
    goto cleanup;
  }
  status = 0;

cleanup:
  GPUDestroyBindGroup(state.textureGroup);
  GPUDestroyBindGroup(state.dataGroup);
  GPUDestroyBindGroup(state.paramsGroup);
  GPUDestroyComputePipeline(state.texturePipeline);
  GPUDestroyComputePipeline(state.pipeline);
  GPUDestroyShaderLayout(textureLayout);
  GPUDestroyShaderLibrary(textureLibrary);
  GPUDestroyShaderLayout(shaderLayout);
  GPUDestroyShaderLibrary(library);
  GPUDestroyFence(state.acquireFence);
  GPUDestroyFence(state.releaseFence);
  GPUDestroyBuffer(state.paramsBuffer);
  GPUDestroyBuffer(state.textureReadback);
  GPUDestroyBuffer(state.textureCudaReadback);
  for (uint32_t i = 0u; i < InteropFormatCount; i++) {
    GPUDestroyBuffer(state.formatTextures[i].readback);
    GPUDestroyTextureView(state.formatTextures[i].sampledView);
    GPUDestroyTextureView(state.formatTextures[i].storageView);
  }
  GPUDestroyTextureView(state.cudaCubeArrayTextureView);
  GPUDestroyTextureView(state.cudaCubeTextureView);
  GPUDestroyTextureView(state.cudaSampledTextureView);
  GPUDestroyTextureView(state.cudaTextureView);
  if (cudaFirst) {
    GPUDestroySemaphore(state.cudaSemaphore);
    GPUDestroySemaphore(state.graphicsSemaphore);
    GPUDestroyBuffer(state.cudaBuffer);
    GPUDestroyBuffer(state.graphicsBuffer);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].cudaTexture);
    }
    GPUDestroyTexture(state.cudaCubeArrayTexture);
    GPUDestroyTexture(state.cudaCubeTexture);
    GPUDestroyTexture(state.cudaTexture);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].graphicsTexture);
    }
    GPUDestroyTexture(state.graphicsCubeArrayTexture);
    GPUDestroyTexture(state.graphicsCubeTexture);
    GPUDestroyTexture(state.graphicsTexture);
  } else {
    GPUDestroySemaphore(state.graphicsSemaphore);
    GPUDestroySemaphore(state.cudaSemaphore);
    GPUDestroyBuffer(state.graphicsBuffer);
    GPUDestroyBuffer(state.cudaBuffer);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].graphicsTexture);
    }
    GPUDestroyTexture(state.graphicsCubeArrayTexture);
    GPUDestroyTexture(state.graphicsCubeTexture);
    GPUDestroyTexture(state.graphicsTexture);
    for (uint32_t i = 0u; i < InteropFormatCount; i++) {
      GPUDestroyTexture(state.formatTextures[i].cudaTexture);
    }
    GPUDestroyTexture(state.cudaCubeArrayTexture);
    GPUDestroyTexture(state.cudaCubeTexture);
    GPUDestroyTexture(state.cudaTexture);
  }
  GPUDestroyDeviceInteropEXT(state.interop);
  if (cudaFirst) {
    GPUDestroyDevice(state.cudaDevice);
    GPUDestroyDevice(state.graphicsDevice);
  } else {
    GPUDestroyDevice(state.graphicsDevice);
    GPUDestroyDevice(state.cudaDevice);
  }
  free(cudaAdapters.items);
  free(graphicsAdapters.items);
  if (cudaFirst) {
    GPUDestroyInstance(cudaInstance);
    GPUDestroyInstance(graphicsInstance);
  } else {
    GPUDestroyInstance(graphicsInstance);
    GPUDestroyInstance(cudaInstance);
  }
  free(artifact);
  free(textureArtifact);

  if (status == 0) {
    puts("graphics/CUDA USL interop validation passed");
  }
  return status;
}
