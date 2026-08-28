# Validation fixture generation stays local to the validation suite.

set(GPU_VALIDATION_REFLECTION_USL_SOURCE
    "${PROJECT_SOURCE_DIR}/tests/validation/usl-reflection-check/reflection.usl")
set(GPU_VALIDATION_MESH_USL_SOURCE
    "${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl")
set(GPU_VALIDATION_TRIANGLE_USL_SOURCE
    "${PROJECT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl")

if(GPU_BUILD_DX12 AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  gpu_add_usl_fixtures(
    GPU_DX12_USL_FIXTURES
    dx12
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/triangle-dx12-usl/triangle.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/textured-quad-dx12-usl/textured_quad.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-dx12-usl/compute_buffer.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/compute_atomics.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/vrs-compare-usl/vrs_compare.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/async_copy.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/mesh-native-usl/mesh_native.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/matrix-elementwise-dx12-usl/matrix_elementwise.usl"
  )
  list(GET GPU_DX12_USL_FIXTURES 0 GPU_DX12_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 1 GPU_DX12_TEXTURED_QUAD_US)
  list(GET GPU_DX12_USL_FIXTURES 2 GPU_DX12_COMPUTE_BUFFER_US)
  list(GET GPU_DX12_USL_FIXTURES 3 GPU_DX12_MESH_TRIANGLE_US)
  list(GET GPU_DX12_USL_FIXTURES 4 GPU_DX12_COMPUTE_ATOMICS_US)
  list(GET GPU_DX12_USL_FIXTURES 5 GPU_DX12_TEXTURED_CUBE_US)
  list(GET GPU_DX12_USL_FIXTURES 6 GPU_DX12_MRT_BLEND_US)
  list(GET GPU_DX12_USL_FIXTURES 7 GPU_DX12_SHADOW_COMPARE_US)
  list(GET GPU_DX12_USL_FIXTURES 8 GPU_DX12_VRS_COMPARE_US)
  list(GET GPU_DX12_USL_FIXTURES 9 GPU_DX12_ASYNC_COPY_US)
  list(GET GPU_DX12_USL_FIXTURES 10 GPU_DX12_MESH_NATIVE_US)
  list(GET GPU_DX12_USL_FIXTURES 11 GPU_DX12_MATRIX_ELEMENTWISE_US)
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_DX12_BINDLESS_USL_FIXTURE
    dx12
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_DX12_BINDLESS_USL_FIXTURE 0 GPU_DX12_BINDLESS_US)
  list(APPEND GPU_DX12_USL_FIXTURES ${GPU_DX12_BINDLESS_USL_FIXTURE})
  add_custom_target(gpu-dx12-fixtures DEPENDS ${GPU_DX12_USL_FIXTURES})
endif()

if(GPU_BUILD_VULKAN AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  gpu_add_usl_fixtures(
    GPU_VULKAN_USL_FIXTURES
    vulkan
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/triangle-vulkan-usl/triangle.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/textured-quad-vulkan-usl/textured_quad.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-vulkan-usl/compute_buffer.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/compute_atomics.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/vrs-compare-usl/vrs_compare.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/async_copy.usl"
  )
  list(GET GPU_VULKAN_USL_FIXTURES 0 GPU_VULKAN_TRIANGLE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 1 GPU_VULKAN_TEXTURED_QUAD_US)
  list(GET GPU_VULKAN_USL_FIXTURES 2 GPU_VULKAN_COMPUTE_BUFFER_US)
  list(GET GPU_VULKAN_USL_FIXTURES 3 GPU_VULKAN_MESH_TRIANGLE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 4 GPU_VULKAN_COMPUTE_ATOMICS_US)
  list(GET GPU_VULKAN_USL_FIXTURES 5 GPU_VULKAN_TEXTURED_CUBE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 6 GPU_VULKAN_MRT_BLEND_US)
  list(GET GPU_VULKAN_USL_FIXTURES 7 GPU_VULKAN_SHADOW_COMPARE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 8 GPU_VULKAN_VRS_COMPARE_US)
  list(GET GPU_VULKAN_USL_FIXTURES 9 GPU_VULKAN_ASYNC_COPY_US)
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_VULKAN_BINDLESS_USL_FIXTURE
    vulkan
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_VULKAN_BINDLESS_USL_FIXTURE 0 GPU_VULKAN_BINDLESS_US)
  list(APPEND GPU_VULKAN_USL_FIXTURES ${GPU_VULKAN_BINDLESS_USL_FIXTURE})
  if(GPU_BUILD_TESTS)
    gpu_add_usl_fixtures(
      GPU_VULKAN_TEST_USL_FIXTURES
      vulkan
      tests
      "${PROJECT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
    )
    list(GET GPU_VULKAN_TEST_USL_FIXTURES 0 GPU_VULKAN_DYNAMIC_TRIANGLE_US)
    list(APPEND GPU_VULKAN_USL_FIXTURES ${GPU_VULKAN_TEST_USL_FIXTURES})
  endif()
  add_custom_target(gpu-vulkan-fixtures DEPENDS ${GPU_VULKAN_USL_FIXTURES})
