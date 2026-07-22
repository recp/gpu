#include "VRSCompare.h"

#include <stddef.h>
#include <string.h>

typedef struct VRSCompareVertex {
  float position[4];
  float uv[2];
} VRSCompareVertex;

static const VRSCompareVertex vrs_compare_vertices[] = {
  { { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
  { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
  { {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
  { {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } }
};

GPUResult
GPUSampleChooseVRSRate(const GPUAdapter *adapter,
                       GPUShadingRateEXT *outRate) {
  static const GPUShadingRateEXT preferredRates[] = {
    GPU_SHADING_RATE_2X2_EXT,
    GPU_SHADING_RATE_1X2_EXT,
    GPU_SHADING_RATE_2X1_EXT,
    GPU_SHADING_RATE_2X4_EXT,
    GPU_SHADING_RATE_4X2_EXT,
    GPU_SHADING_RATE_4X4_EXT
  };
  GPUVRSCapabilitiesEXT caps;
  GPUResult             result;

  if (!adapter || !outRate) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  memset(&caps, 0, sizeof(caps));
  result = GPUGetVRSCapabilitiesEXT(adapter, &caps);
  if (result != GPU_OK ||
      (caps.modes & GPU_VRS_DRAW_RATE_BIT_EXT) == 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(preferredRates); i++) {
    if ((caps.rates & (1u << preferredRates[i])) != 0u) {
      *outRate = preferredRates[i];
      return GPU_OK;
    }
  }
  return GPU_ERROR_UNSUPPORTED;
}

static GPUResult
create_pipeline(GPUSampleVRSCompare *state,
                const char          *label,
                const char          *fragmentEntry,
                GPURenderPipeline  **outPipeline) {
  GPUVertexAttribute          attributes[2] = {0};
  GPUVertexBufferLayout       vertexLayout  = {0};
  GPUColorTargetState         color         = {0};
  GPURenderPipelineCreateInfo info          = {0};

  attributes[0].shaderLocation = 0u;
  attributes[0].format         = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[0].offset         = offsetof(VRSCompareVertex, position);
  attributes[1].shaderLocation = 1u;
  attributes[1].format         = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[1].offset         = offsetof(VRSCompareVertex, uv);
  vertexLayout.strideBytes     = sizeof(VRSCompareVertex);
  vertexLayout.stepMode        = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.pAttributes     = attributes;
  vertexLayout.attributeCount  = GPU_ARRAY_LEN(attributes);

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.enabled   = false;
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType             = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize        = sizeof(info);
  info.label                   = label;
  info.layout                  = state->shaderLayout->pipelineLayout;
  info.library                 = state->library;
  info.vertexEntry             = "vrs_compare_vs";
  info.fragmentEntry           = fragmentEntry;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.pColorTargets           = &color;
  info.colorTargetCount        = 1u;
  info.primitiveTopology       = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                = GPU_CULL_MODE_NONE;
  info.frontFace               = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount = 1u;
  info.multisample.sampleMask  = 0xffffffffu;
  return GPUCreateRenderPipeline(state->device, &info, outPipeline);
}

static GPUResult
create_vertex_buffer(GPUSampleVRSCompare *state) {
  GPUBufferCreateInfo info = {0};
  GPUResult           result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "vrs-compare-vertices";
  info.sizeBytes        = sizeof(vrs_compare_vertices);
  info.usage            = GPU_BUFFER_USAGE_VERTEX | GPU_BUFFER_USAGE_COPY_DST;
  result = GPUCreateBuffer(state->device, &info, &state->vertexBuffer);
  if (result != GPU_OK) {
    return result;
  }
  return GPUQueueWriteBuffer(state->queue,
                             state->vertexBuffer,
                             0u,
                             vrs_compare_vertices,
                             sizeof(vrs_compare_vertices));
}

GPUResult
GPUSampleVRSCompareInit(GPUSampleVRSCompare *state,
                        GPUDevice           *device,
                        GPUQueue            *queue,
                        GPUSwapchain        *swapchain,
                        GPUShaderLibrary    *library,
                        GPUShaderLayout     *shaderLayout,
                        GPUShadingRateEXT    coarseRate,
                        uint32_t             width,
                        uint32_t             height) {
  GPUResult result;

  if (!state || !device || !queue || !swapchain || !library || !shaderLayout ||
      !shaderLayout->pipelineLayout || width == 0u || height == 0u ||
      coarseRate == GPU_SHADING_RATE_1X1_EXT) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  memset(state, 0, sizeof(*state));
  state->device       = device;
  state->queue        = queue;
  state->swapchain    = swapchain;
  state->library      = library;
  state->shaderLayout = shaderLayout;
  state->coarseRate   = coarseRate;
  state->width        = width;
  state->height       = height;

  result = create_pipeline(state,
                           "vrs-compare-fine-pipeline",
                           "vrs_fine_fs",
                           &state->finePipeline);
  if (result == GPU_OK) {
    result = create_pipeline(state,
                             "vrs-compare-coarse-pipeline",
                             "vrs_coarse_fs",
                             &state->coarsePipeline);
  }
  if (result == GPU_OK) {
    result = create_vertex_buffer(state);
  }
  if (result != GPU_OK) {
    GPUSampleVRSCompareDestroy(state);
  }
  return result;
}

GPUResult
GPUSampleVRSCompareResize(GPUSampleVRSCompare *state,
                          uint32_t             width,
                          uint32_t             height) {
  if (!state || width == 0u || height == 0u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }
  state->width  = width;
  state->height = height;
  return GPU_OK;
}

static void
draw_region(GPURenderPassEncoder *pass,
            GPUSampleVRSCompare *state,
            GPURenderPipeline   *pipeline,
            uint32_t             x,
            uint32_t             width,
            GPUShadingRateEXT    rate) {
  GPUViewport      viewport = {0};
  GPUScissorRect   scissor  = {0};
  GPUBufferBinding binding = {0};

  viewport.x        = (float)x;
  viewport.width    = (float)width;
  viewport.height   = (float)state->height;
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  scissor.x         = (int32_t)x;
  scissor.width     = width;
  scissor.height    = state->height;
  GPUSetViewport(pass, &viewport);
  GPUSetScissor(pass, &scissor);
  GPUSetFragmentShadingRateEXT(pass,
                               rate,
                               GPU_SHADING_RATE_COMBINER_KEEP_EXT,
                               GPU_SHADING_RATE_COMBINER_KEEP_EXT);
  binding.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(pass, pipeline);
  GPUBindVertexBuffers(pass, 0u, 1u, &binding);
  GPUDraw(pass,
          (uint32_t)GPU_ARRAY_LEN(vrs_compare_vertices),
          1u,
          0u,
          0u);
}

GPUResult
GPUSampleVRSCompareRender(GPUSampleVRSCompare          *state,
                          void                         *completionSender,
                          GPUCommandBufferCompletionFn  completion) {
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPURenderPassColorAttachment  color    = {0};
  GPURenderPassCreateInfo       passInfo = {0};
  GPUResult                     result;
  uint32_t                      leftWidth;

  if (!state || !state->swapchain || !state->finePipeline ||
      !state->coarsePipeline || !state->vertexBuffer || state->width < 2u) {
    return GPU_ERROR_INVALID_ARGUMENT;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return GPU_ERROR_BACKEND_FAILURE;
  }
  cmdb   = NULL;
  result = GPUAcquireCommandBuffer(state->queue, "vrs-compare-frame", &cmdb);
  if (result != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return result != GPU_OK ? result : GPU_ERROR_BACKEND_FAILURE;
  }
  if (completion) {
    GPUSetCommandBufferCompletionHandler(cmdb,
                                         completionSender,
                                         completion);
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.003f;
  color.clearColor.float32[1] = 0.008f;
  color.clearColor.float32[2] = 0.020f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "vrs-compare-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return GPU_ERROR_BACKEND_FAILURE;
  }

  leftWidth = state->width / 2u;
  draw_region(pass,
              state,
              state->finePipeline,
              0u,
              leftWidth,
              GPU_SHADING_RATE_1X1_EXT);
  draw_region(pass,
              state,
              state->coarsePipeline,
              leftWidth,
              state->width - leftWidth,
              state->coarseRate);
  GPUEndRenderPass(pass);

  result = GPUFinishFrame(state->queue, cmdb, frame);
  if (result == GPU_OK) {
    state->frameCount++;
  }
  return result;
}

void
GPUSampleVRSCompareDestroy(GPUSampleVRSCompare *state) {
  if (!state) {
    return;
  }

  GPUDestroyBuffer(state->vertexBuffer);
  GPUDestroyRenderPipeline(state->coarsePipeline);
  GPUDestroyRenderPipeline(state->finePipeline);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  memset(state, 0, sizeof(*state));
}
