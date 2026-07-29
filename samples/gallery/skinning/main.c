#include "../../common/sample_platform.h"
#include "../../common/SampleStats.h"

#define CGLM_FORCE_DEPTH_ZERO_TO_ONE
#include <cglm/cglm.h>

#include <math.h>

typedef struct SkinVertex {
  float    position[2];
  float    weights[2];
  float    color[4];
  uint16_t joints[2];
} SkinVertex;

typedef struct SkinUniformBlock {
  mat4 bones[4];
} SkinUniformBlock;

typedef struct WebGPUSkinning {
  GPUInstance       *instance;
  GPUAdapter        *adapter;
  GPUDevice         *device;
  GPUQueue          *queue;
  GPUSurface        *surface;
  GPUSwapchain      *swapchain;
  GPUShaderLibrary  *library;
  GPUShaderLayout   *shaderLayout;
  GPURenderPipeline *pipeline;
  GPUBindGroup      *skinGroup;
  GPUBuffer         *vertexBuffer;
  GPUBuffer         *indexBuffer;
  GPUBuffer         *uniformBuffer;
  WebGPURequest      request;
  uint32_t           width;
  uint32_t           height;
  uint32_t           uniformStride;
  uint32_t           frameCount;
  bool               verifyZeroAlloc;
} WebGPUSkinning;

enum {
  SKIN_BONE_COUNT   = 4u,
  SKIN_RING_COUNT   = 17u,
  SKIN_VERTEX_COUNT = SKIN_RING_COUNT * 2u,
  SKIN_INDEX_COUNT  = (SKIN_RING_COUNT - 1u) * 6u,
  FRAME_SLOT_COUNT  = 3u
};

_Static_assert(sizeof(SkinUniformBlock) == 256u,
               "four skin matrices must fill one WebGPU uniform alignment");

static WebGPUSkinning app;

static void
build_geometry(SkinVertex vertices[SKIN_VERTEX_COUNT],
               uint16_t   indices[SKIN_INDEX_COUNT]) {
  const float rootY = -0.72f, height = 1.44f;
  uint32_t    index;

  for (uint32_t ring = 0u; ring < SKIN_RING_COUNT; ring++) {
    float    bonePosition, t, y, width;
    uint32_t joint;

    t            = (float)ring / (float)(SKIN_RING_COUNT - 1u);
    y            = rootY + t * height;
    width        = 0.14f - t * 0.035f;
    bonePosition = t * (float)(SKIN_BONE_COUNT - 1u);
    joint        = (uint32_t)bonePosition;
    if (joint >= SKIN_BONE_COUNT - 1u) {
      joint        = SKIN_BONE_COUNT - 2u;
      bonePosition = (float)(SKIN_BONE_COUNT - 1u);
    }

    for (uint32_t side = 0u; side < 2u; side++) {
      SkinVertex *vertex;
      float       blend;

      vertex              = &vertices[ring * 2u + side];
      blend               = bonePosition - (float)joint;
      vertex->position[0] = side == 0u ? -width : width;
      vertex->position[1] = y;
      vertex->weights[0]  = 1.0f - blend;
      vertex->weights[1]  = blend;
      vertex->color[0]    = 1.0f - t * 0.72f;
      vertex->color[1]    = 0.24f + t * 0.58f;
      vertex->color[2]    = 0.10f + t * 0.86f;
      vertex->color[3]    = 1.0f;
      vertex->joints[0]   = (uint16_t)joint;
      vertex->joints[1]   = (uint16_t)(joint + 1u);
    }
  }

  index = 0u;
  for (uint32_t ring = 0u; ring < SKIN_RING_COUNT - 1u; ring++) {
    uint16_t lowerLeft, lowerRight, upperLeft, upperRight;

    lowerLeft  = (uint16_t)(ring * 2u);
    lowerRight = (uint16_t)(lowerLeft + 1u);
    upperLeft  = (uint16_t)(lowerLeft + 2u);
    upperRight = (uint16_t)(lowerLeft + 3u);
    indices[index++] = lowerLeft;
    indices[index++] = lowerRight;
    indices[index++] = upperLeft;
    indices[index++] = lowerRight;
    indices[index++] = upperRight;
    indices[index++] = upperLeft;
  }
}

