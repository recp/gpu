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

#include "bench.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct UploadCase {
  const char *name;
  uint32_t    width, height, rowBytes, imageRows, images, batch;
} UploadCase;

static const UploadCase kCases[] = {
  {"tile-packed",      64,   64,  256,   64, 1, 8},
  {"tile-padded",      64,   64,  272,   64, 1, 8},
  {"page-packed",     256,  256, 1024,  256, 1, 1},
  {"page-padded",     256,  256, 1056,  256, 1, 1},
  {"page-batch",      256,  256, 1024,  256, 1, 8},
  {"atlas-packed",   1024, 1024, 4096, 1024, 1, 1},
  {"atlas-padded",   1024, 1024, 4128, 1024, 1, 1},
  {"layers-packed",   256,   64, 1024,   64, 4, 1},
  {"layers-gapped",   256,   64, 1024,   68, 4, 1}
};

/* source bytes, textures and staging slots are reused. upload_api includes
 * validation, copying and command encoding; completion_wait includes host and
 * queue scheduling, so it is not an isolated GPU execution duration. each
 * measured batch completes before the next, and readback verification is untimed.
 */

static bool
check_texture(GPUDevice *device, GPUQueue *queue, GPUTexture *texture,
              const UploadCase *test, const uint8_t *source) {
  GPUBufferCreateInfo        info   = {0};
  GPUBufferTextureCopyRegion copy   = {0};
  GPUQueueSubmitInfo         submit = {0};
  GPUCommandBuffer         *command = NULL;
  GPUTransferPassEncoder   *pass;
  GPUBuffer                *buffer = NULL;
  GPUFence                 *fence  = NULL;
  uint8_t                  *bytes  = NULL;
  uint64_t                  size, sourceImage;
  uint32_t                  pitch, image, row;
  bool                      ok = false;

  pitch       = (test->width * 4u + 255u) & ~255u;
  size        = (uint64_t)pitch * test->height * test->images;
  sourceImage = (uint64_t)test->rowBytes * test->imageRows;
  info.sizeBytes = size;
  info.usage     = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  if (!(bytes = malloc((size_t)size))
      || GPUCreateBuffer(device, &info, &buffer) != GPU_OK
      || GPUCreateFence(device, NULL, &fence) != GPU_OK
      || GPUAcquireCommandBuffer(queue, "uploader-cost-verify", &command) != GPU_OK
      || !(pass = GPUBeginTransferPass(command, "uploader-cost-verify")))
    goto cleanup;

  copy.bytesPerRow        = pitch;
  copy.rowsPerImage       = test->height;
  copy.texture.width      = test->width;
  copy.texture.height     = test->height;
  copy.texture.depth      = 1u;
  copy.texture.layerCount = test->images;
  GPUCopyTextureToBuffer(pass, texture, buffer, &copy);
  GPUEndTransferPass(pass);
  submit.ppCommandBuffers   = &command;
  submit.commandBufferCount = 1u;
  submit.fence              = fence;
  if (GPUQueueSubmit(queue, &submit) != GPU_OK)
    goto cleanup;

  command = NULL;
  if (GPUWaitFence(fence, UINT64_C(5000000000)) != GPU_OK
      || GPUQueueReadBuffer(queue, buffer, 0u, bytes, size) != GPU_OK)
    goto cleanup;

  for (image = 0u; image < test->images; image++) {
    for (row = 0u; row < test->height; row++) {
      if (memcmp(bytes + ((uint64_t)image * test->height + row) * pitch,
                 source + image * sourceImage + (uint64_t)row * test->rowBytes,
                 test->width * 4u))
        goto cleanup;
    }
  }
  ok = true;

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyFence(fence);
  GPUDestroyBuffer(buffer);
  free(bytes);
  return ok;
}

