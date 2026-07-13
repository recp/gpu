#include "test.h"

#if defined(_WIN32) || defined(WIN32)
#  include <windows.h>
typedef HANDLE        GPUThreadHandle;
typedef volatile LONG GPUThreadGate;
#else
#  include <pthread.h>
#  include <sched.h>
typedef pthread_t GPUThreadHandle;
typedef int       GPUThreadGate;
#endif

enum {
  GPU_THREADING_TEST_THREAD_COUNT = 4,
  GPU_THREADING_TEST_ROUND_COUNT  = 12
};

typedef struct GPUThreadContext {
  const void         *artifactData;
  GPUThreadGate      *start;
  GPUBindGroupLayout *layout;
  GPUBuffer          *sharedBuffer;
  GPUDevice          *device;
  uint64_t            artifactSize;
  bool                ok;
} GPUThreadContext;

static bool
gpu_threadStarted(GPUThreadGate *start) {
#if defined(_WIN32) || defined(WIN32)
  return InterlockedCompareExchange(start, 0, 0) != 0;
#else
  return __atomic_load_n(start, __ATOMIC_ACQUIRE) != 0;
#endif
}

static void
gpu_startThreads(GPUThreadGate *start) {
#if defined(_WIN32) || defined(WIN32)
  InterlockedExchange(start, 1);
#else
  __atomic_store_n(start, 1, __ATOMIC_RELEASE);
#endif
}

static bool
gpu_threadingRound(GPUThreadContext *ctx) {
  GPUBufferCreateInfo         bufferInfo = {0};
  GPUTextureCreateInfo        textureInfo = {0};
  GPUTextureViewCreateInfo    viewInfo = {0};
  GPUSamplerCreateInfo        samplerInfo = {0};
  GPUPipelineLayoutCreateInfo pipelineInfo = {0};
  GPUBindGroupCreateInfo      groupInfo = {0};
  GPUBindGroupEntry           groupEntry = {0};
  GPUBindGroupLayout         *layouts[1];
  GPUBindGroup               *localGroup;
  GPUBindGroup               *sharedGroup;
  GPUPipelineLayout          *pipelineLayout;
  GPUTextureView             *view;
  GPUTexture                 *texture;
  GPUSampler                 *sampler;
  GPUBuffer                  *buffer;
  bool                        ok;

  localGroup     = NULL;
  sharedGroup    = NULL;
  pipelineLayout = NULL;
  view           = NULL;
  texture        = NULL;
  sampler        = NULL;
  buffer         = NULL;
  ok             = false;

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "thread-buffer";
  bufferInfo.sizeBytes        = 256u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(ctx->device, &bufferInfo, &buffer) != GPU_OK ||
      !buffer) {
    goto cleanup;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "thread-texture";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = 4u;
  textureInfo.height           = 4u;
  textureInfo.depthOrLayers    = 1u;
  textureInfo.mipLevelCount    = 1u;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_SAMPLED |
                                 GPU_TEXTURE_USAGE_COPY_DST;
  if (GPUCreateTexture(ctx->device, &textureInfo, &texture) != GPU_OK ||
      !texture) {
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "thread-view";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    goto cleanup;
  }

  samplerInfo.chain.sType      = GPU_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.chain.structSize = sizeof(samplerInfo);
  samplerInfo.label            = "thread-sampler";
  if (GPUCreateSampler(ctx->device,
                       &samplerInfo,
                       false,
                       &sampler) != GPU_OK ||
      !sampler) {
    goto cleanup;
  }

  layouts[0]                        = ctx->layout;
  pipelineInfo.chain.sType          = GPU_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineInfo.chain.structSize     = sizeof(pipelineInfo);
  pipelineInfo.label                = "thread-pipeline-layout";
  pipelineInfo.bindGroupLayoutCount = 1u;
  pipelineInfo.ppBindGroupLayouts   = layouts;
  if (GPUCreatePipelineLayout(ctx->device,
                              &pipelineInfo,
                              &pipelineLayout) != GPU_OK ||
      !pipelineLayout) {
    goto cleanup;
  }

  groupEntry.binding          = 0u;
  groupEntry.bindingType      = GPU_BINDING_UNIFORM_BUFFER;
  groupEntry.buffer.buffer    = buffer;
  groupEntry.buffer.size      = 256u;
  groupEntry.stage            = GPUBindStageVertex;
  groupEntry.kind             = GPUBindKindBuffer;
  groupInfo.chain.sType       = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize  = sizeof(groupInfo);
  groupInfo.label             = "thread-local-group";
  groupInfo.layout            = ctx->layout;
  groupInfo.entryCount        = 1u;
  groupInfo.pEntries          = &groupEntry;
  if (GPUCreateBindGroup(ctx->device, &groupInfo, &localGroup) != GPU_OK ||
      !localGroup) {
    goto cleanup;
  }

  groupEntry.buffer.buffer = ctx->sharedBuffer;
  groupInfo.label          = "thread-shared-group";
  if (GPUCreateBindGroup(ctx->device, &groupInfo, &sharedGroup) != GPU_OK ||
      !sharedGroup) {
    goto cleanup;
  }

  ok = true;

cleanup:
  GPUDestroyBindGroup(sharedGroup);
  GPUDestroyBindGroup(localGroup);
  GPUDestroyPipelineLayout(pipelineLayout);
  GPUDestroySampler(sampler);
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  GPUDestroyBuffer(buffer);
  return ok;
}