static void
build_skin_uniforms(const WebGPUSkinning *state,
                    float                 seconds,
                    SkinUniformBlock     *uniforms) {
  static const float phases[SKIN_BONE_COUNT] = {
    0.0f, 0.7f, 1.4f, 2.1f
  };
  mat4  global[SKIN_BONE_COUNT];
  mat4  inverseBind, local, rotation, scale, skin;
  vec3  axisZ = {0.0f, 0.0f, 1.0f}, transform;
  const float rootY = -0.72f, boneLength = 0.48f;
  float aspectScale;

  aspectScale = 1.0f / gpu_sample_aspect_ratio(state->width, state->height);
  transform[0] = aspectScale * 0.88f;
  transform[1] = 0.88f;
  transform[2] = 1.0f;
  glm_scale_make(scale, transform);

  for (uint32_t bone = 0u; bone < SKIN_BONE_COUNT; bone++) {
    float angle, restY;

    angle = sinf(seconds * (0.85f + (float)bone * 0.13f) + phases[bone])
          * (0.15f + (float)bone * 0.09f);
    restY = rootY + (float)bone * boneLength;
    transform[0] = 0.0f;
    transform[1] = bone == 0u ? rootY : boneLength;
    transform[2] = 0.0f;
    glm_translate_make(local, transform);
    glm_rotate_make(rotation, angle, axisZ);
    glm_mat4_mul(local, rotation, local);
    if (bone == 0u) {
      glm_mat4_copy(local, global[bone]);
    } else {
      glm_mat4_mul(global[bone - 1u], local, global[bone]);
    }

    transform[0] = 0.0f;
    transform[1] = -restY;
    transform[2] = 0.0f;
    glm_translate_make(inverseBind, transform);
    glm_mat4_mul(global[bone], inverseBind, skin);
    glm_mat4_mul(scale, skin, uniforms->bones[bone]);
  }
}

static int
resize_canvas(WebGPUSkinning *state) {
  return resize_webgpu_canvas(state->swapchain,
                              &state->width,
                              &state->height);
}

