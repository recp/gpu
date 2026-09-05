#include <gpu/gpu.h>

#include "../../../samples/common/webgpu.h"
#include "../../../src/backend/webgpu/common.h"
#include "../f16-builtins-usl/f16_builtins.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct WebGPUF16Validation {
  WebGPURequest            request;
  GPUInstance             *instance;
  GPUDevice               *device;
  GPUShaderLibrary        *library;
  GPUShaderLayout         *shaderLayout;
  GPUComputePipeline      *pipeline;
  GPUBuffer               *buffers[2];
  GPUBindGroup            *bindGroup;
  WGPUBuffer               staging;
  void                    *artifact;
  uint64_t                 artifactSize;
  uint16_t                 output[F16_BUILTIN_OUTPUT_ROWS][4];
  bool                     finished;
} WebGPUF16Validation;

static WebGPUF16Validation validation;

static bool
webgpu_f16_setup_error(const char *stage) {
  fprintf(stderr, "GPU: WebGPU F16 %s failed\n", stage ? stage : "setup");
  return false;
}

static void
webgpu_f16_cleanup(WebGPUF16Validation *state) {
  if (!state) return;
  if (state->staging) {
    wgpuBufferDestroy(state->staging);
    wgpuBufferRelease(state->staging);
    state->staging = NULL;
  }
  GPUDestroyBindGroup(state->bindGroup);
  GPUDestroyBuffer(state->buffers[0]);
  GPUDestroyBuffer(state->buffers[1]);
  GPUDestroyComputePipeline(state->pipeline);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  GPUDestroyDevice(state->device);
  GPUDestroyInstance(state->instance);
  free(state->artifact);
  state->bindGroup    = NULL;
  state->buffers[0]   = NULL;
  state->buffers[1]   = NULL;
  state->pipeline     = NULL;
  state->shaderLayout = NULL;
  state->library      = NULL;
  state->device       = NULL;
  state->instance     = NULL;
  state->artifact     = NULL;
}

static void
webgpu_f16_finish(WebGPUF16Validation *state,
                  const char          *message,
                  bool                 failed) {
  if (!state || state->finished) return;
  state->finished = true;
  set_status(message, failed ? 1 : 0);
  webgpu_f16_cleanup(state);
}

static void
webgpu_f16_mapped(WGPUMapAsyncStatus status,
                  WGPUStringView     message,
                  void              *userData,
                  void              *unused) {
  WebGPUF16Validation *state;
  const void          *mapped;
  char                 statusText[96];

  (void)message;
  (void)unused;
  state = userData;
  if (!state || status != WGPUMapAsyncStatus_Success || !state->staging) {
    webgpu_f16_finish(state, "GPU: WebGPU F16 readback map failed", true);
    return;
  }
  mapped = wgpuBufferGetConstMappedRange(state->staging,
                                         0u,
                                         sizeof(state->output));
  if (!mapped) {
    webgpu_f16_finish(state, "GPU: WebGPU F16 mapped range failed", true);
    return;
  }
  memcpy(state->output, mapped, sizeof(state->output));
  wgpuBufferUnmap(state->staging);
  if (!gpu_f16_builtin_validate(state->output)) {
    webgpu_f16_finish(state,
                      "GPU: WebGPU F16 builtin validation failed",
                      true);
    return;
  }
  (void)snprintf(statusText,
                 sizeof(statusText),
                 "GPU: WebGPU F16 builtin validation passed (%u/%u)",
                 (unsigned)F16_BUILTIN_CHECKS,
                 (unsigned)F16_BUILTIN_CHECKS);
  webgpu_f16_finish(state, statusText, false);
}