static void
gpu_runThreadingTest(GPUThreadContext *ctx) {
  GPUShaderReflection reflection = {0};
  GPUShaderLibrary   *library;

  while (!gpu_threadStarted(ctx->start)) {
#if defined(_WIN32) || defined(WIN32)
    SwitchToThread();
#else
    sched_yield();
#endif
  }

  library = NULL;
  if (GPUCreateShaderLibraryFromUSL(ctx->device,
                                    ctx->artifactData,
                                    ctx->artifactSize,
                                    &library) != GPU_OK ||
      !library ||
      GPUGetShaderReflection(library, &reflection) != GPU_OK) {
    GPUFreeShaderReflection(&reflection);
    GPUDestroyShaderLibrary(library);
    ctx->ok = false;
    return;
  }
  GPUFreeShaderReflection(&reflection);

  ctx->ok = true;
  for (uint32_t i = 0u; i < GPU_THREADING_TEST_ROUND_COUNT; i++) {
    if (!gpu_threadingRound(ctx)) {
      ctx->ok = false;
      break;
    }
  }
  GPUDestroyShaderLibrary(library);
}

#if defined(_WIN32) || defined(WIN32)
static DWORD WINAPI
gpu_threadingMain(LPVOID context) {
  gpu_runThreadingTest(context);
  return 0u;
}
#else
static void *
gpu_threadingMain(void *context) {
  gpu_runThreadingTest(context);
  return NULL;
}
#endif

static bool
gpu_startThread(GPUThreadHandle  *thread,
                GPUThreadContext *ctx) {
#if defined(_WIN32) || defined(WIN32)
  *thread = CreateThread(NULL, 0u, gpu_threadingMain, ctx, 0u, NULL);
  return *thread != NULL;
#else
  return pthread_create(thread, NULL, gpu_threadingMain, ctx) == 0;
#endif
}

static void
gpu_joinThread(GPUThreadHandle thread) {
#if defined(_WIN32) || defined(WIN32)
  WaitForSingleObject(thread, INFINITE);
  CloseHandle(thread);
#else
  pthread_join(thread, NULL);
#endif
}

int
gpu_test_threading(GPUDevice *device, const char *artifactPath) {
  GPUBindGroupLayoutCreateInfo layoutInfo = {0};
  GPUBindGroupLayoutEntry      layoutEntry = {0};
  GPUBufferCreateInfo          bufferInfo = {0};
  GPUThreadContext             contexts[GPU_THREADING_TEST_THREAD_COUNT];
  GPUThreadHandle              threads[GPU_THREADING_TEST_THREAD_COUNT];
  GPUBindGroupLayout          *layout;
  GPUBuffer                   *sharedBuffer;
  void                        *artifactData;
  GPUThreadGate                start;
  uint64_t                     artifactSize;
  uint32_t                     threadCount;
  bool                         ok;

  layout       = NULL;
  sharedBuffer = NULL;
  artifactData = NULL;
  artifactSize = 0u;
  start        = 0;
  threadCount  = 0u;
  ok           = false;

  artifactData = gpu_test_read_file(artifactPath, &artifactSize);
  if (!artifactData || artifactSize == 0u) {
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "thread-shared-buffer";
  bufferInfo.sizeBytes        = 256u;
  bufferInfo.usage            = GPU_BUFFER_USAGE_UNIFORM;
  if (GPUCreateBuffer(device, &bufferInfo, &sharedBuffer) != GPU_OK ||
      !sharedBuffer) {
    goto cleanup;
  }

  layoutEntry.binding         = 0u;
  layoutEntry.bindingType     = GPU_BINDING_UNIFORM_BUFFER;
  layoutEntry.visibility      = GPU_SHADER_STAGE_VERTEX_BIT;
  layoutEntry.arrayCount      = 1u;
  layoutEntry.stage           = GPUBindStageVertex;
  layoutEntry.kind            = GPUBindKindBuffer;
  layoutInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_LAYOUT_CREATE_INFO;
  layoutInfo.chain.structSize = sizeof(layoutInfo);
  layoutInfo.label            = "thread-layout";
  layoutInfo.entryCount       = 1u;
  layoutInfo.pEntries         = &layoutEntry;
  if (GPUCreateBindGroupLayout(device, &layoutInfo, &layout) != GPU_OK ||
      !layout) {
    goto cleanup;
  }

  for (uint32_t i = 0u; i < GPU_THREADING_TEST_THREAD_COUNT; i++) {
    contexts[i].artifactData = artifactData;
    contexts[i].device       = device;
    contexts[i].layout       = layout;
    contexts[i].sharedBuffer = sharedBuffer;
    contexts[i].start        = &start;
    contexts[i].artifactSize = artifactSize;
    contexts[i].ok           = false;
    if (!gpu_startThread(&threads[i], &contexts[i])) {
      break;
    }
    threadCount++;
  }

  gpu_startThreads(&start);
  for (uint32_t i = 0u; i < threadCount; i++) {
    gpu_joinThread(threads[i]);
  }

  ok = threadCount == GPU_THREADING_TEST_THREAD_COUNT;
  for (uint32_t i = 0u; i < threadCount; i++) {
    ok = contexts[i].ok && ok;
  }

cleanup:
  GPUDestroyBindGroupLayout(layout);
  GPUDestroyBuffer(sharedBuffer);
  free(artifactData);
  if (!ok) {
    fprintf(stderr, "concurrent resource creation failed\n");
  }
  return ok;
}
