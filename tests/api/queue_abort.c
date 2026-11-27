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

/*
 * a kernel that never ends must not take the queue's waits with it. it spins
 * on a flag in host memory and signals a semaphore; behind it a queue write
 * and a command buffer that waits for that semaphore. the system gives up on
 * work stalled like that (a GPU timeout); then every wait returns, each
 * bounded: the work behind it and the stuck one with GPU_ERROR_DEVICE_LOST,
 * a fresh submit and GPUQueueWaitIdle at once, and the device reports the
 * loss. only then the flag ends the kernel and the device goes.
 *   queue_abort <queue_abort.us>
 */

#include "test.h"

#include <inttypes.h>

#if defined(_WIN32)
#  include <windows.h>
#else
#  include <unistd.h>
#endif

enum {
  QA_STOP  = 0u,
  QA_COUNT = 1u,
  QA_MARK  = 2u,
  QA_WORDS = 4u
};

/* the system gives up on a stuck queue in seconds; this long means a hang */
#define QA_WAIT_NS (30ull * 1000000000ull)

typedef struct QATest {
  GPUDevice          *device;
  GPUQueue           *queue;
  GPUShaderLibrary   *library;
  GPUShaderLayout    *layout;
  GPUComputePipeline *spin, *mark;
  GPUBuffer          *state;
  GPUBindGroup       *group;
  volatile uint32_t  *words;
  uint32_t            errors, lost;
} QATest;

static size_t
qa_pageSize(void) {
#if defined(_WIN32)
  SYSTEM_INFO info;

  GetSystemInfo(&info);
  return info.dwPageSize;
#else
  long size = sysconf(_SC_PAGESIZE);

  return size > 0 ? (size_t)size : 0u;
#endif
}

static void
qa_error(GPUDevice *device, const GPUDeviceErrorInfo *error, void *user) {
  QATest *t = user;

  (void)device;
  t->errors++;
  t->lost += error->type == GPU_DEVICE_ERROR_LOST;
  printf("queue abort: device error (type %d, result %d): %s\n", (int)error->type, (int)error->result,
         error->message ? error->message : "");
}

static bool
qa_pipeline(QATest *t, const char *entry, GPUComputePipeline **out) {
  GPUComputePipelineCreateInfo info = {0};

  info.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.layout           = t->layout->pipelineLayout;
  info.library          = t->library;
  info.entryPoint       = entry;

  return GPUCreateComputePipeline(t->device, &info, out) == GPU_OK;
}

/* the state in host memory, so the CPU can end the kernel while it runs */

static bool
qa_setup(QATest *t, const char *path, void *host, uint64_t hostSize) {
  GPUBufferHostMemoryEXT hostInfo   = {0};
  GPUBufferCreateInfo    bufferInfo = {0};
  GPUBindGroupCreateInfo groupInfo  = {0};
  GPUBindGroupEntry      entry      = {0};
  void                  *bytecode;
  uint64_t               size;
  bool                   ok;

  if (!(bytecode = gpu_test_read_file(path, &size)))
    return false;

  ok = GPUCreateShaderLibraryFromUSL(t->device, bytecode, size, &t->library) == GPU_OK
       && GPUCreateShaderLayout(t->device, t->library, &t->layout) == GPU_OK
       && qa_pipeline(t, "queue_abort_spin_cs", &t->spin) && qa_pipeline(t, "queue_abort_mark_cs", &t->mark);
  free(bytecode);

  memset(host, 0, (size_t)hostSize);
  hostInfo.chain.sType        = GPU_STRUCTURE_TYPE_BUFFER_HOST_MEMORY_EXT;
  hostInfo.chain.structSize   = sizeof(hostInfo);
  hostInfo.pData              = host;
  hostInfo.allocationSize     = hostSize;
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.chain.pNext      = &hostInfo;
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DST | GPU_BUFFER_USAGE_COPY_SRC;
  bufferInfo.sizeBytes        = QA_WORDS * sizeof(uint32_t);
  ok       = ok && GPUCreateBuffer(t->device, &bufferInfo, &t->state) == GPU_OK;
  t->words = host;

  entry.binding       = 0u;
  entry.bindingType   = GPU_BINDING_STORAGE_BUFFER;
  entry.buffer.buffer = t->state;
  entry.buffer.size   = QA_WORDS * sizeof(uint32_t);

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.layout           = ok ? t->layout->bindGroupLayouts[0] : NULL;
  groupInfo.entryCount       = 1u;
  groupInfo.pEntries         = &entry;

  return ok && GPUCreateBindGroup(t->device, &groupInfo, &t->group) == GPU_OK;
}

