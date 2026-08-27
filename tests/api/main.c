#include "test.h"
#include "../../src/api/instance_internal.h"

static int
run_adapter_request(void *ctx) {
  return gpu_test_adapter_request_options(
    ((GPUApiTestContext *)ctx)->instance
  );
}

static int
run_queue(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_queue(testCtx->instance,
                        testCtx->adapter,
                        testCtx->device);
}

static int
run_sampler(void *ctx) {
  return gpu_test_sampler(((GPUApiTestContext *)ctx)->device);
}

static int
run_bindgroup(void *ctx) {
  return gpu_test_bindgroup(((GPUApiTestContext *)ctx)->device);
}

static int
run_bindless(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_bindless(testCtx->adapter,
                           testCtx->descriptorIndexingBytecodePath);
}

static int
run_resources(void *ctx) {
  return gpu_test_resources(((GPUApiTestContext *)ctx)->device);
}

static int
run_copy(void *ctx) {
  return gpu_test_copy(((GPUApiTestContext *)ctx)->device);
}

static int
run_coordinate(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_coordinate_contract(testCtx->device,
                                      testCtx->coordinateBytecodePath);
}

static int
run_render(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_render(testCtx->device, testCtx->mrtBytecodePath);
}

static int
run_msaa(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_msaa_resolve_sample(testCtx->device,
                                      testCtx->msaaBytecodePath);
}

static int
run_compute(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_compute(testCtx->device, testCtx->computeBytecodePath);
}

static int
run_execution_graph(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_execution_graph_validation() &&
         gpu_test_execution_graph(testCtx->adapter,
                                  testCtx->executionGraphBytecodePath);
}

static int
run_query(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_query(testCtx->adapter,
                        testCtx->device,
                        testCtx->computeBytecodePath);
}

static int
run_barrier(void *ctx) {
  return gpu_test_barrier(((GPUApiTestContext *)ctx)->device);
}

static int
run_memory(void *ctx) {
  return gpu_test_memory(((GPUApiTestContext *)ctx)->adapter);
}

static int
run_multigpu(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_multigpu(testCtx->adapter, testCtx->device);
}

static int
run_runtime(void *ctx) {
  return gpu_test_runtime(((GPUApiTestContext *)ctx)->device);
}

static int
run_threading(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_threading(testCtx->device, testCtx->uslBytecodePath);
}

static int
run_shader(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_shader(testCtx->device,
                         testCtx->uslBytecodePath,
                         testCtx->descriptorArrayBytecodePath);
}

static int
run_source_sampler(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_source_sampler_draw(testCtx->device,
                                      testCtx->sourceSamplerBytecodePath);
}

static int
run_descriptor_array(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_descriptor_array(testCtx->device,
                                   testCtx->descriptorArrayBytecodePath);
}

static int
run_descriptor_indexing(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_descriptor_indexing(
    testCtx->adapter,
    testCtx->descriptorIndexingBytecodePath
  );
}

static int
run_buffer_descriptor_array(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_buffer_descriptor_array(
    testCtx->adapter,
    testCtx->bufferDescriptorArrayBytecodePath,
    testCtx->bufferDescriptorArrayDynamicBytecodePath
  );
}

static int
run_storage_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_storage_texture_view(testCtx->device,
                                       testCtx->storageTextureBytecodePath);
}

static int
run_cube_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_cube_texture_view(testCtx->device,
                                    testCtx->cubeTextureBytecodePath);
}

static int
run_line_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_line_texture_view(testCtx->device,
                                    testCtx->lineTextureBytecodePath);
}

static int
run_volume_texture(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_volume_texture_view(testCtx->device,
                                      testCtx->volumeTextureBytecodePath);
}

static int
run_subgroup(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_subgroup(testCtx->adapter, testCtx->subgroupBytecodePath);
}

static int
run_subgroup_matrix(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_subgroup_matrix(testCtx->adapter,
                                  testCtx->subgroupMatrixBytecodePath);
}

static int
run_shader_f16(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_shader_f16(testCtx->adapter,
                             testCtx->shaderF16BytecodePath);
}

static int
run_atomic64(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_atomic64(testCtx->adapter,
                           testCtx->atomic64BytecodePath);
}

static int
run_vrs(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_vrs(testCtx->adapter,
                      testCtx->device,
                      testCtx->coordinateBytecodePath);
}

static int
run_sampler_feedback(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_sampler_feedback(testCtx->adapter,
                                   testCtx->device,
                                   testCtx->samplerFeedbackBytecodePath);
}

static int
run_ray_query(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_ray_query(testCtx->adapter,
                            testCtx->rayQueryBytecodePath);
}