static bool
webgpu_f16_begin_readback(WebGPUF16Validation *state) {
  WGPUBufferDescriptor         bufferInfo = WGPU_BUFFER_DESCRIPTOR_INIT;
  WGPUCommandEncoderDescriptor encoderInfo =
    WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  WGPUCommandBufferDescriptor commandInfo =
    WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
  WGPUBufferMapCallbackInfo callbackInfo =
    WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
  GPUDeviceWebGPU       *native;
  WGPUCommandEncoder     encoder;
  WGPUCommandBuffer      command;

  /* GPUQueueReadBuffer is deliberately synchronous and unavailable in a
   * browser. Keep this validation-only readback asynchronous and outside the
   * public runtime contract. */
  native = gpu_webgpuDevice(state ? state->device : NULL);
  if (!state || !native || !native->device || !native->queue ||
      !state->buffers[1] || !state->buffers[1]->_priv) {
    return webgpu_f16_setup_error("native readback setup");
  }

  bufferInfo.label = gpu_webgpuString("f16-builtins-readback");
  bufferInfo.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
  bufferInfo.size  = sizeof(state->output);
  state->staging   = wgpuDeviceCreateBuffer(native->device, &bufferInfo);
  if (!state->staging) return webgpu_f16_setup_error("staging buffer");

  encoder = wgpuDeviceCreateCommandEncoder(native->device, &encoderInfo);
  if (!encoder) return webgpu_f16_setup_error("readback encoder");
  wgpuCommandEncoderCopyBufferToBuffer(
    encoder,
    (WGPUBuffer)state->buffers[1]->_priv,
    0u,
    state->staging,
    0u,
    sizeof(state->output)
  );
  command = wgpuCommandEncoderFinish(encoder, &commandInfo);
  wgpuCommandEncoderRelease(encoder);
  if (!command) return webgpu_f16_setup_error("readback command");
  wgpuQueueSubmit(native->queue, 1u, &command);
  wgpuCommandBufferRelease(command);

  callbackInfo.mode      = WGPUCallbackMode_AllowSpontaneous;
  callbackInfo.callback  = webgpu_f16_mapped;
  callbackInfo.userdata1 = state;
  wgpuBufferMapAsync(state->staging,
                     WGPUMapMode_Read,
                     0u,
                     sizeof(state->output),
                     callbackInfo);
  return true;
}

static bool
webgpu_f16_dispatch(WebGPUF16Validation *state) {
  GPUComputePipelineCreateInfo  pipelineInfo = {0};
  GPUBufferCreateInfo           bufferInfo = {0};
  GPUBindGroupEntry             entries[2] = {0};
  GPUBindGroupCreateInfo        groupInfo = {0};
  GPUQueueSubmitInfo            submitInfo = {0};
  GPUCommandBuffer             *cmdb;
  GPUComputePassEncoder        *pass;
  GPUQueue                     *queue;
  uint64_t                      sizes[2];
  GPUResult                     result;

  queue = GPUGetQueue(state->device, GPU_QUEUE_COMPUTE, 0u);
  if (!queue || !GPUIsFeatureEnabled(state->device,
                                     GPU_FEATURE_SHADER_F16)) {
    return webgpu_f16_setup_error("queue or shader-f16 feature");
  }
  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         state->artifact,
                                         state->artifactSize,
                                         &state->library);
  if (result != GPU_OK || !state->library) {
    fprintf(stderr,
            "GPU: WebGPU F16 USL library failed (%d, %llu bytes)\n",
            (int)result,
            (unsigned long long)state->artifactSize);
    return false;
  }
  result = GPUCreateShaderLayout(state->device,
                                 state->library,
                                 &state->shaderLayout);
  if (result != GPU_OK || !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts[0] ||
      !state->shaderLayout->pipelineLayout) {
    return webgpu_f16_setup_error("reflected layout");
  }

  pipelineInfo.chain.sType      = GPU_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize = sizeof(pipelineInfo);
  pipelineInfo.label            = "webgpu-f16-builtins";
  pipelineInfo.layout           = state->shaderLayout->pipelineLayout;
  pipelineInfo.library          = state->library;
  pipelineInfo.entryPoint       = "f16_builtins";
  if (GPUCreateComputePipeline(state->device,
                               &pipelineInfo,
                               &state->pipeline) != GPU_OK ||
      !state->pipeline) {
    return webgpu_f16_setup_error("compute pipeline");
  }

  sizes[0] = sizeof(gpu_f16_builtin_inputs);
  sizes[1] = sizeof(state->output);
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.usage            = GPU_BUFFER_USAGE_STORAGE |
                                GPU_BUFFER_USAGE_COPY_SRC |
                                GPU_BUFFER_USAGE_COPY_DST;
  for (uint32_t binding = 0u; binding < 2u; binding++) {
    bufferInfo.sizeBytes = sizes[binding];
    if (GPUCreateBuffer(state->device,
                        &bufferInfo,
                        &state->buffers[binding]) != GPU_OK ||
        !state->buffers[binding] ||
        GPUQueueWriteBuffer(queue,
                            state->buffers[binding],
                            0u,
                            binding == 0u
                              ? (const void *)gpu_f16_builtin_inputs
                              : (const void *)state->output,
                            sizes[binding]) != GPU_OK) {
      return webgpu_f16_setup_error("storage buffer");
    }
    entries[binding].binding       = binding;
    entries[binding].bindingType   = binding == 0u
                                       ? GPU_BINDING_READ_ONLY_STORAGE_BUFFER
                                       : GPU_BINDING_STORAGE_BUFFER;
    entries[binding].buffer.buffer = state->buffers[binding];
    entries[binding].buffer.size   = sizes[binding];
  }

  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label            = "webgpu-f16-builtins-group";
  groupInfo.layout           = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.entryCount       = 2u;
  groupInfo.pEntries         = entries;
  cmdb                       = NULL;
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->bindGroup) != GPU_OK ||
      !state->bindGroup ||
      GPUAcquireCommandBuffer(queue, "webgpu-f16-builtins", &cmdb) != GPU_OK ||
      !cmdb || !(pass = GPUBeginComputePass(cmdb, "webgpu-f16-builtins"))) {
    return webgpu_f16_setup_error("bind group or command buffer");
  }
  GPUBindComputePipeline(pass, state->pipeline);
  GPUBindComputeGroup(pass, 0u, state->bindGroup, 0u, NULL);
  GPUDispatch(pass, F16_BUILTIN_CASES, 1u, 1u);
  GPUEndComputePass(pass);

  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = &cmdb;
  if (GPUQueueSubmit(queue, &submitInfo) != GPU_OK)
    return webgpu_f16_setup_error("compute submit");
  return webgpu_f16_begin_readback(state);
}