static int
create_pipeline(WebGPUSkinning *state) {
  GPUVertexAttribute             attributes[4] = {0};
  GPUVertexBufferLayout          vertexLayout  = {0};
  GPUColorTargetState            color         = {0};
  GPURenderPipelineCreateInfo    info          = {0};
  const GPUBindGroupLayoutEntry *layoutEntries;
  void                          *artifact;
  uint64_t                       artifactSize;
  uint32_t                       layoutEntryCount;
  GPUResult                      result;

  artifact     = NULL;
  artifactSize = 0u;
  if (!read_file("/skinning.us", &artifact, &artifactSize)) {
    set_status("GPU: failed to read /skinning.us", 1);
    return 0;
  }
  result = GPUCreateShaderLibraryFromUSL(state->device,
                                         artifact,
                                         artifactSize,
                                         &state->library);
  free(artifact);
  if (result != GPU_OK || !state->library) {
    set_status("GPU: failed to compile the skinning artifact", 1);
    return 0;
  }
  if (GPUCreateShaderLayout(state->device,
                            state->library,
                            &state->shaderLayout) != GPU_OK ||
      !state->shaderLayout ||
      state->shaderLayout->bindGroupLayoutCount != 1u ||
      !state->shaderLayout->bindGroupLayouts ||
      !state->shaderLayout->bindGroupLayouts[0]) {
    set_status("GPU: unexpected skinning shader reflection", 1);
    return 0;
  }

  layoutEntries = GPUGetBindGroupLayoutEntries(
    state->shaderLayout->bindGroupLayouts[0],
    &layoutEntryCount
  );
  if (!layoutEntries || layoutEntryCount != 1u ||
      layoutEntries[0].binding != 0u ||
      layoutEntries[0].bindingType != GPU_BINDING_UNIFORM_BUFFER ||
      !layoutEntries[0].hasDynamicOffset) {
    set_status("GPU: USL skinning reflection mismatch", 1);
    return 0;
  }

  attributes[0].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[0].offset          = offsetof(SkinVertex, position);
  attributes[0].shaderLocation = 0u;
  attributes[1].format          = GPU_VERTEX_FORMAT_FLOAT32X2;
  attributes[1].offset          = offsetof(SkinVertex, weights);
  attributes[1].shaderLocation = 1u;
  attributes[2].format          = GPU_VERTEX_FORMAT_FLOAT32X4;
  attributes[2].offset          = offsetof(SkinVertex, color);
  attributes[2].shaderLocation = 2u;
  attributes[3].format          = GPU_VERTEX_FORMAT_UINT16X2;
  attributes[3].offset          = offsetof(SkinVertex, joints);
  attributes[3].shaderLocation = 3u;
  vertexLayout.pAttributes      = attributes;
  vertexLayout.strideBytes      = sizeof(SkinVertex);
  vertexLayout.stepMode         = GPU_VERTEX_STEP_MODE_VERTEX;
  vertexLayout.attributeCount   = GPU_ARRAY_LEN(attributes);

  color.format          = GPUGetSwapchainFormat(state->swapchain);
  color.blend.writeMask = GPU_COLOR_WRITE_ALL;

  info.chain.sType              = GPU_STRUCTURE_TYPE_RENDER_PIPELINE_CREATE_INFO;
  info.chain.structSize         = sizeof(info);
  info.label                    = "skinning-webgpu-usl-pipeline";
  info.layout                   = state->shaderLayout->pipelineLayout;
  info.library                  = state->library;
  info.vertexEntry              = "skin_vs";
  info.fragmentEntry            = "skin_fs";
  info.pColorTargets            = &color;
  info.vertex.pBufferLayouts    = &vertexLayout;
  info.vertex.bufferLayoutCount = 1u;
  info.colorTargetCount         = 1u;
  info.primitiveTopology        = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  info.cullMode                 = GPU_CULL_MODE_NONE;
  info.frontFace                = GPU_FRONT_FACE_CCW;
  info.multisample.sampleCount  = 1u;
  info.multisample.sampleMask   = UINT32_MAX;
  result = GPUCreateRenderPipeline(state->device, &info, &state->pipeline);
  if (result != GPU_OK || !state->pipeline) {
    set_status("GPU: failed to create the skinning pipeline", 1);
    return 0;
  }
  return 1;
}