static bool
measure_case(GPUDevice *device, const UploadCase *test, uint32_t iterations) {
  GPUTextureCreateInfo  textureInfo = {0};
  GPUUploaderCreateInfo uploadInfo  = {0};
  GPUTextureWriteRegion region      = {0};
  GPUUploader         *uploader = NULL;
  GPUTexture          *texture  = NULL;
  GPUFence            *fence    = NULL;
  GPUQueue            *queue;
  uint8_t             *source = NULL;
  uint64_t             size, imageSize, payload, stagingSize, i;
  double               begin, encoded, submitted, complete;
  uint32_t             frame, batch;
  bool                 ok = false;

  queue       = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  imageSize   = (uint64_t)test->rowBytes * test->imageRows;
  size        = imageSize * (test->images - 1u)
                + (uint64_t)test->rowBytes * (test->height - 1u) + test->width * 4u;
  payload     = (uint64_t)test->width * 4u * test->height * test->images;
  stagingSize = ((payload + 255u) & ~UINT64_C(255)) * test->batch;
  if (!(source = malloc((size_t)size)))
    goto cleanup;

  for (i = 0u; i < size; i++) {
    source[i] = (uint8_t)(i * 29u + (i >> 8u) * 17u);
  }
  textureInfo.dimension     = GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format        = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width         = test->width;
  textureInfo.height        = test->height;
  textureInfo.depthOrLayers = test->images;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount   = 1u;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;
  uploadInfo.chain.sType        = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
  uploadInfo.chain.structSize   = sizeof(uploadInfo);
  uploadInfo.stagingSizeBytes   = stagingSize;
  uploadInfo.stagingBufferCount = 3u;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK
      || GPUCreateUploader(queue, &uploadInfo, &uploader) != GPU_OK)
    goto cleanup;

  region.width        = test->width;
  region.height       = test->height;
  region.depth        = 1u;
  region.layerCount   = test->images;
  region.bytesPerRow  = test->rowBytes;
  region.rowsPerImage = test->imageRows;
  for (frame = 0u; frame < iterations + 8u; frame++) {
    begin = bench_now();
    for (batch = 0u; batch < test->batch; batch++) {
      if (GPUUploadTexture(uploader, texture, &region, source, size) != GPU_OK)
        goto cleanup;
    }
    encoded = bench_now();
    if (GPUFlushUploads(uploader, &fence) != GPU_OK)
      goto cleanup;

    submitted = bench_now();
    if (GPUWaitFence(fence, UINT64_C(5000000000)) != GPU_OK)
      goto cleanup;

    complete = bench_now();
    GPUDestroyFence(fence);
    fence = NULL;
    if (frame >= 8u) {
      printf("%s,%u,%" PRIu64 ",%u,%.0f,%.0f,%.0f,%.0f\n",
             test->name, frame - 8u, payload, test->batch,
             (encoded - begin) * 1e9, (submitted - encoded) * 1e9,
             (complete - submitted) * 1e9, (complete - begin) * 1e9);
    }
  }
  ok = check_texture(device, queue, texture, test, source);

cleanup:
  GPUDestroyUploader(uploader);
  GPUDestroyFence(fence);
  GPUDestroyTexture(texture);
  free(source);
  return ok;
}

int
main(int argc, char *argv[]) {
  GPUDeviceCreateInfo   info         = {0};
  GPUInstanceCreateInfo instanceInfo = {0};
  GPUAdapterProperties properties   = {0};
  const GPUFeature     feature = GPU_FEATURE_THREADED_UPLOADS;
  GPUInstance         *instance = NULL;
  GPUAdapter          *adapter;
  GPUDevice           *device = NULL;
  uint32_t             iterations = 128u, i;
  bool                 ok = false;

  if (argc > 2 || (argc == 2 && !bench_parseU32(argv[1], 1u, &iterations)))
    return 1;

  instanceInfo.preferredBackend = GPU_BACKEND_METAL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK
      || !(adapter = GPUGetAutoSelectedAdapter(instance)))
    goto cleanup;

  info.required.pFeatures    = &feature;
  info.required.featureCount = 1u;
  if (GPUCreateDevice(adapter, &info, &device) != GPU_OK
      || GPUGetAdapterProperties(adapter, &properties) != GPU_OK)
    goto cleanup;

  fprintf(stderr, "uploader completed-batch cost: %s; reused source, texture and staging\n",
          properties.name ? properties.name : "unknown");
  printf("case,iteration,payload_bytes,uploads,upload_api_ns,flush_ns,completion_wait_ns,total_ns\n");
  for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
    if (!measure_case(device, &kCases[i], iterations))
      goto cleanup;
  }
  ok = true;

cleanup:
  GPUDestroyDevice(device);
  GPUDestroyInstance(instance);
  return ok ? 0 : 1;
}
