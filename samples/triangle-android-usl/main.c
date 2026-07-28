#include "../common/android.h"
#include "../common/android_samples.h"

typedef struct TriangleRenderer {
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
} TriangleRenderer;

static bool
triangle_create(GPUAndroidSample *sample, void *userData) {
  TriangleRenderer            *state;
  GPUColorTargetState          colorTarget = {0};
  GPURenderPipelineCreateInfo  pipelineInfo = {0};

  state = userData;
  if (!GPUSampleAndroidLoadUSL(sample, "triangle.us", &state->library) ||
      GPUCreateShaderLayout(sample->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 0u ||
      !state->shaderLayout->pipelineLayout) {
    return false;
  }

  colorTarget.format          = GPUGetSwapchainFormat(sample->swapchain);
  colorTarget.blend.writeMask = GPU_COLOR_WRITE_ALL;

  pipelineInfo.chain.sType       =
    GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  pipelineInfo.chain.structSize  = sizeof(pipelineInfo);
  pipelineInfo.label             = "android-usl-triangle";
  pipelineInfo.layout            = state->shaderLayout->pipelineLayout;
  pipelineInfo.library           = state->library;
  pipelineInfo.vertexEntry       = "tri_vs";
  pipelineInfo.fragmentEntry     = "tri_fs";
  pipelineInfo.pColorTargets     = &colorTarget;
  pipelineInfo.colorTargetCount  = 1u;
  pipelineInfo.primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  pipelineInfo.cullMode          = GPU_CULL_MODE_NONE;
  pipelineInfo.frontFace         = GPU_FRONT_FACE_CCW;
  pipelineInfo.multisample.sampleCount = 1u;
  pipelineInfo.multisample.sampleMask  = UINT32_MAX;
  return GPUCreateRenderPipeline(sample->device,
                                 &pipelineInfo,
                                 &state->pipeline) == GPU_OK &&
         state->pipeline;
}

static bool
triangle_render(GPUAndroidSample *sample, void *userData) {
  TriangleRenderer              *state;
  GPUFrame                      *frame;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *pass;
  GPURenderPassColorAttachment   color    = {0};
  GPURenderPassCreateInfo        passInfo = {0};

  state = userData;
  frame = GPUBeginFrame(sample->swapchain);
  if (!frame) {
    return true;
  }

  cmdb = NULL;
  if (GPUAcquireCommandBuffer(sample->queue,
                              "android-usl-frame",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    GPUEndFrame(frame);
    return false;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.015f;
  color.clearColor.float32[1] = 0.02f;
  color.clearColor.float32[2] = 0.035f;
  color.clearColor.float32[3] = 1.0f;

  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "android-usl-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return false;
  }

  GPUBindRenderPipeline(pass, state->pipeline);
  GPUDraw(pass, 3u, 1u, 0u, 0u);
  GPUEndRenderPass(pass);
  return GPUFinishFrame(sample->queue, cmdb, frame) == GPU_OK;
}

static void
triangle_destroy(GPUAndroidSample *sample, void *userData) {
  TriangleRenderer *state;

  (void)sample;
  state = userData;
  GPUDestroyRenderPipeline(state->pipeline);
  GPUDestroyShaderLayout(state->shaderLayout);
  GPUDestroyShaderLibrary(state->library);
  state->pipeline     = NULL;
  state->shaderLayout = NULL;
  state->library      = NULL;
}

const GPUAndroidSampleDefinition*
GPUSampleAndroidTriangle(void) {
  static const GPUAndroidSampleCallbacks callbacks = {
    .create  = triangle_create,
    .render  = triangle_render,
    .destroy = triangle_destroy
  };
  static const GPUAndroidSampleDefinition definition = {
    .callbacks    = &callbacks,
    .id           = "triangle",
    .name         = "GPU + USL Triangle",
    .userDataSize = sizeof(TriangleRenderer)
  };

  return &definition;
}

#if GPU_ANDROID_SAMPLE_STANDALONE
void
android_main(struct android_app *app) {
  GPUSampleAndroidRunDefinition(app, GPUSampleAndroidTriangle());
}
#endif
