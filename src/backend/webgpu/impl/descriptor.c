/*
 * Copyright (C) 2026 Recep Aslantas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#include "../common.h"
#include "../impl.h"

static GPUResult
webgpu_createPipelineLayout(GPUDevice *device, GPUPipelineLayout *layout) {
  WGPUPipelineLayoutDescriptor descriptor = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  GPUBindGroupLayout *const   *groups;
  GPUDeviceWebGPU             *native;
  uint32_t                     groupCount;
  uint32_t                     pushConstantSize;

  native = gpu_webgpuDevice(device);
  groups = gpuGetPipelineLayoutGroups(layout, &groupCount);
  gpuGetPipelineLayoutPushConstants(layout, &pushConstantSize, NULL);
  if (!native || !native->device || groupCount != 0u || groups ||
      pushConstantSize != 0u) {
    return GPU_ERROR_UNSUPPORTED;
  }

  layout->_native = wgpuDeviceCreatePipelineLayout(native->device,
                                                    &descriptor);
  return layout->_native ? GPU_OK : GPU_ERROR_BACKEND_FAILURE;
}

static void
webgpu_destroyPipelineLayout(GPUPipelineLayout *layout) {
  if (layout && layout->_native) {
    wgpuPipelineLayoutRelease(layout->_native);
    layout->_native = NULL;
  }
}

void
webgpu_initDescriptor(GPUApiDescriptor *api) {
  api->createPipelineLayout  = webgpu_createPipelineLayout;
  api->destroyPipelineLayout = webgpu_destroyPipelineLayout;
}