static GPUResult
qa_submit(QATest *t, GPUComputePipeline *pipeline, const char *label, GPUFence *fence,
          const GPUQueueSemaphoreWait *wait, const GPUQueueSemaphoreSignal *signal) {
  GPUQueueSubmitExInfo   submit = {0};
  GPUCommandBuffer      *cmdb   = NULL;
  GPUComputePassEncoder *pass;
  GPUResult              result;

  if ((result = GPUAcquireCommandBuffer(t->queue, label, &cmdb)) != GPU_OK || !cmdb)
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;

  if ((pass = GPUBeginComputePass(cmdb, label))) {
    GPUBindComputePipeline(pass, pipeline);
    GPUBindComputeGroup(pass, 0u, t->group, 0u, NULL);
    GPUDispatch(pass, 1u, 1u, 1u);
    GPUEndComputePass(pass);
  }

  submit.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_EX_INFO;
  submit.chain.structSize   = sizeof(submit);
  submit.commandBufferCount = 1u;
  submit.ppCommandBuffers   = &cmdb;
  submit.fence              = fence;
  submit.pWaits             = wait;
  submit.waitCount          = wait ? 1u : 0u;
  submit.pSignals           = signal;
  submit.signalCount        = signal ? 1u : 0u;

  return GPUQueueSubmitEx(t->queue, &submit);
}

static GPUResult
qa_wait(GPUFence *fence, const char *name, uint64_t start) {
  GPUResult result;

  result = GPUWaitFence(fence, QA_WAIT_NS);
  printf("queue abort: %s: %d at %.2f s\n", name, (int)result, (double)(gpu_test_now_ns() - start) / 1e9);

  return result;
}