static int
run_intersection_function(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_intersection_function_table(
    testCtx->adapter,
    testCtx->intersectionFunctionBytecodePath
  );
}

static int
run_ray_pipeline(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_ray_pipeline_feature(testCtx->adapter,
                                       testCtx->rayPipelineBytecodePath);
}

static int
run_clock_derivatives(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_clock_derivatives(
    testCtx->adapter,
    testCtx->shaderSubgroupClockBytecodePath,
    testCtx->shaderDeviceClockBytecodePath,
    testCtx->computeDerivativeQuadsBytecodePath,
    testCtx->computeDerivativeLinearBytecodePath
  );
}

static int
run_untyped_pointer(void *ctx) {
  GPUApiTestContext *testCtx = ctx;

  return gpu_test_untyped_pointer(testCtx->device,
                                  testCtx->untypedPointerBytecodePath);
}

static int
run_dx12_binding_plan(void *ctx) {
  GPUApiTestContext *testCtx = ctx;
  GPUAdapterProperties properties;

  if (GPUGetAdapterProperties(testCtx->adapter, &properties) != GPU_OK) {
    return 0;
  }
  if (properties.backend != GPU_BACKEND_DX12) {
    printf("DX12 binding-plan execution skipped: non-DX12 backend\n");
    return 1;
  }

  return gpu_test_dx12_binding_plan(testCtx->device,
                                    testCtx->dx12BindingPlanBytecodePath);
}

static bool
parse_backend(const char *name, GPUBackend *outBackend) {
  if (!name || !outBackend) {
    return false;
  }

  if (strcmp(name, "metal") == 0) {
    *outBackend = GPU_BACKEND_METAL;
  } else if (strcmp(name, "vulkan") == 0) {
    *outBackend = GPU_BACKEND_VULKAN;
  } else if (strcmp(name, "dx12") == 0) {
    *outBackend = GPU_BACKEND_DX12;
  } else if (strcmp(name, "webgpu") == 0) {
    *outBackend = GPU_BACKEND_WEBGPU;
  } else {
    return false;
  }
  return true;
}

