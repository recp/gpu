#include "test.h"
#include "../../src/api/device_internal.h"

enum {
  VIEW_TARGET_WIDTH         = 8u,
  VIEW_TARGET_HEIGHT        = 8u,
  VIEW_TARGET_LAYERS        = 3u,
  VIEW_TARGET_MIPS          = 3u,
  VIEW_FIRST_MIP            = 1u,
  VIEW_FIRST_LAYER          = 1u,
  VIEW_FIRST_WIDTH          = 4u,
  VIEW_FIRST_HEIGHT         = 4u,
  VIEW_SECOND_MIP           = 2u,
  VIEW_SECOND_LAYER         = 2u,
  VIEW_SECOND_WIDTH         = 2u,
  VIEW_SECOND_HEIGHT        = 2u,
  VIEW_ROW_PITCH            = 256u,
  VIEW_SECOND_BUFFER_OFFSET = VIEW_ROW_PITCH * VIEW_FIRST_HEIGHT,
  VIEW_READBACK_BYTES       = VIEW_SECOND_BUFFER_OFFSET +
                              VIEW_ROW_PITCH * VIEW_SECOND_HEIGHT
};

static bool
view_render_submit(GPUDevice        *device,
                   GPUCommandQueue  *queue,
                   GPUCommandBuffer *cmdb) {
  GPUCommandBuffer  *buffers[1];
  GPUQueueSubmitInfo submitInfo = {0};
  GPUFence          *fence;
  GPUResult          submitResult;
  GPUResult          waitResult;

  fence = NULL;
  if (GPUCreateFence(device, NULL, &fence) != GPU_OK || !fence) {
    return false;
  }

  buffers[0]                    = cmdb;
  submitInfo.chain.sType        = GPU_STRUCTURE_TYPE_QUEUE_SUBMIT_INFO;
  submitInfo.chain.structSize   = sizeof(submitInfo);
  submitInfo.commandBufferCount = 1u;
  submitInfo.ppCommandBuffers   = buffers;
  submitInfo.fence              = fence;
  submitResult                  = GPUQueueSubmit(queue, &submitInfo);
  waitResult                    = submitResult == GPU_OK
                                    ? GPUWaitFence(fence, UINT64_MAX)
                                    : GPU_ERROR_BACKEND_FAILURE;
  if (submitResult != GPU_OK || waitResult != GPU_OK) {
    fprintf(stderr,
            "texture view render submit failed: submit=%d wait=%d\n",
            submitResult,
            waitResult);
  }
  GPUDestroyFence(fence);
  return submitResult == GPU_OK && waitResult == GPU_OK;
}

static bool
view_render_pixels_equal(const uint8_t *pixels,
                         uint64_t       offset,
                         uint32_t       width,
                         uint32_t       height,
                         uint8_t        red,
                         uint8_t        green,
                         uint8_t        blue,
                         uint8_t        alpha) {
  for (uint32_t y = 0u; y < height; y++) {
    const uint8_t *row;

    row = pixels + offset + (uint64_t)y * VIEW_ROW_PITCH;
    for (uint32_t x = 0u; x < width; x++) {
      const uint8_t *pixel = row + x * 4u;

      if (pixel[0] != red || pixel[1] != green ||
          pixel[2] != blue || pixel[3] != alpha) {
        return false;
      }
    }
  }
  return true;
}

static bool
view_render_depths_equal(const uint8_t *pixels,
                         uint64_t       offset,
                         uint32_t       width,
                         uint32_t       height,
                         float          expected) {
  for (uint32_t y = 0u; y < height; y++) {
    const uint8_t *row;

    row = pixels + offset + (uint64_t)y * VIEW_ROW_PITCH;
    for (uint32_t x = 0u; x < width; x++) {
      float depth;

      memcpy(&depth, row + x * sizeof(depth), sizeof(depth));
      if (depth != expected) {
        return false;
      }
    }
  }
  return true;
}