static int
create_resources(WebGPUSkinning *state) {
  SkinVertex               vertices[SKIN_VERTEX_COUNT];
  uint16_t                 indices[SKIN_INDEX_COUNT];
  GPUDeviceCapabilities    capabilities;
  GPUBufferCreateInfo      bufferInfo = {0};
  GPUBindGroupEntry        groupEntry = {0};
  GPUBindGroupCreateInfo   groupInfo  = {0};
  uint64_t                 alignment, stride;

  build_geometry(vertices, indices);
  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "skinning-webgpu-vertices";
  bufferInfo.sizeBytes        = sizeof(vertices);
  bufferInfo.usage            = GPU_BUFFER_USAGE_VERTEX |
                                GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->vertexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->vertexBuffer,
                          0u,
                          vertices,
                          sizeof(vertices)) != GPU_OK) {
    set_status("GPU: failed to upload skinning vertices", 1);
    return 0;
  }

  bufferInfo.label     = "skinning-webgpu-indices";
  bufferInfo.sizeBytes = sizeof(indices);
  bufferInfo.usage     = GPU_BUFFER_USAGE_INDEX | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->indexBuffer) != GPU_OK ||
      GPUQueueWriteBuffer(state->queue,
                          state->indexBuffer,
                          0u,
                          indices,
                          sizeof(indices)) != GPU_OK) {
    set_status("GPU: failed to upload skinning indices", 1);
    return 0;
  }

  if (GPUGetDeviceCapabilities(state->device, &capabilities) != GPU_OK) {
    set_status("GPU: failed to query uniform alignment", 1);
    return 0;
  }
  alignment = capabilities.limits.minUniformBufferOffsetAlignment;
  if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
    set_status("GPU: invalid dynamic uniform alignment", 1);
    return 0;
  }
  stride = (sizeof(SkinUniformBlock) + alignment - 1u) & ~(alignment - 1u);
  if (stride > UINT32_MAX) {
    set_status("GPU: dynamic uniform alignment exceeds API offset range", 1);
    return 0;
  }
  state->uniformStride = (uint32_t)stride;

  bufferInfo.label     = "skinning-webgpu-uniform-ring";
  bufferInfo.sizeBytes = stride * FRAME_SLOT_COUNT;
  bufferInfo.usage     = GPU_BUFFER_USAGE_UNIFORM | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateBuffer(state->device,
                      &bufferInfo,
                      &state->uniformBuffer) != GPU_OK) {
    set_status("GPU: failed to create the skinning uniform ring", 1);
    return 0;
  }

  groupEntry.buffer.buffer = state->uniformBuffer;
  groupEntry.buffer.size   = sizeof(SkinUniformBlock);
  groupEntry.binding       = 0u;
  groupEntry.bindingType   = GPU_BINDING_UNIFORM_BUFFER;
  groupInfo.chain.sType      = GPU_STRUCTURE_TYPE_BIND_GROUP_CREATE_INFO;
  groupInfo.chain.structSize = sizeof(groupInfo);
  groupInfo.label             = "skinning-webgpu-group0";
  groupInfo.layout            = state->shaderLayout->bindGroupLayouts[0];
  groupInfo.pEntries          = &groupEntry;
  groupInfo.entryCount        = 1u;
  if (GPUCreateBindGroup(state->device,
                         &groupInfo,
                         &state->skinGroup) != GPU_OK ||
      !state->skinGroup) {
    set_status("GPU: failed to create the skinning bind group", 1);
    return 0;
  }
  return 1;
}

static int
update_skin(WebGPUSkinning *state, uint32_t *outDynamicOffset) {
  SkinUniformBlock uniforms;
  uint32_t         frameSlot, offset;
  float            seconds;

  frameSlot = state->frameCount % FRAME_SLOT_COUNT;
  offset    = frameSlot * state->uniformStride;
  seconds   = (float)(emscripten_get_now() * 0.001);
  build_skin_uniforms(state, seconds, &uniforms);
  if (GPUQueueWriteBuffer(state->queue,
                          state->uniformBuffer,
                          offset,
                          &uniforms,
                          sizeof(uniforms)) != GPU_OK) {
    return 0;
  }
  *outDynamicOffset = offset;
  return 1;
}