int
main(int argc, char **argv) {
  GPUInstanceCreateInfo instanceInfo = {0};
  GPURuntimeConfig      runtimeConfig = {0};
  GPUInstance          *instance;
  GPUAdapter           *adapter;
  GPUDevice            *device;
  GPUApiTestContext      ctx;
  GPUApiTest             tests[39];
  uint64_t               timingStart, timingMark;
  bool                   timings;
  int                    ok;

  timings     = getenv("GPU_API_TIMINGS") != NULL;
  timingStart = timings ? gpu_test_now_ns() : 0u;
  timingMark  = timingStart;

  if (argc != 14 && argc != 15) {
    fprintf(stderr,
            "usage: %s <reflection.us> <render_mrt.us> <compute.us> "
            "<source_sampler.us> <storage_texture.us> <cube_texture.us> "
            "<line_texture.us> <volume_texture.us> <descriptor_arrays.us> "
            "<coordinate.us> <descriptor_indexing.us> <subgroup.us> "
            "<shader_f16.us> "
            "[metal|vulkan|dx12|webgpu]\n",
            argv[0]);
    return 2;
  }

  instanceInfo.chain.sType      = GPU_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instanceInfo.chain.structSize = sizeof(instanceInfo);
  instanceInfo.preferredBackend = GPU_BACKEND_DEFAULT;
  instanceInfo.enableValidation = getenv("GPU_API_DISABLE_VALIDATION") == NULL;
  if (argc == 15 &&
      !parse_backend(argv[14], &instanceInfo.preferredBackend)) {
    fprintf(stderr, "unknown backend: %s\n", argv[14]);
    return 2;
  }
  instance = NULL;
  if (GPUCreateInstance(&instanceInfo, &instance) != GPU_OK || !instance) {
    fprintf(stderr, "failed to create instance\n");
    return 1;
  }
  if (timings) {
    uint64_t now = gpu_test_now_ns();
    printf("api:timing:instance=%.3fms\n",
           (double)(now - timingMark) / 1000000.0);
    timingMark = now;
  }

  if (gpu_test_request_adapter(instance, &adapter) != GPU_OK || !adapter) {
    fprintf(stderr, "failed to get adapter\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (timings) {
    uint64_t now = gpu_test_now_ns();
    printf("api:timing:adapter=%.3fms\n",
           (double)(now - timingMark) / 1000000.0);
    timingMark = now;
  }
  if (getenv("GPU_TEST_ADAPTER") || getenv("GPU_API_VERBOSE")) {
    GPUAdapterCapabilities capabilities;
    GPUAdapterProperties properties;

    if (GPUGetAdapterProperties(adapter, &properties) == GPU_OK) {
      printf("api:adapter=%s type=%u\n",
             properties.name ? properties.name : "unknown",
             (unsigned)properties.type);
    }
    if (getenv("GPU_API_VERBOSE") &&
        GPUGetAdapterCapabilities(adapter, &capabilities) == GPU_OK) {
      fputs("api:features=", stdout);
      for (uint32_t i = 0u; i < capabilities.supported.featureCount; i++) {
        printf("%s%u",
               i == 0u ? "" : ",",
               (unsigned)capabilities.supported.pFeatures[i]);
      }
      putchar('\n');
    }
  }

  if (gpu_test_create_device(adapter, NULL, &device) != GPU_OK || !device) {
    fprintf(stderr, "failed to create device\n");
    GPUDestroyInstance(instance);
    return 1;
  }
  if (timings) {
    uint64_t now = gpu_test_now_ns();
    printf("api:timing:device=%.3fms\n",
           (double)(now - timingMark) / 1000000.0);
    timingMark = now;
  }

  runtimeConfig.chain.sType          = GPU_STRUCTURE_TYPE_RUNTIME_CONFIG;
  runtimeConfig.chain.structSize     = sizeof(runtimeConfig);
  runtimeConfig.enableDebugMarkers   = true;
  runtimeConfig.enableVerboseLogs    = getenv("GPU_API_VERBOSE") != NULL;
  if (GPUConfigureRuntime(device, &runtimeConfig) != GPU_OK) {
    fprintf(stderr, "failed to enable debug markers\n");
    GPUDestroyDevice(device);
    GPUDestroyInstance(instance);
    return 1;
  }
  if (timings) {
    uint64_t now = gpu_test_now_ns();
    printf("api:timing:runtime=%.3fms\n",
           (double)(now - timingMark) / 1000000.0);
    timingMark = now;
  }

  ctx.instance                    = instance;
  ctx.adapter                     = adapter;
  ctx.device                      = device;
  ctx.uslBytecodePath             = argv[1];
  ctx.mrtBytecodePath             = argv[2];
  ctx.msaaBytecodePath            = getenv("GPU_MSAA_USL_PATH");
  ctx.computeBytecodePath         = argv[3];
  ctx.sourceSamplerBytecodePath   = argv[4];
  ctx.storageTextureBytecodePath  = argv[5];
  ctx.cubeTextureBytecodePath     = argv[6];
  ctx.lineTextureBytecodePath     = argv[7];
  ctx.volumeTextureBytecodePath   = argv[8];
  ctx.descriptorArrayBytecodePath = argv[9];
  ctx.coordinateBytecodePath         = argv[10];
  ctx.descriptorIndexingBytecodePath = argv[11];
  ctx.bufferDescriptorArrayBytecodePath =
    getenv("GPU_BUFFER_DESCRIPTOR_ARRAY_USL_PATH");
  ctx.bufferDescriptorArrayDynamicBytecodePath =
    getenv("GPU_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_PATH");
  ctx.subgroupBytecodePath           = argv[12];
  ctx.subgroupMatrixBytecodePath     = getenv("GPU_SUBGROUP_MATRIX_USL_PATH");
  ctx.shaderF16BytecodePath          = argv[13];
  ctx.atomic64BytecodePath           = getenv("GPU_ATOMIC64_USL_PATH");
  ctx.rayQueryBytecodePath           = getenv("GPU_RAY_QUERY_USL_PATH");
  ctx.intersectionFunctionBytecodePath =
    getenv("GPU_INTERSECTION_FUNCTION_USL_PATH");
  ctx.rayPipelineBytecodePath        = getenv("GPU_RAY_PIPELINE_USL_PATH");
  ctx.executionGraphBytecodePath     = getenv("GPU_EXECUTION_GRAPH_USL_PATH");
  ctx.samplerFeedbackBytecodePath    =
    getenv("GPU_SAMPLER_FEEDBACK_USL_PATH");
  ctx.shaderSubgroupClockBytecodePath =
    getenv("GPU_SHADER_SUBGROUP_CLOCK_USL_PATH");
  ctx.shaderDeviceClockBytecodePath =
    getenv("GPU_SHADER_DEVICE_CLOCK_USL_PATH");
  ctx.computeDerivativeQuadsBytecodePath =
    getenv("GPU_COMPUTE_DERIVATIVE_QUADS_USL_PATH");
  ctx.computeDerivativeLinearBytecodePath =
    getenv("GPU_COMPUTE_DERIVATIVE_LINEAR_USL_PATH");
  ctx.untypedPointerBytecodePath =
    getenv("GPU_UNTYPED_POINTER_USL_PATH");
  ctx.dx12BindingPlanBytecodePath =
    getenv("GPU_DX12_BINDING_PLAN_USL_PATH");

  tests[0]  = (GPUApiTest){ "adapter-request", run_adapter_request, &ctx };
  tests[1]  = (GPUApiTest){ "queue", run_queue, &ctx };
  tests[2]  = (GPUApiTest){ "sampler", run_sampler, &ctx };
  tests[3]  = (GPUApiTest){ "bindgroup", run_bindgroup, &ctx };
  tests[4]  = (GPUApiTest){ "resources", run_resources, &ctx };
  tests[5]  = (GPUApiTest){ "threading", run_threading, &ctx };
  tests[6]  = (GPUApiTest){ "copy", run_copy, &ctx };
  tests[7]  = (GPUApiTest){ "render", run_render, &ctx };
  tests[8]  = (GPUApiTest){ "compute", run_compute, &ctx };
  tests[9]  = (GPUApiTest){ "query", run_query, &ctx };
  tests[10] = (GPUApiTest){ "barrier", run_barrier, &ctx };
  tests[11] = (GPUApiTest){ "memory", run_memory, &ctx };
  tests[12] = (GPUApiTest){ "multigpu", run_multigpu, &ctx };
  tests[13] = (GPUApiTest){ "runtime", run_runtime, &ctx };
  tests[14] = (GPUApiTest){ "shader", run_shader, &ctx };
  tests[15] = (GPUApiTest){ "source-sampler", run_source_sampler, &ctx };
  tests[16] = (GPUApiTest){ "storage-texture", run_storage_texture, &ctx };
  tests[17] = (GPUApiTest){ "cube-texture", run_cube_texture, &ctx };
  tests[18] = (GPUApiTest){ "line-texture", run_line_texture, &ctx };
  tests[19] = (GPUApiTest){ "volume-texture", run_volume_texture, &ctx };
  tests[20] = (GPUApiTest){ "descriptor-array", run_descriptor_array, &ctx };
  tests[21] = (GPUApiTest){
    "descriptor-indexing", run_descriptor_indexing, &ctx
  };
  tests[22] = (GPUApiTest){ "subgroup", run_subgroup, &ctx };
  tests[23] = (GPUApiTest){
    "subgroup-matrix", run_subgroup_matrix, &ctx
  };
  tests[24] = (GPUApiTest){ "shader-f16", run_shader_f16, &ctx };
  tests[25] = (GPUApiTest){ "bindless", run_bindless, &ctx };
  tests[26] = (GPUApiTest){ "coordinate", run_coordinate, &ctx };
  tests[27] = (GPUApiTest){ "vrs", run_vrs, &ctx };
  tests[28] = (GPUApiTest){ "ray-query", run_ray_query, &ctx };
  tests[29] = (GPUApiTest){ "atomic64", run_atomic64, &ctx };
  tests[30] = (GPUApiTest){ "ray-pipeline", run_ray_pipeline, &ctx };
  tests[31] = (GPUApiTest){ "execution-graph", run_execution_graph, &ctx };
  tests[32] = (GPUApiTest){
    "sampler-feedback", run_sampler_feedback, &ctx
  };
  tests[33] = (GPUApiTest){
    "clock-derivatives", run_clock_derivatives, &ctx
  };
  tests[34] = (GPUApiTest){
    "untyped-pointer", run_untyped_pointer, &ctx
  };
  tests[35] = (GPUApiTest){
    "buffer-descriptor-array", run_buffer_descriptor_array, &ctx
  };
  tests[36] = (GPUApiTest){ "msaa", run_msaa, &ctx };
  tests[37] = (GPUApiTest){
    "intersection-function-table", run_intersection_function, &ctx
  };
  tests[38] = (GPUApiTest){
    "dx12-binding-plan", run_dx12_binding_plan, &ctx
  };

  ok = gpu_run_api_tests(tests, (uint32_t)GPU_ARRAY_LEN(tests));

  if (timings) {
    uint64_t now = gpu_test_now_ns();
    printf("api:timing:tests=%.3fms\n",
           (double)(now - timingMark) / 1000000.0);
    printf("api:timing:total=%.3fms\n",
           (double)(now - timingStart) / 1000000.0);
  }

  GPUDestroyDevice(device);
  if (instance->validationError != 0u) {
    fprintf(stderr, "native validation reported an error\n");
    ok = 0;
  }
  GPUDestroyInstance(instance);
  if (!ok) {
    return 1;
  }

  printf("GPU API validation passed\n");
  return 0;
}