endif()

if(GPU_BUILD_TESTS OR (GPU_BUILD_CUDA AND GPU_BUILD_SAMPLES))
  gpu_add_usl_fixtures(
    GPU_CUDA_METADATA_USL_FIXTURES
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/storage-texture-cuda-usl/storage_texture.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/sampled-texture-cuda-usl/sampled_texture.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/sampled-format-cuda-usl/sampled_format.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/texture-geometry-cuda-usl/texture_geometry.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/storage-geometry-cuda-usl/storage_geometry.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-cuda-usl/compute_buffer.usl"
  )
  list(GET GPU_CUDA_METADATA_USL_FIXTURES 0 GPU_CUDA_STORAGE_TEXTURE_US)
  list(GET GPU_CUDA_METADATA_USL_FIXTURES 1 GPU_CUDA_SAMPLED_TEXTURE_US)
  list(GET GPU_CUDA_METADATA_USL_FIXTURES 2 GPU_CUDA_SAMPLED_FORMAT_US)
  list(GET GPU_CUDA_METADATA_USL_FIXTURES 3 GPU_CUDA_TEXTURE_GEOMETRY_US)
  list(GET GPU_CUDA_METADATA_USL_FIXTURES 4 GPU_CUDA_STORAGE_GEOMETRY_US)
  list(GET GPU_CUDA_METADATA_USL_FIXTURES 5 GPU_CUDA_COMPUTE_BUFFER_US)

  set(GPU_USL_FIXTURE_TARGET_CAPS bounded_descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_CUDA_DESCRIPTOR_ARRAY_USL_FIXTURES
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array_dynamic.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/resource-descriptor-array-cuda-usl/resource_descriptor_array.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_CUDA_DESCRIPTOR_ARRAY_USL_FIXTURES
       0
       GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_US)
  list(GET GPU_CUDA_DESCRIPTOR_ARRAY_USL_FIXTURES
       1
       GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_US)
  list(GET GPU_CUDA_DESCRIPTOR_ARRAY_USL_FIXTURES
       2
       GPU_CUDA_RESOURCE_DESCRIPTOR_ARRAY_US)

  set(GPU_USL_FIXTURE_TARGET_CAPS ptx_6_2,sm_70,subgroup)
  gpu_add_usl_fixtures(
    GPU_CUDA_SUBGROUP_USL_FIXTURES
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/api/subgroup.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/subgroup_relative.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_CUDA_SUBGROUP_USL_FIXTURES 0 GPU_CUDA_SUBGROUP_US)
  list(GET GPU_CUDA_SUBGROUP_USL_FIXTURES 1 GPU_CUDA_SUBGROUP_RELATIVE_US)

  set(GPU_USL_FIXTURE_TARGET_CAPS ptx_6_0,sm_70)
  gpu_add_usl_fixtures(
    GPU_CUDA_ATOMIC32_USL_FIXTURE
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/compute_atomics.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_CUDA_ATOMIC32_USL_FIXTURE 0 GPU_CUDA_ATOMIC32_US)

  set(GPU_USL_FIXTURE_TARGET_CAPS
      ptx_6_0,sm_70,shader_subgroup_clock,shader_device_clock)
  gpu_add_usl_fixtures(
    GPU_CUDA_CLOCK_USL_FIXTURES
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/api/shader_subgroup_clock.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/shader_device_clock.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_CUDA_CLOCK_USL_FIXTURES 0 GPU_CUDA_SUBGROUP_CLOCK_US)
  list(GET GPU_CUDA_CLOCK_USL_FIXTURES 1 GPU_CUDA_DEVICE_CLOCK_US)

  set(GPU_USL_FIXTURE_TARGET_CAPS ptx_7_0,sm_80)
  gpu_add_usl_fixtures(
    GPU_CUDA_ASYNC_COPY_USL_FIXTURE
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/async_copy.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_CUDA_ASYNC_COPY_USL_FIXTURE 0 GPU_CUDA_ASYNC_COPY_US)
  add_custom_target(
    gpu-cuda-async-copy-fixture
    DEPENDS ${GPU_CUDA_ASYNC_COPY_USL_FIXTURE}
  )

  set(GPU_USL_FIXTURE_TARGET_CAPS
      ptx_6_3,sm_70,subgroup,shader_f16,subgroup_matrix)
  gpu_add_usl_fixtures(
    GPU_CUDA_SUBGROUP_MATRIX_USL_FIXTURE
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/subgroup-matrix-cuda-usl/subgroup_matrix.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_CUDA_SUBGROUP_MATRIX_USL_FIXTURE
       0
       GPU_CUDA_SUBGROUP_MATRIX_US)
  add_custom_target(
    gpu-cuda-subgroup-matrix-fixture
    DEPENDS ${GPU_CUDA_SUBGROUP_MATRIX_USL_FIXTURE}
  )

  set(GPU_USL_FIXTURE_TARGET_CAPS ptx_4_0,sm_50,atomic64)
  gpu_add_usl_fixtures(
    GPU_CUDA_ATOMIC64_USL_FIXTURE
    ptx
    validation
    "${PROJECT_SOURCE_DIR}/tests/api/atomic64.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_CUDA_ATOMIC64_USL_FIXTURE 0 GPU_CUDA_ATOMIC64_US)
  add_custom_target(
    gpu-cuda-atomic64-fixture
    DEPENDS ${GPU_CUDA_ATOMIC64_USL_FIXTURE}
  )
  add_custom_target(gpu-cuda-metadata-fixtures
    DEPENDS
      ${GPU_CUDA_METADATA_USL_FIXTURES}
      ${GPU_CUDA_DESCRIPTOR_ARRAY_USL_FIXTURES}
      ${GPU_CUDA_SUBGROUP_USL_FIXTURES}
      ${GPU_CUDA_ATOMIC32_USL_FIXTURE}
      ${GPU_CUDA_CLOCK_USL_FIXTURES}
      ${GPU_CUDA_ASYNC_COPY_USL_FIXTURE}
      ${GPU_CUDA_SUBGROUP_MATRIX_USL_FIXTURE}
      ${GPU_CUDA_ATOMIC64_USL_FIXTURE}
  )
endif()

if(GPU_BUILD_CUDA AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  set(GPU_CUDA_USL_FIXTURES
      ${GPU_CUDA_METADATA_USL_FIXTURES}
      ${GPU_CUDA_DESCRIPTOR_ARRAY_USL_FIXTURES}
      ${GPU_CUDA_SUBGROUP_USL_FIXTURES}
      ${GPU_CUDA_ATOMIC32_USL_FIXTURE}
      ${GPU_CUDA_CLOCK_USL_FIXTURES}
      ${GPU_CUDA_ASYNC_COPY_USL_FIXTURE}
      ${GPU_CUDA_SUBGROUP_MATRIX_USL_FIXTURE}
      ${GPU_CUDA_ATOMIC64_USL_FIXTURE})
  add_custom_target(gpu-cuda-fixtures DEPENDS ${GPU_CUDA_USL_FIXTURES})
endif()

if(GPU_BUILD_METAL AND GPU_BUILD_TESTS AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  gpu_add_usl_fixtures(
    GPU_METAL_ASYNC_COPY_USL_FIXTURE
    metal
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/async_copy.usl"
  )
  list(GET GPU_METAL_ASYNC_COPY_USL_FIXTURE 0 GPU_METAL_ASYNC_COPY_US)
  add_custom_target(
    gpu-metal-async-copy-fixture
    DEPENDS ${GPU_METAL_ASYNC_COPY_USL_FIXTURE}
  )
endif()

if(GPU_BUILD_METAL AND GPU_BUILD_SAMPLES AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  gpu_add_usl_fixtures(
    GPU_METAL_USL_FIXTURES
    metal
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/textured-quad-usl/textured_quad.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-usl/compute_visible.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-usl/compute_buffer.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/usl-reflection-check/reflection.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/usl-reflection-check/reflection_storage.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/compute_atomics.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
    "${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
  )
  list(GET GPU_METAL_USL_FIXTURES 0 GPU_METAL_TRIANGLE_US)
  list(GET GPU_METAL_USL_FIXTURES 1 GPU_METAL_TEXTURED_QUAD_US)
  list(GET GPU_METAL_USL_FIXTURES 2 GPU_METAL_COMPUTE_US)
  list(GET GPU_METAL_USL_FIXTURES 3 GPU_METAL_COMPUTE_BUFFER_US)
  list(GET GPU_METAL_USL_FIXTURES 4 GPU_METAL_REFLECTION_US)
  list(GET GPU_METAL_USL_FIXTURES 5 GPU_METAL_REFLECTION_STORAGE_US)
  list(GET GPU_METAL_USL_FIXTURES 6 GPU_METAL_MESH_TRIANGLE_US)
  list(GET GPU_METAL_USL_FIXTURES 7 GPU_METAL_COMPUTE_ATOMICS_US)
  list(GET GPU_METAL_USL_FIXTURES 8 GPU_METAL_TEXTURED_CUBE_US)
  list(GET GPU_METAL_USL_FIXTURES 9 GPU_METAL_MRT_BLEND_US)
  list(GET GPU_METAL_USL_FIXTURES 10 GPU_METAL_SHADOW_COMPARE_US)
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_METAL_BINDLESS_USL_FIXTURE
    metal
    validation
    "${PROJECT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(GET GPU_METAL_BINDLESS_USL_FIXTURE 0 GPU_METAL_BINDLESS_US)
  list(APPEND GPU_METAL_USL_FIXTURES ${GPU_METAL_BINDLESS_USL_FIXTURE})
  add_custom_target(gpu-metal-fixtures DEPENDS ${GPU_METAL_USL_FIXTURES})
endif()