static GPURenderPassEncoder *
view_render_begin_clear(GPUCommandBuffer *cmdb,
                        GPUTextureView   *view,
                        const float       clearColor[4],
                        const char       *label) {
  GPURenderPassColorAttachment color = {0};
  GPURenderPassCreateInfo      passInfo = {0};

  color.view                  = view;
  color.loadOp                = GPU_LOAD_OP_CLEAR;
  color.storeOp               = GPU_STORE_OP_STORE;
  color.clearColor.float32[0] = clearColor[0];
  color.clearColor.float32[1] = clearColor[1];
  color.clearColor.float32[2] = clearColor[2];
  color.clearColor.float32[3] = clearColor[3];
  passInfo.chain.sType          = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize     = sizeof(passInfo);
  passInfo.label                = label;
  passInfo.colorAttachmentCount = 1u;
  passInfo.pColorAttachments    = &color;
  return GPUBeginRenderPass(cmdb, &passInfo);
}

static GPURenderPassEncoder *
view_render_begin_depth_clear(GPUCommandBuffer *cmdb,
                              GPUTextureView   *view,
                              float             clearDepth,
                              const char       *label) {
  GPURenderPassDepthStencilAttachment depth = {0};
  GPURenderPassCreateInfo              passInfo = {0};

  depth.view                              = view;
  depth.depthLoadOp                       = GPU_LOAD_OP_CLEAR;
  depth.depthStoreOp                      = GPU_STORE_OP_STORE;
  depth.stencilLoadOp                     = GPU_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp                    = GPU_STORE_OP_DONT_CARE;
  depth.clearDepth                        = clearDepth;
  passInfo.chain.sType                    = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize               = sizeof(passInfo);
  passInfo.label                          = label;
  passInfo.pDepthStencilAttachment        = &depth;
  return GPUBeginRenderPass(cmdb, &passInfo);
}

static GPURenderPassEncoder *
view_render_begin_ds_clear(GPUCommandBuffer *cmdb,
                           GPUTextureView   *view,
                           float             clearDepth,
                           uint32_t          clearStencil,
                           const char       *label) {
  GPURenderPassDepthStencilAttachment depthStencil = {0};
  GPURenderPassCreateInfo              passInfo     = {0};

  depthStencil.view                       = view;
  depthStencil.depthLoadOp                = GPU_LOAD_OP_CLEAR;
  depthStencil.depthStoreOp               = GPU_STORE_OP_STORE;
  depthStencil.stencilLoadOp              = GPU_LOAD_OP_CLEAR;
  depthStencil.stencilStoreOp             = GPU_STORE_OP_STORE;
  depthStencil.clearDepth                 = clearDepth;
  depthStencil.clearStencil               = clearStencil;
  passInfo.chain.sType                    = GPU_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  passInfo.chain.structSize               = sizeof(passInfo);
  passInfo.label                          = label;
  passInfo.pDepthStencilAttachment        = &depthStencil;
  return GPUBeginRenderPass(cmdb, &passInfo);
}

