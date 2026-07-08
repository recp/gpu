#include "test.h"

static int
check_render_pass_validation(void) {
  GPUCommandBuffer fakeCmdb = {0};
  GPUTextureView fakeView = {0};
  GPURenderPassColorAttachment colors[9] = {0};
  GPURenderPassDepthStencilAttachment depthStencil = {0};
  GPURenderPassCreateInfo rp = {0};

  if (GPUBeginRenderPass(NULL, &rp) ||
      GPUBeginRenderPass(&fakeCmdb, NULL)) {
    fprintf(stderr, "render pass accepted null input\n");
    return 0;
  }

  rp.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  rp.chain.structSize = sizeof(rp);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted wrong sType\n");
    return 0;
  }

  rp.chain.sType = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp.chain.structSize = (uint32_t)(sizeof(rp) - 1u);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted short structSize\n");
    return 0;
  }

  rp.chain.structSize = sizeof(rp);
  rp.colorAttachmentCount = 1u;
  rp.pColorAttachments = NULL;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted missing color attachments\n");
    return 0;
  }

  rp.pColorAttachments = colors;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted null color view\n");
    return 0;
  }

  colors[0].view = &fakeView;
  colors[0].loadOp = (GPULoadOp)99;
  colors[0].storeOp = GPU_STORE_OP_STORE;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid color load op\n");
    return 0;
  }

  colors[0].loadOp = GPU_LOAD_OP_CLEAR;
  colors[0].storeOp = (GPUStoreOp)99;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid color store op\n");
    return 0;
  }

  colors[0].storeOp = GPU_STORE_OP_STORE;
  rp.colorAttachmentCount = (uint32_t)GPU_ARRAY_LEN(colors);
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted too many color attachments\n");
    return 0;
  }

  rp.colorAttachmentCount = 0u;
  rp.pColorAttachments = NULL;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted no attachments\n");
    return 0;
  }

  rp.pDepthStencilAttachment = &depthStencil;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted null depth-stencil view\n");
    return 0;
  }

  depthStencil.view = &fakeView;
  depthStencil.depthLoadOp = (GPULoadOp)99;
  depthStencil.depthStoreOp = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp = GPU_LOAD_OP_DONT_CARE;
  depthStencil.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted invalid depth load op\n");
    return 0;
  }

  depthStencil.depthLoadOp = GPU_LOAD_OP_CLEAR;
  fakeCmdb._submitted = true;
  if (GPUBeginRenderPass(&fakeCmdb, &rp)) {
    fprintf(stderr, "render pass accepted submitted command buffer\n");
    return 0;
  }

  return 1;
}

static int
check_render_encoder_validation(void) {
  GPURenderPassEncoder pass = {0};
  GPURenderPassEncoder endedPass = {0};
  GPUBuffer *fakeBuffer = (GPUBuffer *)(uintptr_t)1u;
  GPUBufferBinding binding = {0};
  GPUDynamicStateApplyInfo dynamicState = {0};

  GPUBindRenderPipeline(NULL, NULL);
  GPUBindVertexBuffers(NULL, 0u, 0u, NULL);
  GPUBindIndexBuffer(NULL, fakeBuffer, 0u, GPUIndexTypeUInt16);
  GPUDraw(NULL, 1u, 1u, 0u, 0u);
  GPUDrawIndexed(NULL, 1u, 1u, 0u, 0, 0u);
  GPUApplyDynamicState(NULL, &dynamicState);
  GPUApplyDynamicState(&pass, NULL);
  GPUDraw(&pass, 1u, 1u, 0u, 0u);

  binding.buffer = fakeBuffer;
  GPUBindVertexBuffers(&pass, UINT32_MAX, 2u, &binding);
  GPUBindIndexBuffer(&pass, fakeBuffer, 0u, (GPUIndexType)99);
  if (pass._hasIndexBuffer) {
    fprintf(stderr, "render encoder accepted invalid index type\n");
    return 0;
  }

  GPUBindIndexBuffer(&pass, fakeBuffer, 0u, GPUIndexTypeUInt16);
  if (!pass._hasIndexBuffer ||
      pass._indexBuffer != fakeBuffer ||
      pass._indexType != GPUIndexTypeUInt16) {
    fprintf(stderr, "render encoder rejected valid index binding\n");
    return 0;
  }

  dynamicState.chain.sType = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  dynamicState.chain.structSize = sizeof(dynamicState);
  GPUApplyDynamicState(&pass, &dynamicState);

  dynamicState.chain.sType = GPU_STRUCTURE_TYPE_DYNAMIC_STATE_APPLY_INFO;
  dynamicState.chain.structSize = (uint32_t)(sizeof(dynamicState) - 1u);
  GPUApplyDynamicState(&pass, &dynamicState);

  endedPass._ended = true;
  GPUBindIndexBuffer(&endedPass, fakeBuffer, 0u, GPUIndexTypeUInt16);
  if (endedPass._hasIndexBuffer) {
    fprintf(stderr, "render encoder accepted index binding after end\n");
    return 0;
  }
  GPUBindVertexBuffers(&endedPass, 0u, 1u, &binding);
  GPUSetViewport(&endedPass, &dynamicState.viewport);
  GPUDraw(&endedPass, 1u, 1u, 0u, 0u);
  GPUDrawIndexed(&endedPass, 1u, 1u, 0u, 0, 0u);
  GPUApplyDynamicState(&endedPass, &dynamicState);
  GPUEndRenderPass(&endedPass);

  return 1;
}

int
gpu_test_render(void) {
  return check_render_pass_validation() && check_render_encoder_validation();
}
