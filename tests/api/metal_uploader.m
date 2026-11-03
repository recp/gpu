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

#include <gpu/gpu.h>
#import <Foundation/Foundation.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

typedef struct UploadCase {
  const char *name;
  uint32_t    width, height, images, rowBytes, imageRows;
  bool        volume;
} UploadCase;

static const UploadCase kCases[] = {
  {"packed",              64, 3, 1, 256, 3, false},
  {"packed-single-gap",   64, 3, 1, 256, 7, false},
  {"source-row-padding",  64, 3, 1, 272, 3, false},
  {"aligned-short-tail",  63, 3, 1, 256, 3, false},
  {"unaligned-packed",    63, 3, 1, 252, 3, false},
  {"packed-array",        64, 3, 3, 256, 3, false},
  {"array-image-gaps",    64, 3, 3, 256, 5, false},
  {"packed-volume",       64, 3, 3, 256, 3, true},
  {"volume-image-gaps",   64, 3, 3, 256, 5, true},
  {"array-padding-tail",  61, 3, 2, 272, 5, false},
  {"one-texel",            1, 1, 1,   4, 1, false}
};

static uint8_t
payload_byte(uint64_t offset, uint32_t epoch) {
  return (uint8_t)(offset * 29u + (offset >> 8u) * 17u + epoch * 37u);
}

