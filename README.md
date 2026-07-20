# 🔭 GPU - Modern Cross platform GPU library

GPU is a cross-platform C library for explicit graphics and compute workloads.
Since it inspired from Metal, its API keeps common rendering paths close to Metal while preserving the
control required by Vulkan and DirectX 12.

!! The project is under active development. The public API may not yet stable.

## Backends

- [x] Metal
- [x] Vulkan
- [x] DirectX 12
- [x] WebGPU

## Shaders

GPU integrates with the [Universal Shading](https://github.com/UniversalShading/us). 
USL owns source parsing, reflection, target compilation, and persistent shader
artifact caching. GPU consumes USL artifacts and creates native backend shader
objects.

## Build

Metal debug build on macOS:

```sh
cmake --preset macos-debug
cmake --build --preset macos-debug
ctest --test-dir out/build/macos-debug --output-on-failure
```

Vulkan debug build on macOS:

```sh
cmake --preset macos-vulkan-debug
cmake --build --preset macos-vulkan-debug
ctest --test-dir out/build/macos-vulkan-debug --output-on-failure
```

## Frame Flow

The common render path uses an acquired frame, one command buffer, explicit
render-pass commands, and a single submit/present helper:

```c
GPUFrame         *frame = GPUBeginFrame(swapchain);
GPUCommandBuffer *cmdb  = NULL;

if (!frame) {
  return;
}

if (GPUAcquireCommandBuffer(queue, "frame", &cmdb) != GPU_OK) {
  GPUEndFrame(frame);
  return;
}

GPURenderPassColorAttachment color = {0};
color.view                         = GPUFrameGetTargetView(frame);
color.loadOp                       = GPU_LOAD_OP_CLEAR;
color.storeOp                      = GPU_STORE_OP_STORE;
color.clearColor.float32[3]        = 1.0f;

GPURenderPassCreateInfo passInfo = {0};
passInfo.colorAttachmentCount    = 1;
passInfo.pColorAttachments       = &color;

GPURenderPassEncoder *pass = GPUBeginRenderPass(cmdb, &passInfo);
GPUBindRenderPipeline(pass, pipeline);
GPUDraw(pass, 3, 1, 0, 0);
GPUEndRenderPass(pass);

GPUResult result = GPUFinishFrame(queue, cmdb, frame);
```

Use `GPUSchedulePresent`, `GPUQueueSubmit`, and `GPUEndFrame` separately when
manual submit control is required.

## Samples

- `samples/triangle-usl`: Metal triangle using a USL artifact
- `samples/triangle-vulkan-usl`: Vulkan triangle using the same USL source
- `samples/triangle-dx12-usl`: Direct3D 12 triangle using the same USL source
- `samples/textured-quad-usl`: Metal texture and sampler binding
- `samples/compute-buffer-usl`: compute-to-render buffer flow
- `samples/*-webgpu-usl`: browser WebGPU render, compute, storage, and binding flows
- `samples/usl-reflection-check`: reflection and binding metadata validation

The Metal triangle executable is generated at:

```sh
./out/build/macos-debug/samples/gpu-triangle-metal-usl/gpu-triangle-metal-usl
```

## License

GPU is licensed under the Apache License 2.0.

# Trademarks

Apple, Metal, DirectX, Vulkan, and other product names are trademarks of their
respective owners.