static void
render_frame(void *userData) {
  WebGPUSkinning               *state;
  GPUFrame                     *frame;
  GPUCommandBuffer             *cmdb;
  GPURenderPassEncoder         *pass;
  GPUBufferBinding              vertexBuffer = {0};
  GPURenderPassColorAttachment  color        = {0};
  GPURenderPassCreateInfo       passInfo     = {0};
  uint32_t                      dynamicOffset;

  state = userData;
  if (!resize_canvas(state) || !update_skin(state, &dynamicOffset)) {
    set_status("GPU: failed to update the skinning frame", 1);
    emscripten_cancel_main_loop();
    return;
  }

  frame = GPUBeginFrame(state->swapchain);
  if (!frame) {
    return;
  }
  cmdb = NULL;
  if (GPUAcquireCommandBuffer(state->queue,
                              "skinning-webgpu-frame",
                              &cmdb) != GPU_OK || !cmdb) {
    GPUEndFrame(frame);
    return;
  }

  color.view                  = GPUFrameGetTargetView(frame);
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = 0.008f;
  color.clearColor.float32[1] = 0.018f;
  color.clearColor.float32[2] = 0.048f;
  color.clearColor.float32[3] = 1.0f;
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = "skinning-webgpu-pass";
  passInfo.pColorAttachments    = &color;
  passInfo.colorAttachmentCount = 1u;
  pass = GPUBeginRenderPass(cmdb, &passInfo);
  if (!pass) {
    (void)GPUDiscardCommandBuffer(cmdb);
    GPUEndFrame(frame);
    return;
  }

  vertexBuffer.buffer = state->vertexBuffer;
  GPUBindRenderPipeline(pass, state->pipeline);
  GPUBindRenderGroup(pass,
                     0u,
                     state->skinGroup,
                     1u,
                     &dynamicOffset);
  GPUBindVertexBuffers(pass, 0u, 1u, &vertexBuffer);
  GPUBindIndexBuffer(pass, state->indexBuffer, 0u, GPU_INDEX_TYPE_UINT16);
  GPUDrawIndexed(pass, SKIN_INDEX_COUNT, 1u, 0u, 0, 0u);
  GPUEndRenderPass(pass);

  if (GPUFinishFrame(state->queue, cmdb, frame) != GPU_OK) {
    set_status("GPU: failed to finish the skinning frame", 1);
  } else {
    state->frameCount++;
    if (!GPUSampleCheckZeroAlloc(state->device,
                                 state->frameCount,
                                 state->verifyZeroAlloc,
                                 "skinning")) {
      set_status("GPU: warm skinning frame allocated wrapper memory", 1);
      emscripten_cancel_main_loop();
    }
  }
}

static void
webgpu_ready(GPUResult  result,
             GPUAdapter *adapter,
             GPUDevice  *device,
             void       *userData) {
  WebGPUSkinning  *state;
  GPURuntimeConfig runtime = {0};

  state = userData;
  if (result != GPU_OK || !adapter || !device) {
    set_status(!adapter ? "GPU: failed to request WebGPU adapter"
                        : "GPU: failed to request WebGPU device",
               1);
    return;
  }

  state->adapter = adapter;
  state->device  = device;
  state->queue   = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  state->verifyZeroAlloc = GPUSampleEnvEnabled("GPU_SAMPLE_ASSERT_ZERO_ALLOC");
  runtime.chain.sType      = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtime.chain.structSize = sizeof(runtime);
  runtime.validationMode   = GPU_VALIDATION_FULL;
  runtime.enableStats      = true;
  if (GPUConfigureRuntime(device, &runtime) != GPU_OK) {
    set_status("GPU: failed to configure WebGPU runtime stats", 1);
    return;
  }

  state->surface = GPUCreateSurfaceFromNative(state->instance,
                                               state->adapter,
                                               (void *)"#canvas",
                                               GPU_SURFACE_WEB_CANVAS,
                                               1.0f);
  if (!state->queue || !state->surface || !resize_canvas(state)) {
    set_status("GPU: failed to create WebGPU queue or canvas surface", 1);
    return;
  }

  state->swapchain = GPUCreateSwapchainDefault(device,
                                                state->surface,
                                                state->width,
                                                state->height);
  if (!state->swapchain || !create_pipeline(state) ||
      !create_resources(state)) {
    set_status("GPU: failed to initialize skinning resources", 1);
    return;
  }

  set_status("GPU: WebGPU USL GPU skinning ready", 0);
  emscripten_set_main_loop_arg(render_frame, state, 0, true);
}

int
main(void) {
  GPUInstanceCreateInfo info = {0};
  GPUResult             result;

  info.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  info.chain.structSize = sizeof(info);
  info.label            = "skinning-webgpu-usl";
  info.preferredBackend = GPU_BACKEND_WEBGPU;
  info.enableValidation = true;
  result = GPUCreateInstance(&info, &app.instance);
  if (result != GPU_OK || !app.instance) {
    set_status("GPU: failed to create WebGPU instance", 1);
    return 1;
  }

  set_status("GPU: requesting WebGPU device", 0);
  result = request_webgpu_device(app.instance,
                                 &app.request,
                                 webgpu_ready,
                                 &app);
  return result == GPU_OK ? 0 : 1;
}