static void
webgpu_f16_ready(GPUResult   result,
                 GPUAdapter *adapter,
                 GPUDevice  *device,
                 void       *userData) {
  WebGPUF16Validation *state;
  GPURuntimeConfig     runtimeConfig = {0};

  (void)adapter;
  state = userData;
  if (!state || result != GPU_OK || !device) {
    webgpu_f16_finish(state, "GPU: WebGPU F16 device request failed", true);
    return;
  }
  state->device = device;
  if (!GPUIsFeatureEnabled(device, GPU_FEATURE_SHADER_F16)) {
    webgpu_f16_finish(state,
                      "GPU: WebGPU shader-f16 unavailable on this adapter",
                      true);
    return;
  }
  runtimeConfig.chain.sType       = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize  = sizeof(runtimeConfig);
  runtimeConfig.validationMode    = GPU_VALIDATION_FULL;
  runtimeConfig.enableVerboseLogs = true;
  if (GPUConfigureRuntime(device, &runtimeConfig) != GPU_OK ||
      !webgpu_f16_dispatch(state)) {
    webgpu_f16_finish(state, "GPU: WebGPU F16 dispatch setup failed", true);
  }
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUFeature            feature = GPU_FEATURE_SHADER_F16;

  if (setenv("USL_STRICT_IEEE", "1", 1) != 0 ||
      !read_file("/f16_builtins.us",
                 &validation.artifact,
                 &validation.artifactSize)) {
    webgpu_f16_finish(&validation,
                      "GPU: WebGPU F16 artifact setup failed",
                      true);
    return 1;
  }
  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  if (GPUCreateInstance(&info, &validation.instance) != GPU_OK ||
      !validation.instance ||
      request_webgpu_device_features(validation.instance,
                                     &validation.request,
                                     webgpu_f16_ready,
                                     &validation,
                                     &feature,
                                     1u) != GPU_OK) {
    webgpu_f16_finish(&validation,
                      "GPU: WebGPU F16 instance setup failed",
                      true);
    return 1;
  }
  return 0;
}