int
main(int argc, char **argv) {
  GPUDeviceCreateInfo deviceInfo = {0};
  const GPUFeature    features[] = {GPU_FEATURE_BUFFER_HOST_MEMORY_EXT};
  QATest              t = {0};
  GPUInstance        *instance = NULL;
  GPUAdapter         *adapter  = NULL;
  GPUFence           *spun = NULL, *after = NULL, *again = NULL;
  GPUSemaphore       *spunSemaphore = NULL;
  GPUQueueSemaphoreSignal signal;
  GPUQueueSemaphoreWait   wait;
  uint8_t            *allocation = NULL, *host;
  uint32_t            zero = 0u, spins;
  GPUResult           spunResult, afterResult, againResult, idleResult;
  uint64_t            start;
  size_t              page;
  bool                ok;

  if (argc < 2) {
    fprintf(stderr, "usage: %s <queue_abort.us>\n", argv[0]);
    return 2;
  }

  page = qa_pageSize();
  deviceInfo.required.pFeatures    = features;
  deviceInfo.required.featureCount = 1u;

  if (GPUCreateInstance(NULL, &instance) != GPU_OK || gpu_test_request_adapter(instance, &adapter) != GPU_OK
      || !adapter || !GPUIsFeatureSupported(adapter, GPU_FEATURE_BUFFER_HOST_MEMORY_EXT) || page == 0u
      || gpu_test_create_device(adapter, &deviceInfo, &t.device) != GPU_OK || !t.device
      || !(allocation = malloc(page * 2u))) {
    GPUDestroyDevice(t.device);
    GPUDestroyInstance(instance);
    free(allocation);
    puts("queue abort: no device that borrows host memory");
    return 77;
  }

  host = (uint8_t *)(((uintptr_t)allocation + page - 1u) / page * page);
  (void)GPUSetDeviceErrorCallback(t.device, qa_error, &t);
  t.queue = GPUGetQueue(t.device, GPU_QUEUE_GRAPHICS, 0u);
  ok      = t.queue && qa_setup(&t, argv[1], host, page) && GPUQueueWaitIdle(t.queue) == GPU_OK
            && GPUCreateFence(t.device, NULL, &spun) == GPU_OK && GPUCreateFence(t.device, NULL, &after) == GPU_OK
            && GPUCreateFence(t.device, NULL, &again) == GPU_OK
            && GPUCreateSemaphore(t.device, NULL, &spunSemaphore) == GPU_OK;

  if (!ok) {
    fprintf(stderr, "queue abort: setup failed\n");
    goto done;
  }

  /* the endless kernel, then a queue write and a command buffer behind it */

  signal.semaphore = spunSemaphore;
  signal.value     = 1u;
  wait.semaphore   = spunSemaphore;
  wait.value       = 1u;
  wait.waitStages  = GPU_STAGE_COMPUTE;
  start            = gpu_test_now_ns();
  ok               = qa_submit(&t, t.spin, "queue-abort-spin", spun, NULL, &signal) == GPU_OK
                     && GPUQueueWriteBuffer(t.queue, t.state, QA_MARK * sizeof(uint32_t), &zero, sizeof(zero))
                          == GPU_OK
                     && qa_submit(&t, t.mark, "queue-abort-after", after, &wait, NULL) == GPU_OK;

  if (!ok) {
    fprintf(stderr, "queue abort: submits failed\n");
    t.words[QA_STOP] = 1u;
    goto done;
  }

  afterResult = qa_wait(after, "the work behind it", start);
  spunResult  = qa_wait(spun, "the stuck one", start);

  /* the queue after it: a fresh submit, then the idle wait */

  againResult = qa_submit(&t, t.mark, "queue-abort-again", again, NULL, NULL);
  printf("queue abort: a fresh submit: %d\n", (int)againResult);
  againResult = againResult == GPU_OK ? qa_wait(again, "its wait", start) : againResult;
  fflush(stdout);
  idleResult = GPUQueueWaitIdle(t.queue);
  printf("queue abort: GPUQueueWaitIdle: %d at %.2f s\n", (int)idleResult,
         (double)(gpu_test_now_ns() - start) / 1e9);

  spins            = t.words[QA_COUNT];
  t.words[QA_STOP] = 1u;
  printf("queue abort: %u device errors (%u lost); the kernel spun %" PRIu32 " times\n", t.errors, t.lost, spins);

  /* a kernel compiled away would finish: its fence GPU_OK. with the GPU busy
   * elsewhere it may not have started (0 spins); the waits behind it give up
   * the same */
  ok = spunResult == GPU_ERROR_DEVICE_LOST && afterResult == GPU_ERROR_DEVICE_LOST
       && againResult == GPU_ERROR_DEVICE_LOST && idleResult == GPU_ERROR_DEVICE_LOST && t.lost == 1u;

done:
  printf("queue abort: %s\n", ok ? "ok" : "FAILED");
  fflush(stdout);

  GPUDestroySemaphore(spunSemaphore);
  GPUDestroyFence(again);
  GPUDestroyFence(after);
  GPUDestroyFence(spun);
  GPUDestroyBindGroup(t.group);
  GPUDestroyBuffer(t.state);
  GPUDestroyComputePipeline(t.mark);
  GPUDestroyComputePipeline(t.spin);
  GPUDestroyShaderLayout(t.layout);
  GPUDestroyShaderLibrary(t.library);
  GPUDestroyDevice(t.device);
  GPUDestroyInstance(instance);
  free(allocation);

  return ok ? 0 : 1;
}