static bool
check_case(GPUDevice *device, const UploadCase *test) {
  GPUTextureCreateInfo       textureInfo = {0};
  GPUBufferCreateInfo        bufferInfo  = {0};
  GPUUploaderCreateInfo      uploadInfo  = {0};
  GPUTextureWriteRegion      initial     = {0}, write = {0};
  GPUBufferTextureCopyRegion copy        = {0};
  GPUQueueSubmitInfo         submit      = {0};
  uint8_t                    prefix[17], bufferBytes[17];
  GPUTexture                *texture  = NULL;
  GPUBuffer                 *readback = NULL, *buffer = NULL;
  GPUUploader               *uploader = NULL;
  GPUFence                  *first = NULL, *last = NULL, *complete = NULL;
  GPUCommandBuffer          *command = NULL;
  GPUTransferPassEncoder    *pass;
  GPUQueue                  *queue;
  uint8_t                   *mapping = MAP_FAILED, *source, *bytes = NULL;
  uint64_t                   sourceSize, sourceImage, mappingSize, totalSize, offset;
  uint64_t                   pageSize, rounded;
  uint32_t                   width, height, images, rowBytes, epoch, x, y, z, c;
  uint8_t                    expected;
  bool                       inside, ok = false;

  queue       = GPUGetQueue(device, GPU_QUEUE_GRAPHICS, 0u);
  width       = test->width + 2u;
  height      = test->height + 2u;
  images      = test->images + 2u;
  rowBytes    = (width * 4u + 255u) & ~255u;
  totalSize   = (uint64_t)rowBytes * height * images;
  sourceImage = (uint64_t)test->rowBytes * test->imageRows;
  sourceSize  = sourceImage * (test->images - 1u)
                + (uint64_t)test->rowBytes * (test->height - 1u) + test->width * 4u;
  pageSize    = (uint64_t)sysconf(_SC_PAGESIZE);
  rounded     = (sourceSize + pageSize - 1u) & ~(pageSize - 1u);
  mappingSize = rounded + 2u * pageSize;
  mapping = mmap(NULL, (size_t)mappingSize, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (mapping == MAP_FAILED
      || mprotect(mapping + pageSize, (size_t)rounded, PROT_READ | PROT_WRITE)
      || !(bytes = malloc((size_t)totalSize)))
    goto cleanup;

  /* the final logical byte directly precedes a guard page, including short tails. */
  source = mapping + pageSize + rounded - sourceSize;
  textureInfo.dimension     = test->volume ? GPU_TEXTURE_DIMENSION_3D : GPU_TEXTURE_DIMENSION_2D;
  textureInfo.format        = GPU_FORMAT_RGBA8_UNORM;
  textureInfo.width         = width;
  textureInfo.height        = height;
  textureInfo.depthOrLayers = images;
  textureInfo.mipLevelCount = 1u;
  textureInfo.sampleCount   = 1u;
  textureInfo.usage         = GPU_TEXTURE_USAGE_COPY_SRC | GPU_TEXTURE_USAGE_COPY_DST;
  bufferInfo.sizeBytes      = totalSize;
  bufferInfo.usage          = GPU_BUFFER_USAGE_COPY_SRC | GPU_BUFFER_USAGE_COPY_DST;
  if (GPUCreateTexture(device, &textureInfo, &texture) != GPU_OK
      || GPUCreateBuffer(device, &bufferInfo, &readback) != GPU_OK)
    goto cleanup;

  bufferInfo.sizeBytes = sizeof(prefix);
  uploadInfo.chain.sType        = GPU_STRUCTURE_TYPE_UPLOADER_CREATE_INFO;
  uploadInfo.chain.structSize   = sizeof(uploadInfo);
  uploadInfo.stagingSizeBytes   = 4096u;
  uploadInfo.stagingBufferCount = 2u;
  if (GPUCreateBuffer(device, &bufferInfo, &buffer) != GPU_OK
      || GPUCreateUploader(queue, &uploadInfo, &uploader) != GPU_OK
      || GPUCreateFence(device, NULL, &complete) != GPU_OK)
    goto cleanup;

  memset(bytes, 0x19, (size_t)totalSize);
  initial.width        = width;
  initial.height       = height;
  initial.depth        = test->volume ? images : 1u;
  initial.layerCount   = test->volume ? 1u : images;
  initial.bytesPerRow  = rowBytes;
  initial.rowsPerImage = height;
  if (GPUQueueWriteTexture(queue, texture, &initial, bytes, totalSize) != GPU_OK)
    goto cleanup;

  write.x              = 1u;
  write.y              = 1u;
  write.z              = test->volume ? 1u : 0u;
  write.baseArrayLayer = test->volume ? 0u : 1u;
  write.width          = test->width;
  write.height         = test->height;
  write.depth          = test->volume ? test->images : 1u;
  write.layerCount     = test->volume ? 1u : test->images;
  write.bytesPerRow    = test->rowBytes;
  write.rowsPerImage   = test->imageRows;
  for (epoch = 0u; epoch < 5u; epoch++) {
    for (offset = 0u; offset < sourceSize; offset++) {
      source[offset] = payload_byte(offset, epoch);
    }
    memset(prefix, (int)(0x40u + epoch), sizeof(prefix));
    if (GPUUploadBuffer(uploader, buffer, 0u, prefix, sizeof(prefix)) != GPU_OK
        || GPUUploadTexture(uploader, texture, &write, source, sourceSize) != GPU_OK)
      goto cleanup;

    memset(source, 0xee, (size_t)sourceSize);
    GPUDestroyFence(last);
    last = NULL;
    if (GPUFlushUploads(uploader, &last) != GPU_OK)
      goto cleanup;

    if (!epoch && GPUFlushUploads(uploader, &first) != GPU_OK)
      goto cleanup;
  }

  GPUDestroyUploader(uploader);
  uploader = NULL;
  if (GPUWaitFence(last, 0u) != GPU_OK || GPUWaitFence(first, 0u) != GPU_OK
      || GPUQueueReadBuffer(queue, buffer, 0u, bufferBytes, sizeof(bufferBytes)) != GPU_OK
      || memcmp(prefix, bufferBytes, sizeof(prefix)))
    goto cleanup;

  if (GPUAcquireCommandBuffer(queue, "uploader-layout-readback", &command) != GPU_OK
      || !(pass = GPUBeginTransferPass(command, "uploader-layout-readback")))
    goto cleanup;

  copy.texture.width      = width;
  copy.texture.height     = height;
  copy.texture.depth      = test->volume ? images : 1u;
  copy.texture.layerCount = test->volume ? 1u : images;
  copy.bytesPerRow        = rowBytes;
  copy.rowsPerImage       = height;
  GPUCopyTextureToBuffer(pass, texture, readback, &copy);
  GPUEndTransferPass(pass);
  submit.ppCommandBuffers   = &command;
  submit.commandBufferCount = 1u;
  submit.fence              = complete;
  if (GPUQueueSubmit(queue, &submit) != GPU_OK)
    goto cleanup;

  command = NULL;
  if (GPUWaitFence(complete, UINT64_C(5000000000)) != GPU_OK
      || GPUQueueReadBuffer(queue, readback, 0u, bytes, totalSize) != GPU_OK)
    goto cleanup;

  for (z = 0u; z < images; z++) {
    for (y = 0u; y < height; y++) {
      for (x = 0u; x < width; x++) {
        inside = z > 0u && z <= test->images && y > 0u && y <= test->height
                 && x > 0u && x <= test->width;
        for (c = 0u; c < 4u; c++) {
          expected = 0x19u;
          if (inside) {
            offset = (z - 1u) * sourceImage + (uint64_t)(y - 1u) * test->rowBytes
                     + (x - 1u) * 4u + c;
            expected = payload_byte(offset, 4u);
          }
          offset = ((uint64_t)z * height + y) * rowBytes + x * 4u + c;
          if (bytes[offset] != expected) {
            fprintf(stderr, "%s pixel %u,%u,%u:%u got %u expected %u\n",
                    test->name, x, y, z, c, bytes[offset], expected);
            goto cleanup;
          }
        }
      }
    }
  }
  ok = true;

cleanup:
  if (command)
    GPUDiscardCommandBuffer(command);
  GPUDestroyUploader(uploader);
  GPUDestroyFence(complete);
  GPUDestroyFence(last);
  GPUDestroyFence(first);
  GPUDestroyBuffer(buffer);
  GPUDestroyBuffer(readback);
  GPUDestroyTexture(texture);
  if (mapping != MAP_FAILED)
    munmap(mapping, (size_t)mappingSize);
  free(bytes);
  printf("uploader layout %s: %s\n", test->name, ok ? "pass" : "fail");
  return ok;
}

int
main(void) {
  @autoreleasepool {
    GPUDeviceCreateInfo info = {0};
    const GPUFeature   feature = GPU_FEATURE_THREADED_UPLOADS;
    GPUInstance       *instance = NULL;
    GPUAdapter        *adapter;
    GPUDevice         *device = NULL;
    uint32_t           i;
    bool               ok = true;

    if (GPUCreateInstance(NULL, &instance) != GPU_OK
        || !(adapter = GPUGetAutoSelectedAdapter(instance)))
      return 1;

    info.required.pFeatures    = &feature;
    info.required.featureCount = 1u;
    if (GPUCreateDevice(adapter, &info, &device) != GPU_OK) {
      GPUDestroyInstance(instance);
      return 77;
    }

    for (i = 0u; i < sizeof(kCases) / sizeof(kCases[0]); i++) {
      ok = check_case(device, &kCases[i]) && ok;
    }
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return ok ? 0 : 1;
  }
}