int
gpu_test_texture_view_render(GPUDevice *device) {
  static const float firstClear[4]  = {1.0f, 0.0f, 0.0f, 1.0f};
  static const float secondClear[4] = {0.0f, 1.0f, 0.0f, 1.0f};
  GPUCommandQueue               *queue;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *renderPass;
  GPUCopyPassEncoder            *copyPass;
  GPUTexture                    *texture;
  GPUTextureView                *firstView;
  GPUTextureView                *secondView;
  GPUBuffer                     *readback;
  GPUTextureCreateInfo           textureInfo = {0};
  GPUTextureViewCreateInfo       viewInfo = {0};
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUBufferTextureCopyRegion     copyRegion = {0};
  GPUTextureBarrier              textureBarriers[2] = {{0}};
  GPUBarrierBatch                barrierBatch = {0};
  uint8_t                        pixels[VIEW_READBACK_BYTES] = {0};
  bool                           firstMatches;
  bool                           secondMatches;
  int                            ok;

  queue      = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  cmdb       = NULL;
  renderPass = NULL;
  copyPass   = NULL;
  texture    = NULL;
  firstView  = NULL;
  secondView = NULL;
  readback   = NULL;
  ok         = queue != NULL;
  if (!ok) {
    fprintf(stderr, "texture view render has no graphics queue\n");
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "texture-view-render-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width            = VIEW_TARGET_WIDTH;
  textureInfo.height           = VIEW_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = VIEW_TARGET_LAYERS;
  textureInfo.mipLevelCount    = VIEW_TARGET_MIPS;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_COLOR_TARGET |
                                 GPU_TEXTURE_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "texture view render texture setup failed\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  viewInfo.format           = GPU_FORMAT_RGBA8_UNORM;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  viewInfo.label            = "texture-view-render-first";
  viewInfo.baseMipLevel     = VIEW_FIRST_MIP;
  viewInfo.baseArrayLayer   = VIEW_FIRST_LAYER;
  ok = GPUCreateTextureView(texture, &viewInfo, &firstView) == GPU_OK &&
       firstView;
  viewInfo.label          = "texture-view-render-second";
  viewInfo.baseMipLevel   = VIEW_SECOND_MIP;
  viewInfo.baseArrayLayer = VIEW_SECOND_LAYER;
  ok = ok && GPUCreateTextureView(texture, &viewInfo, &secondView) == GPU_OK &&
       secondView;
  if (!ok) {
    fprintf(stderr, "texture view render view setup failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "texture-view-render-readback";
  bufferInfo.sizeBytes        = VIEW_READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "texture view render readback setup failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "texture-view-render", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "texture view render command buffer failed\n");
    goto cleanup;
  }

  renderPass = view_render_begin_clear(cmdb,
                                       firstView,
                                       firstClear,
                                       "texture-view-render-first");
  if (!renderPass) {
    fprintf(stderr, "texture view first render pass failed\n");
    goto cleanup;
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  renderPass = view_render_begin_clear(cmdb,
                                       secondView,
                                       secondClear,
                                       "texture-view-render-second");
  if (!renderPass) {
    fprintf(stderr, "texture view second render pass failed\n");
    goto cleanup;
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarriers[0].texture    = texture;
  textureBarriers[0].srcAccess  = GPU_ACCESS_COLOR_WRITE;
  textureBarriers[0].dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarriers[0].baseMip    = VIEW_FIRST_MIP;
  textureBarriers[0].mipCount   = 1u;
  textureBarriers[0].baseLayer  = VIEW_FIRST_LAYER;
  textureBarriers[0].layerCount = 1u;
  textureBarriers[1]            = textureBarriers[0];
  textureBarriers[1].baseMip    = VIEW_SECOND_MIP;
  textureBarriers[1].baseLayer  = VIEW_SECOND_LAYER;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 2u;
  barrierBatch.pTextureBarriers    = textureBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "texture-view-render-readback");
  if (!copyPass) {
    fprintf(stderr, "texture view render copy pass failed\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow                    = VIEW_ROW_PITCH;
  copyRegion.rowsPerImage                   = VIEW_FIRST_HEIGHT;
  copyRegion.texture.texture.mipLevel       = VIEW_FIRST_MIP;
  copyRegion.texture.texture.baseArrayLayer = VIEW_FIRST_LAYER;
  copyRegion.texture.width                  = VIEW_FIRST_WIDTH;
  copyRegion.texture.height                 = VIEW_FIRST_HEIGHT;
  copyRegion.texture.depth                  = 1u;
  copyRegion.texture.layerCount             = 1u;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);

  copyRegion.bufferOffset                   = VIEW_SECOND_BUFFER_OFFSET;
  copyRegion.rowsPerImage                   = VIEW_SECOND_HEIGHT;
  copyRegion.texture.texture.mipLevel       = VIEW_SECOND_MIP;
  copyRegion.texture.texture.baseArrayLayer = VIEW_SECOND_LAYER;
  copyRegion.texture.width                  = VIEW_SECOND_WIDTH;
  copyRegion.texture.height                 = VIEW_SECOND_HEIGHT;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  ok   = view_render_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok || GPUQueueReadBuffer(queue,
                                readback,
                                0u,
                                pixels,
                                sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "texture view render readback failed\n");
    ok = 0;
    goto cleanup;
  }

  firstMatches = view_render_pixels_equal(pixels,
                                           0u,
                                           VIEW_FIRST_WIDTH,
                                           VIEW_FIRST_HEIGHT,
                                           255u,
                                           0u,
                                           0u,
                                           255u);
  secondMatches = view_render_pixels_equal(pixels,
                                            VIEW_SECOND_BUFFER_OFFSET,
                                            VIEW_SECOND_WIDTH,
                                            VIEW_SECOND_HEIGHT,
                                            0u,
                                            255u,
                                            0u,
                                            255u);
  if (!firstMatches || !secondMatches) {
    fprintf(stderr,
            "texture view render mismatch: first=%u,%u,%u,%u "
            "second=%u,%u,%u,%u\n",
            (unsigned)pixels[0],
            (unsigned)pixels[1],
            (unsigned)pixels[2],
            (unsigned)pixels[3],
            (unsigned)pixels[VIEW_SECOND_BUFFER_OFFSET],
            (unsigned)pixels[VIEW_SECOND_BUFFER_OFFSET + 1u],
            (unsigned)pixels[VIEW_SECOND_BUFFER_OFFSET + 2u],
            (unsigned)pixels[VIEW_SECOND_BUFFER_OFFSET + 3u]);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyBuffer(readback);
  GPUDestroyTextureView(secondView);
  GPUDestroyTextureView(firstView);
  GPUDestroyTexture(texture);
  return ok;
}

int
gpu_test_texture_view_depth(GPUDevice *device) {
  GPUCommandQueue               *queue;
  GPUCommandBuffer              *cmdb;
  GPURenderPassEncoder          *renderPass;
  GPUCopyPassEncoder            *copyPass;
  GPUTexture                    *texture;
  GPUTextureView                *firstView;
  GPUTextureView                *secondView;
  GPUBuffer                     *readback;
  GPUTextureCreateInfo           textureInfo = {0};
  GPUTextureViewCreateInfo       viewInfo = {0};
  GPUBufferCreateInfo            bufferInfo = {0};
  GPUBufferTextureCopyRegion     copyRegion = {0};
  GPUTextureBarrier              textureBarriers[2] = {{0}};
  GPUBarrierBatch                barrierBatch = {0};
  uint8_t                        pixels[VIEW_READBACK_BYTES] = {0};
  bool                           firstMatches;
  bool                           secondMatches;
  int                            ok;

  queue      = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  cmdb       = NULL;
  renderPass = NULL;
  copyPass   = NULL;
  texture    = NULL;
  firstView  = NULL;
  secondView = NULL;
  readback   = NULL;
  ok         = queue != NULL;
  if (!ok) {
    fprintf(stderr, "texture view depth has no graphics queue\n");
    return 0;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "texture-view-depth-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  textureInfo.width            = VIEW_TARGET_WIDTH;
  textureInfo.height           = VIEW_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = VIEW_TARGET_LAYERS;
  textureInfo.mipLevelCount    = VIEW_TARGET_MIPS;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL |
                                 GPU_TEXTURE_USAGE_COPY_SRC;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "texture view depth texture setup failed\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  viewInfo.format           = GPU_FORMAT_DEPTH32_FLOAT;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.arrayLayerCount  = 1u;
  viewInfo.label            = "texture-view-depth-first";
  viewInfo.baseMipLevel     = VIEW_FIRST_MIP;
  viewInfo.baseArrayLayer   = VIEW_FIRST_LAYER;
  ok = GPUCreateTextureView(texture, &viewInfo, &firstView) == GPU_OK &&
       firstView;
  viewInfo.label          = "texture-view-depth-second";
  viewInfo.baseMipLevel   = VIEW_SECOND_MIP;
  viewInfo.baseArrayLayer = VIEW_SECOND_LAYER;
  ok = ok && GPUCreateTextureView(texture, &viewInfo, &secondView) == GPU_OK &&
       secondView;
  if (!ok) {
    fprintf(stderr, "texture view depth view setup failed\n");
    goto cleanup;
  }

  bufferInfo.chain.sType      = GPU_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.chain.structSize = sizeof(bufferInfo);
  bufferInfo.label            = "texture-view-depth-readback";
  bufferInfo.sizeBytes        = VIEW_READBACK_BYTES;
  bufferInfo.usage            = GPU_BUFFER_USAGE_COPY_DST |
                                GPU_BUFFER_USAGE_COPY_SRC;
  if (GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK || !readback) {
    fprintf(stderr, "texture view depth readback setup failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue, "texture-view-depth", &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "texture view depth command buffer failed\n");
    goto cleanup;
  }

  renderPass = view_render_begin_depth_clear(cmdb,
                                             firstView,
                                             0.25f,
                                             "texture-view-depth-first");
  if (!renderPass) {
    fprintf(stderr, "texture view first depth pass failed\n");
    goto cleanup;
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  renderPass = view_render_begin_depth_clear(cmdb,
                                             secondView,
                                             0.75f,
                                             "texture-view-depth-second");
  if (!renderPass) {
    fprintf(stderr, "texture view second depth pass failed\n");
    goto cleanup;
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  textureBarriers[0].texture    = texture;
  textureBarriers[0].srcAccess  = GPU_ACCESS_DEPTH_WRITE;
  textureBarriers[0].dstAccess  = GPU_ACCESS_TRANSFER_READ;
  textureBarriers[0].baseMip    = VIEW_FIRST_MIP;
  textureBarriers[0].mipCount   = 1u;
  textureBarriers[0].baseLayer  = VIEW_FIRST_LAYER;
  textureBarriers[0].layerCount = 1u;
  textureBarriers[1]            = textureBarriers[0];
  textureBarriers[1].baseMip    = VIEW_SECOND_MIP;
  textureBarriers[1].baseLayer  = VIEW_SECOND_LAYER;
  barrierBatch.srcStages           = GPU_STAGE_FRAGMENT;
  barrierBatch.dstStages           = GPU_STAGE_TRANSFER;
  barrierBatch.textureBarrierCount = 2u;
  barrierBatch.pTextureBarriers    = textureBarriers;
  GPUEncodeBarriers(cmdb, &barrierBatch);

  copyPass = GPUBeginCopyPass(cmdb, "texture-view-depth-readback");
  if (!copyPass) {
    fprintf(stderr, "texture view depth copy pass failed\n");
    goto cleanup;
  }
  copyRegion.bytesPerRow                    = VIEW_ROW_PITCH;
  copyRegion.rowsPerImage                   = VIEW_FIRST_HEIGHT;
  copyRegion.texture.texture.mipLevel       = VIEW_FIRST_MIP;
  copyRegion.texture.texture.baseArrayLayer = VIEW_FIRST_LAYER;
  copyRegion.texture.width                  = VIEW_FIRST_WIDTH;
  copyRegion.texture.height                 = VIEW_FIRST_HEIGHT;
  copyRegion.texture.depth                  = 1u;
  copyRegion.texture.layerCount             = 1u;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);

  copyRegion.bufferOffset                   = VIEW_SECOND_BUFFER_OFFSET;
  copyRegion.rowsPerImage                   = VIEW_SECOND_HEIGHT;
  copyRegion.texture.texture.mipLevel       = VIEW_SECOND_MIP;
  copyRegion.texture.texture.baseArrayLayer = VIEW_SECOND_LAYER;
  copyRegion.texture.width                  = VIEW_SECOND_WIDTH;
  copyRegion.texture.height                 = VIEW_SECOND_HEIGHT;
  GPUCopyTextureToBuffer(copyPass, texture, readback, &copyRegion);
  GPUEndCopyPass(copyPass);
  copyPass = NULL;

  ok   = view_render_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok || GPUQueueReadBuffer(queue,
                                readback,
                                0u,
                                pixels,
                                sizeof(pixels)) != GPU_OK) {
    fprintf(stderr, "texture view depth readback failed\n");
    ok = 0;
    goto cleanup;
  }

  firstMatches = view_render_depths_equal(pixels,
                                          0u,
                                          VIEW_FIRST_WIDTH,
                                          VIEW_FIRST_HEIGHT,
                                          0.25f);
  secondMatches = view_render_depths_equal(pixels,
                                           VIEW_SECOND_BUFFER_OFFSET,
                                           VIEW_SECOND_WIDTH,
                                           VIEW_SECOND_HEIGHT,
                                           0.75f);
  if (!firstMatches || !secondMatches) {
    float firstDepth;
    float secondDepth;

    memcpy(&firstDepth, pixels, sizeof(firstDepth));
    memcpy(&secondDepth,
           pixels + VIEW_SECOND_BUFFER_OFFSET,
           sizeof(secondDepth));
    fprintf(stderr,
            "texture view depth mismatch: first=%f second=%f\n",
            firstDepth,
            secondDepth);
    ok = 0;
  }

cleanup:
  if (copyPass) {
    GPUEndCopyPass(copyPass);
  }
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyBuffer(readback);
  GPUDestroyTextureView(secondView);
  GPUDestroyTextureView(firstView);
  GPUDestroyTexture(texture);
  return ok;
}

int
gpu_test_texture_view_depth_stencil(GPUDevice *device) {
  static const GPUFormat formats[] = {
    GPU_FORMAT_DEPTH32_FLOAT_STENCIL8,
    GPU_FORMAT_DEPTH24_UNORM_STENCIL8
  };

  GPUCommandQueue          *queue;
  GPUCommandBuffer         *cmdb;
  GPURenderPassEncoder     *renderPass;
  GPUTexture               *texture;
  GPUTextureView           *view;
  GPUTextureCreateInfo      textureInfo = {0};
  GPUTextureViewCreateInfo  viewInfo    = {0};
  GPUFormatCapabilities     formatCaps;
  GPUFormat                 format;
  int                       ok;

  queue      = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  cmdb       = NULL;
  renderPass = NULL;
  texture    = NULL;
  view       = NULL;
  ok         = queue != NULL;
  if (!ok) {
    fprintf(stderr, "texture view depth-stencil has no graphics queue\n");
    return 0;
  }
  ok = 0;

  format = GPU_FORMAT_UNDEFINED;
  for (uint32_t i = 0u; i < GPU_ARRAY_LEN(formats); i++) {
    if (GPUGetFormatCapabilities(device->phyDevice,
                                 formats[i],
                                 &formatCaps) == GPU_OK &&
        formatCaps.depthStencil) {
      format = formats[i];
      break;
    }
  }
  if (format == GPU_FORMAT_UNDEFINED) {
    printf("texture view depth-stencil skipped: unsupported format\n");
    return 1;
  }

  textureInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_CREATE_INFO;
  textureInfo.chain.structSize = sizeof(textureInfo);
  textureInfo.label            = "texture-view-depth-stencil-target";
  textureInfo.dimension        = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format           = format;
  textureInfo.width            = VIEW_TARGET_WIDTH;
  textureInfo.height           = VIEW_TARGET_HEIGHT;
  textureInfo.depthOrLayers    = VIEW_TARGET_LAYERS;
  textureInfo.mipLevelCount    = VIEW_TARGET_MIPS;
  textureInfo.sampleCount      = 1u;
  textureInfo.usage            = GPU_TEXTURE_USAGE_DEPTH_STENCIL;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK || !texture) {
    fprintf(stderr, "texture view depth-stencil texture setup failed\n");
    goto cleanup;
  }

  viewInfo.chain.sType      = GPU_STRUCTURE_TYPE_TEXTURE_VIEW_CREATE_INFO;
  viewInfo.chain.structSize = sizeof(viewInfo);
  viewInfo.label            = "texture-view-depth-stencil-subresource";
  viewInfo.viewType         = GPU_TEXTURE_VIEW_2D_ARRAY;
  viewInfo.format           = format;
  viewInfo.baseMipLevel     = VIEW_SECOND_MIP;
  viewInfo.mipLevelCount    = 1u;
  viewInfo.baseArrayLayer   = VIEW_SECOND_LAYER;
  viewInfo.arrayLayerCount  = 1u;
  if (GPUCreateTextureView(texture, &viewInfo, &view) != GPU_OK || !view) {
    fprintf(stderr, "texture view depth-stencil view setup failed\n");
    goto cleanup;
  }

  if (GPUAcquireCommandBuffer(queue,
                              "texture-view-depth-stencil",
                              &cmdb) != GPU_OK ||
      !cmdb) {
    fprintf(stderr, "texture view depth-stencil command buffer failed\n");
    goto cleanup;
  }

  renderPass = view_render_begin_ds_clear(cmdb,
                                          view,
                                          0.375f,
                                          37u,
                                          "texture-view-depth-stencil-clear");
  if (!renderPass) {
    fprintf(stderr, "texture view depth-stencil render pass failed\n");
    goto cleanup;
  }
  GPUEndRenderPass(renderPass);
  renderPass = NULL;

  ok   = view_render_submit(device, queue, cmdb);
  cmdb = NULL;
  if (!ok) {
    fprintf(stderr, "texture view depth-stencil submit failed\n");
  }

cleanup:
  if (renderPass) {
    GPUEndRenderPass(renderPass);
  }
  GPUDestroyTextureView(view);
  GPUDestroyTexture(texture);
  return ok;
}
