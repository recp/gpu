if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL)
  function(gpu_configure_metal_sample target)
    target_compile_options(${target} PRIVATE -fobjc-arc)
    target_link_libraries(${target} PRIVATE
      gpu
      ${GPU_FRAMEWORK_UI}
      ${GPU_FRAMEWORK_METAL}
      ${GPU_FRAMEWORK_FOUNDATION}
      ${GPU_FRAMEWORK_QUARTZCORE}
    )
  endfunction()
endif()

if(GPU_BUILD_TESTS)
  add_executable(gpu-adapter-enumeration-contract
    ${PROJECT_SOURCE_DIR}/tests/validation/adapter-enumeration/main.c
  )
  target_include_directories(gpu-adapter-enumeration-contract PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_link_libraries(gpu-adapter-enumeration-contract PRIVATE gpu)
  set_target_properties(gpu-adapter-enumeration-contract PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-adapter-enumeration-contract PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-adapter-enumeration-contract PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME adapter-enumeration-contract
           COMMAND gpu-adapter-enumeration-contract)

  add_executable(gpu-cuda-format-contract
    ${PROJECT_SOURCE_DIR}/src/backend/cuda/impl/format.c
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-format/main.c
  )
  target_include_directories(gpu-cuda-format-contract PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-cuda-format-contract PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-cuda-format-contract PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-cuda-format-contract PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME cuda-format-contract COMMAND gpu-cuda-format-contract)

  add_executable(gpu-cuda-texture-contract
    ${PROJECT_SOURCE_DIR}/src/backend/cuda/impl/texture_plan.c
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-texture/main.c
  )
  target_include_directories(gpu-cuda-texture-contract PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-cuda-texture-contract PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-cuda-texture-contract PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-cuda-texture-contract PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME cuda-texture-contract COMMAND gpu-cuda-texture-contract)

  add_executable(gpu-cuda-sampler-contract
    ${PROJECT_SOURCE_DIR}/src/backend/cuda/impl/sampler_plan.c
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-sampler/main.c
  )
  target_include_directories(gpu-cuda-sampler-contract PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src
  )
  if(TARGET us)
    target_link_libraries(gpu-cuda-sampler-contract PRIVATE us)
  elseif(TARGET gpu_usl)
    target_link_libraries(gpu-cuda-sampler-contract PRIVATE gpu_usl)
  endif()
  set_target_properties(gpu-cuda-sampler-contract PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-cuda-sampler-contract PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-cuda-sampler-contract PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME cuda-sampler-contract COMMAND gpu-cuda-sampler-contract)

  function(gpu_add_cuda_ptx_metadata_test name source artifact)
    set(target "gpu-cuda-${name}-metadata")
    add_executable(${target}
      ${PROJECT_SOURCE_DIR}/tests/validation/cuda-ptx-metadata/main.c
      ${source}
    )
    target_include_directories(${target} PRIVATE
      ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(${target} PRIVATE gpu)
    gpu_attach_usl_validation(${target}
                              gpu-cuda-metadata-fixtures
                              "${artifact}")
    set_target_properties(${target} PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(MSVC)
      target_compile_options(${target} PRIVATE /W4 /WX)
    else()
      target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME "cuda-${name}-metadata"
             COMMAND ${target} "${artifact}")
  endfunction()

  gpu_add_cuda_ptx_metadata_test(
    storage-texture
    ${PROJECT_SOURCE_DIR}/tests/validation/storage-texture-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_STORAGE_TEXTURE_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    sampled-texture
    ${PROJECT_SOURCE_DIR}/tests/validation/sampled-texture-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_SAMPLED_TEXTURE_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    sampled-format
    ${PROJECT_SOURCE_DIR}/tests/validation/sampled-format-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_SAMPLED_FORMAT_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    texture-geometry
    ${PROJECT_SOURCE_DIR}/tests/validation/texture-geometry-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_TEXTURE_GEOMETRY_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    storage-geometry
    ${PROJECT_SOURCE_DIR}/tests/validation/storage-geometry-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_STORAGE_GEOMETRY_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    compute-buffer
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_COMPUTE_BUFFER_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    profile-cache
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-ptx-metadata/profile_cache.c
    "${GPU_CUDA_COMPUTE_BUFFER_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    buffer-descriptor-array
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-ptx-metadata/buffer_array.c
    "${GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_US}"
  )
  target_compile_definitions(gpu-cuda-buffer-descriptor-array-metadata PRIVATE
    GPU_PTX_EXPECT_DYNAMIC=0
  )
  gpu_add_cuda_ptx_metadata_test(
    buffer-descriptor-array-dynamic
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-ptx-metadata/buffer_array.c
    "${GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_US}"
  )
  target_compile_definitions(
    gpu-cuda-buffer-descriptor-array-dynamic-metadata PRIVATE
    GPU_PTX_EXPECT_DYNAMIC=1
  )
  gpu_add_cuda_ptx_metadata_test(
    resource-descriptor-array
    ${PROJECT_SOURCE_DIR}/tests/validation/resource-descriptor-array-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_RESOURCE_DESCRIPTOR_ARRAY_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    subgroup
    ${PROJECT_SOURCE_DIR}/tests/validation/subgroup-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_SUBGROUP_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    subgroup-relative
    ${PROJECT_SOURCE_DIR}/tests/validation/subgroup-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_SUBGROUP_RELATIVE_US}"
  )
  target_compile_definitions(gpu-cuda-subgroup-relative-metadata PRIVATE
    GPU_PTX_SUBGROUP_RELATIVE=1
  )
  gpu_add_cuda_ptx_metadata_test(
    atomic32
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/ptx_metadata.c
    "${GPU_CUDA_ATOMIC32_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    subgroup-clock
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-ptx-metadata/clock.c
    "${GPU_CUDA_SUBGROUP_CLOCK_US}"
  )
  target_compile_definitions(gpu-cuda-subgroup-clock-metadata PRIVATE
    GPU_PTX_CLOCK_ENTRY="shader_subgroup_clock_cs"
    GPU_PTX_CLOCK_FEATURE=GPU_FEATURE_SHADER_SUBGROUP_CLOCK
    GPU_PTX_CLOCK_SOURCE="clock64"
  )
  gpu_add_cuda_ptx_metadata_test(
    device-clock
    ${PROJECT_SOURCE_DIR}/tests/validation/cuda-ptx-metadata/clock.c
    "${GPU_CUDA_DEVICE_CLOCK_US}"
  )
  target_compile_definitions(gpu-cuda-device-clock-metadata PRIVATE
    GPU_PTX_CLOCK_ENTRY="shader_device_clock_cs"
    GPU_PTX_CLOCK_FEATURE=GPU_FEATURE_SHADER_DEVICE_CLOCK
    GPU_PTX_CLOCK_SOURCE="globaltimer"
  )
  gpu_add_cuda_ptx_metadata_test(
    async-copy
    ${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/ptx_metadata.c
    "${GPU_CUDA_ASYNC_COPY_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    subgroup-matrix
    ${PROJECT_SOURCE_DIR}/tests/validation/subgroup-matrix-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_SUBGROUP_MATRIX_US}"
  )
  gpu_add_cuda_ptx_metadata_test(
    atomic64
    ${PROJECT_SOURCE_DIR}/tests/validation/atomic64-cuda-usl/ptx_metadata.c
    "${GPU_CUDA_ATOMIC64_US}"
  )
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gpu-triangle-manual
    ${PROJECT_SOURCE_DIR}/tests/validation/triangle-manual/main.m
  )
  gpu_configure_metal_sample(gpu-triangle-manual)
  set_target_properties(gpu-triangle-manual PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY
      "${PROJECT_BINARY_DIR}/validation/gpu-triangle-manual"
  )
  set(GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP
    "${PROJECT_BINARY_DIR}/CMakeFiles/gpu-triangle-manual-artifacts.stamp"
  )
  add_custom_command(
    OUTPUT "${GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:gpu-triangle-manual>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tests/validation/triangle-manual/triangle.usl.metal"
            $<TARGET_FILE_DIR:gpu-triangle-manual>
    COMMAND ${CMAKE_COMMAND} -E touch
            "${GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP}"
    DEPENDS
      "${PROJECT_SOURCE_DIR}/tests/validation/triangle-manual/triangle.usl.metal"
    VERBATIM
  )
  add_custom_target(gpu-triangle-manual-artifacts
    DEPENDS "${GPU_TRIANGLE_MANUAL_ARTIFACT_STAMP}"
  )
  add_dependencies(gpu-triangle-manual gpu-triangle-manual-artifacts)
  if(GPU_BUILD_TESTS)
    add_test(NAME metal-resize-lifecycle
             COMMAND gpu-triangle-manual)
    set_tests_properties(metal-resize-lifecycle PROPERTIES
      ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=4;GPU_SAMPLE_RESIZE_AFTER_FRAMES=1"
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-triangle-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/triangle-usl/main.m
  )
  gpu_configure_metal_sample(gpu-triangle-metal-usl)
  gpu_attach_usl_validation(gpu-triangle-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_TRIANGLE_US}")

  add_executable(gpu-textured-quad-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/textured-quad-usl/main.m
  )
  gpu_configure_metal_sample(gpu-textured-quad-metal-usl)
  gpu_attach_usl_validation(gpu-textured-quad-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_TEXTURED_QUAD_US}")

  add_executable(gpu-textured-cube-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/textured-cube-usl/main.m
  )
  target_include_directories(gpu-textured-cube-metal-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  gpu_configure_metal_sample(gpu-textured-cube-metal-usl)
  gpu_attach_usl_validation(gpu-textured-cube-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_TEXTURED_CUBE_US}")

  add_executable(gpu-mrt-blend-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/mrt-blend-usl/main.m
    ${PROJECT_SOURCE_DIR}/samples/common/MRTBlend.c
  )
  gpu_configure_metal_sample(gpu-mrt-blend-metal-usl)
  gpu_attach_usl_validation(gpu-mrt-blend-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_MRT_BLEND_US}")
  if(GPU_BUILD_TESTS)
    add_test(NAME metal-mrt-blend
             COMMAND gpu-mrt-blend-metal-usl)
    set_tests_properties(metal-mrt-blend PROPERTIES
      ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=96;GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-shadow-compare-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/shadow-compare-usl/main.m
    ${PROJECT_SOURCE_DIR}/samples/common/ShadowCompare.c
  )
  gpu_configure_metal_sample(gpu-shadow-compare-metal-usl)
  gpu_attach_usl_validation(gpu-shadow-compare-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_SHADOW_COMPARE_US}")
  if(GPU_BUILD_TESTS)
    add_test(NAME metal-shadow-compare
             COMMAND gpu-shadow-compare-metal-usl)
    set_tests_properties(metal-shadow-compare PROPERTIES
      ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=96;GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-bindless-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/bindless-usl/main.m
    ${PROJECT_SOURCE_DIR}/samples/common/BindlessTexture.c
  )
  gpu_configure_metal_sample(gpu-bindless-metal-usl)
  gpu_attach_usl_validation(gpu-bindless-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_BINDLESS_US}")
  if(GPU_BUILD_TESTS)
    add_test(NAME metal-bindless
             COMMAND gpu-bindless-metal-usl)
    set_tests_properties(metal-bindless PROPERTIES
      ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=96;GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-mesh-triangle-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/main.m
    ${PROJECT_SOURCE_DIR}/samples/common/MeshTriangle.c
  )
  gpu_configure_metal_sample(gpu-mesh-triangle-metal-usl)
  gpu_attach_usl_validation(gpu-mesh-triangle-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_MESH_TRIANGLE_US}")
  if(GPU_BUILD_TESTS)
    add_test(NAME metal-mesh-triangle
             COMMAND gpu-mesh-triangle-metal-usl)
    set_tests_properties(metal-mesh-triangle PROPERTIES
      ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=96;GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-compute-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-usl/main.m
  )
  gpu_configure_metal_sample(gpu-compute-metal-usl)
  gpu_attach_usl_validation(gpu-compute-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_COMPUTE_US}")

  add_executable(gpu-compute-buffer-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-usl/main.m
  )
  gpu_configure_metal_sample(gpu-compute-buffer-metal-usl)
  gpu_attach_usl_validation(gpu-compute-buffer-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_COMPUTE_BUFFER_US}")

  add_executable(gpu-compute-atomics-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/main.c
  )
  target_compile_definitions(gpu-compute-atomics-metal-usl PRIVATE
    GPU_SAMPLE_BACKEND=GPU_BACKEND_METAL
  )
  target_link_libraries(gpu-compute-atomics-metal-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-compute-atomics-metal-usl
                        gpu-metal-fixtures
                        "${GPU_METAL_COMPUTE_ATOMICS_US}")
  set_target_properties(gpu-compute-atomics-metal-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME metal-compute-atomics
      COMMAND gpu-compute-atomics-metal-usl
              "${GPU_METAL_COMPUTE_ATOMICS_US}"
    )
  endif()

  add_executable(gpu-usl-reflection-check
    ${PROJECT_SOURCE_DIR}/tests/validation/usl-reflection-check/main.c
  )
  target_link_libraries(gpu-usl-reflection-check PRIVATE gpu)
  gpu_attach_usl_validation(gpu-usl-reflection-check
                        gpu-metal-fixtures
                        "${GPU_METAL_REFLECTION_US}"
                        "${GPU_METAL_REFLECTION_STORAGE_US}")
  set_target_properties(gpu-usl-reflection-check PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL AND
   CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(GPU_USL_HOST_FIXTURE "" CACHE FILEPATH
      "Host gpu-usl-fixture executable used for cross-compiled samples")
  if(NOT GPU_USL_HOST_FIXTURE)
    find_program(GPU_USL_HOST_FIXTURE_AUTO
      NAMES gpu-usl-fixture
      HINTS
        "${PROJECT_SOURCE_DIR}/out/build/macos-debug"
        "${PROJECT_SOURCE_DIR}/out/build/macos-release"
    )
    if(GPU_USL_HOST_FIXTURE_AUTO)
      set(GPU_USL_HOST_FIXTURE "${GPU_USL_HOST_FIXTURE_AUTO}")
    endif()
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_FIXTURE}")
    message(FATAL_ERROR
      "iOS samples require a host gpu-usl-fixture. Build the macOS sample "
      "fixtures first or set GPU_USL_HOST_FIXTURE.")
  endif()

  set(GPU_IOS_TRIANGLE_DIR
      "${PROJECT_BINARY_DIR}/usl/metal/ios-triangle")
  set(GPU_IOS_TRIANGLE_SOURCE
      "${GPU_IOS_TRIANGLE_DIR}/triangle.usl")
  set(GPU_IOS_TRIANGLE_US
      "${GPU_IOS_TRIANGLE_DIR}/triangle.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_TRIANGLE_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_IOS_TRIANGLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
            "${GPU_IOS_TRIANGLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_TRIANGLE_SOURCE}"
    DEPENDS
      "${PROJECT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_TRIANGLE_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-triangle-ios-usl MACOSX_BUNDLE
    ${PROJECT_SOURCE_DIR}/tests/validation/triangle-ios-usl/main.m
    "${GPU_IOS_TRIANGLE_US}"
  )
  gpu_configure_metal_sample(gpu-triangle-ios-usl)
  target_compile_options(gpu-triangle-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-triangle-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${PROJECT_SOURCE_DIR}/tests/validation/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "gpu.samples.triangle-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_MESH_TRIANGLE_DIR
      "${PROJECT_BINARY_DIR}/usl/metal/ios-mesh-triangle")
  set(GPU_IOS_MESH_TRIANGLE_SOURCE
      "${GPU_IOS_MESH_TRIANGLE_DIR}/mesh_triangle.usl")
  set(GPU_IOS_MESH_TRIANGLE_US
      "${GPU_IOS_MESH_TRIANGLE_DIR}/mesh_triangle.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_MESH_TRIANGLE_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_IOS_MESH_TRIANGLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
            "${GPU_IOS_MESH_TRIANGLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_MESH_TRIANGLE_SOURCE}"
    DEPENDS
      "${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/mesh_triangle.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_MESH_TRIANGLE_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-mesh-triangle-ios-usl MACOSX_BUNDLE
    ${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-ios-usl/main.m
    "${GPU_IOS_MESH_TRIANGLE_US}"
  )
  gpu_configure_metal_sample(gpu-mesh-triangle-ios-usl)
  target_compile_options(gpu-mesh-triangle-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-mesh-triangle-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${PROJECT_SOURCE_DIR}/tests/validation/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "gpu.samples.mesh-triangle-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_RAY_QUERY_DIR
      "${PROJECT_BINARY_DIR}/usl/metal/ios-ray-query")
  set(GPU_IOS_RAY_QUERY_SOURCE
      "${GPU_IOS_RAY_QUERY_DIR}/ray_query.usl")
  set(GPU_IOS_RAY_QUERY_US
      "${GPU_IOS_RAY_QUERY_DIR}/ray_query.us")
  set(GPU_IOS_INTERSECTION_FUNCTION_SOURCE
      "${GPU_IOS_RAY_QUERY_DIR}/intersection_function.usl")
  set(GPU_IOS_INTERSECTION_FUNCTION_US
      "${GPU_IOS_RAY_QUERY_DIR}/intersection_function.us")
  add_custom_command(
    OUTPUT
      "${GPU_IOS_RAY_QUERY_US}"
      "${GPU_IOS_INTERSECTION_FUNCTION_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_IOS_RAY_QUERY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tests/api/ray_query.usl"
            "${GPU_IOS_RAY_QUERY_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tests/api/intersection_function.usl"
            "${GPU_IOS_INTERSECTION_FUNCTION_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            USL_TARGET_CAPS=ray_query
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_RAY_QUERY_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            USL_TARGET_CAPS=ray_query,intersection_function_table
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_INTERSECTION_FUNCTION_SOURCE}"
    DEPENDS
      "${PROJECT_SOURCE_DIR}/tests/api/ray_query.usl"
      "${PROJECT_SOURCE_DIR}/tests/api/intersection_function.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties(
    "${GPU_IOS_RAY_QUERY_US}"
    "${GPU_IOS_INTERSECTION_FUNCTION_US}"
    PROPERTIES
      GENERATED TRUE
      MACOSX_PACKAGE_LOCATION Resources
    )

  add_executable(gpu-ray-query-ios-usl MACOSX_BUNDLE
    ${PROJECT_SOURCE_DIR}/tests/validation/ray-query-ios-usl/main.m
    ${PROJECT_SOURCE_DIR}/tests/api/ray.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
    "${GPU_IOS_RAY_QUERY_US}"
    "${GPU_IOS_INTERSECTION_FUNCTION_US}"
  )
  gpu_configure_metal_sample(gpu-ray-query-ios-usl)
  target_compile_options(gpu-ray-query-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-ray-query-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${PROJECT_SOURCE_DIR}/tests/validation/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "gpu.samples.ray-query-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_VRS_DIR
      "${PROJECT_BINARY_DIR}/usl/metal/ios-vrs")
  set(GPU_IOS_VRS_SOURCE
      "${GPU_IOS_VRS_DIR}/vrs.usl")
  set(GPU_IOS_VRS_US
      "${GPU_IOS_VRS_DIR}/vrs.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_VRS_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_IOS_VRS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tests/validation/vrs-ios-usl/vrs.usl"
            "${GPU_IOS_VRS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_VRS_SOURCE}"
    DEPENDS
      "${PROJECT_SOURCE_DIR}/tests/validation/vrs-ios-usl/vrs.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_VRS_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-vrs-ios-usl MACOSX_BUNDLE
    ${PROJECT_SOURCE_DIR}/tests/validation/vrs-ios-usl/main.m
    "${GPU_IOS_VRS_US}"
  )
  gpu_configure_metal_sample(gpu-vrs-ios-usl)
  target_compile_options(gpu-vrs-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-vrs-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${PROJECT_SOURCE_DIR}/tests/validation/triangle-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "gpu.samples.vrs-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )

  set(GPU_IOS_METAL4_BINDLESS_DIR
      "${PROJECT_BINARY_DIR}/usl/metal/ios-metal4-bindless")
  set(GPU_IOS_METAL4_BINDLESS_SOURCE
      "${GPU_IOS_METAL4_BINDLESS_DIR}/metal4_bindless.usl")
  set(GPU_IOS_METAL4_BINDLESS_US
      "${GPU_IOS_METAL4_BINDLESS_DIR}/metal4_bindless.us")
  add_custom_command(
    OUTPUT "${GPU_IOS_METAL4_BINDLESS_US}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_IOS_METAL4_BINDLESS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
            "${GPU_IOS_METAL4_BINDLESS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_NO_BACKEND_SIDECAR=1
            USL_TARGET_CAPS=descriptor_indexing
            "${GPU_USL_HOST_FIXTURE}"
            metal
            "${GPU_IOS_METAL4_BINDLESS_SOURCE}"
    DEPENDS
      "${PROJECT_SOURCE_DIR}/tests/validation/bindless-usl/bindless.usl"
      "${GPU_USL_HOST_FIXTURE}"
    VERBATIM
  )
  set_source_files_properties("${GPU_IOS_METAL4_BINDLESS_US}" PROPERTIES
    GENERATED TRUE
    MACOSX_PACKAGE_LOCATION Resources
  )

  add_executable(gpu-metal4-bindless-ios-usl MACOSX_BUNDLE
    ${PROJECT_SOURCE_DIR}/tests/validation/metal4-bindless-ios-usl/main.m
    "${GPU_IOS_METAL4_BINDLESS_US}"
  )
  gpu_configure_metal_sample(gpu-metal4-bindless-ios-usl)
  target_compile_options(gpu-metal4-bindless-ios-usl PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-metal4-bindless-ios-usl PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      "${PROJECT_SOURCE_DIR}/tests/validation/metal4-bindless-ios-usl/Info.plist"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "gpu.samples.metal4-bindless-ios-usl"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_DX12)
  add_executable(gpu-triangle-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/triangle-dx12-usl/main.c
  )
  target_link_libraries(gpu-triangle-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-triangle-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_TRIANGLE_US}")
  set_target_properties(gpu-triangle-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-textured-quad-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/textured-quad-dx12-usl/main.c
  )
  target_link_libraries(gpu-textured-quad-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-textured-quad-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_TEXTURED_QUAD_US}")
  set_target_properties(gpu-textured-quad-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-shadow-compare-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/shadow-compare-dx12-usl/main.c
    ${PROJECT_SOURCE_DIR}/samples/common/ShadowCompare.c
  )
  target_link_libraries(gpu-shadow-compare-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-shadow-compare-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_SHADOW_COMPARE_US}")
  set_target_properties(gpu-shadow-compare-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-vrs-compare-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/vrs-compare-dx12-usl/main.c
    ${PROJECT_SOURCE_DIR}/samples/common/VRSCompare.c
  )
  target_link_libraries(gpu-vrs-compare-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-vrs-compare-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_VRS_COMPARE_US}")
  set_target_properties(gpu-vrs-compare-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-vrs-attachment-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/vrs-compare-dx12-usl/main.c
    ${PROJECT_SOURCE_DIR}/samples/common/VRSCompare.c
  )
  target_compile_definitions(gpu-vrs-attachment-dx12-usl PRIVATE
    GPU_SAMPLE_VRS_ATTACHMENT=1
  )
  target_link_libraries(gpu-vrs-attachment-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-vrs-attachment-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_VRS_COMPARE_US}")
  set_target_properties(gpu-vrs-attachment-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-textured-cube-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/textured-cube-dx12-usl/main.c
  )
  target_include_directories(gpu-textured-cube-dx12-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  target_link_libraries(gpu-textured-cube-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-textured-cube-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_TEXTURED_CUBE_US}")
  set_target_properties(gpu-textured-cube-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-mrt-blend-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/mrt-blend-dx12-usl/main.c
    ${PROJECT_SOURCE_DIR}/samples/common/MRTBlend.c
  )
  target_link_libraries(gpu-mrt-blend-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-mrt-blend-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_MRT_BLEND_US}")
  set_target_properties(gpu-mrt-blend-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-bindless-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/bindless-dx12-usl/main.c
    ${PROJECT_SOURCE_DIR}/samples/common/BindlessTexture.c
  )
  target_link_libraries(gpu-bindless-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-bindless-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_BINDLESS_US}")
  set_target_properties(gpu-bindless-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-mesh-triangle-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-dx12-usl/main.c
    ${PROJECT_SOURCE_DIR}/samples/common/MeshTriangle.c
  )
  target_link_libraries(gpu-mesh-triangle-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-mesh-triangle-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_MESH_TRIANGLE_US}")
  set_target_properties(gpu-mesh-triangle-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-buffer-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-dx12-usl/main.c
  )
  target_link_libraries(gpu-compute-buffer-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-compute-buffer-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_COMPUTE_BUFFER_US}")
  set_target_properties(gpu-compute-buffer-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-matrix-elementwise-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/matrix-elementwise-dx12-usl/main.c
  )
  target_link_libraries(gpu-matrix-elementwise-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-matrix-elementwise-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_MATRIX_ELEMENTWISE_US}")
  set_target_properties(gpu-matrix-elementwise-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-atomics-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/main.c
  )
  target_compile_definitions(gpu-compute-atomics-dx12-usl PRIVATE
    GPU_SAMPLE_BACKEND=GPU_BACKEND_DX12
  )
  target_link_libraries(gpu-compute-atomics-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-compute-atomics-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_COMPUTE_ATOMICS_US}")
  set_target_properties(gpu-compute-atomics-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-render-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-dx12-usl/window.c
  )
  target_link_libraries(gpu-compute-render-dx12-usl PRIVATE gpu user32)
  gpu_attach_usl_validation(gpu-compute-render-dx12-usl
                        gpu-dx12-fixtures
                        "${GPU_DX12_COMPUTE_BUFFER_US}")
  set_target_properties(gpu-compute-render-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  if(GPU_BUILD_TESTS)
    add_test(
      NAME dx12-compute-atomics
      COMMAND gpu-compute-atomics-dx12-usl
              "${GPU_DX12_COMPUTE_ATOMICS_US}"
    )
    add_test(
      NAME dx12-native-dxil-compute-atomics
      COMMAND gpu-compute-atomics-dx12-usl
              "${GPU_DX12_COMPUTE_ATOMICS_US}"
    )
    set_tests_properties(dx12-native-dxil-compute-atomics PROPERTIES
      ENVIRONMENT
        "GPU_DX12_NATIVE_DXIL=1;GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
    )
    add_test(
      NAME dx12-compute-buffer
      COMMAND gpu-compute-buffer-dx12-usl
              "${GPU_DX12_COMPUTE_BUFFER_US}"
    )
    add_test(
      NAME dx12-native-dxil-compute-buffer
      COMMAND gpu-compute-buffer-dx12-usl
              "${GPU_DX12_COMPUTE_BUFFER_US}"
    )
    set_tests_properties(dx12-native-dxil-compute-buffer PROPERTIES
      ENVIRONMENT
        "GPU_DX12_NATIVE_DXIL=1;GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
    )
    add_test(
      NAME dx12-native-dxil-matrix-elementwise
      COMMAND gpu-matrix-elementwise-dx12-usl
              "${GPU_DX12_MATRIX_ELEMENTWISE_US}"
    )
    set_tests_properties(dx12-native-dxil-matrix-elementwise PROPERTIES
      ENVIRONMENT
        "GPU_DX12_NATIVE_DXIL=1;GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
    )
    add_test(NAME dx12-triangle
             COMMAND gpu-triangle-dx12-usl)
    add_test(NAME dx12-textured-quad
             COMMAND gpu-textured-quad-dx12-usl)
    add_test(NAME dx12-shadow-compare
             COMMAND gpu-shadow-compare-dx12-usl)
    add_test(NAME dx12-vrs-compare
             COMMAND gpu-vrs-compare-dx12-usl)
    add_test(NAME dx12-vrs-attachment
             COMMAND gpu-vrs-attachment-dx12-usl)
    add_test(NAME dx12-textured-cube
             COMMAND gpu-textured-cube-dx12-usl)
    add_test(NAME dx12-mrt-blend
             COMMAND gpu-mrt-blend-dx12-usl)
    add_test(NAME dx12-bindless
             COMMAND gpu-bindless-dx12-usl)
    add_test(NAME dx12-mesh-triangle
             COMMAND gpu-mesh-triangle-dx12-usl)
    add_test(NAME dx12-compute-render
             COMMAND gpu-compute-render-dx12-usl)
    set_tests_properties(
      dx12-triangle
      dx12-textured-quad
      dx12-shadow-compare
      dx12-vrs-compare
      dx12-vrs-attachment
      dx12-textured-cube
      dx12-mrt-blend
      dx12-bindless
      dx12-mesh-triangle
      dx12-compute-render
      PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 30
    )
    set_property(
      TEST dx12-triangle dx12-textured-quad dx12-shadow-compare
           dx12-vrs-compare dx12-vrs-attachment
           dx12-textured-cube
           dx12-mrt-blend dx12-bindless dx12-mesh-triangle
           dx12-compute-render
      PROPERTY ENVIRONMENT
        "GPU_SAMPLE_EXIT_AFTER_FRAMES=96"
        "GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
        "GPU_SAMPLE_SKIP_NONINTERACTIVE=1"
    )
  endif()
endif()

if(GPU_BUILD_DX12 AND GPU_BUILD_TESTS)
  add_executable(gpu-f64-core-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/f64-core-dx12-usl/main.c
  )
  target_compile_definitions(gpu-f64-core-dx12-usl PRIVATE
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(gpu-f64-core-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-f64-core-dx12-usl
                            gpu-dx12-fixtures
                            "${GPU_DX12_F64_CORE_US}")
  set_target_properties(gpu-f64-core-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-f64-core-dx12-usl PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-f64-core-dx12-usl PRIVATE
      -Wall -Wextra -Werror
    )
  endif()
  add_test(
    NAME dx12-native-dxil-f64-core
    COMMAND gpu-f64-core-dx12-usl
            "${GPU_DX12_F64_CORE_US}"
  )
  set_tests_properties(dx12-native-dxil-f64-core PROPERTIES
    ENVIRONMENT
      "GPU_DX12_NATIVE_DXIL=1;GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
  )

  add_executable(gpu-f64-storage-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/f64-storage-dx12-usl/main.c
  )
  target_compile_definitions(gpu-f64-storage-dx12-usl PRIVATE
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(gpu-f64-storage-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-f64-storage-dx12-usl
                            gpu-dx12-fixtures
                            "${GPU_DX12_F64_STORAGE_US}")
  set_target_properties(gpu-f64-storage-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-f64-storage-dx12-usl PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-f64-storage-dx12-usl PRIVATE
      -Wall -Wextra -Werror
    )
  endif()
  add_test(
    NAME dx12-native-dxil-f64-storage
    COMMAND gpu-f64-storage-dx12-usl
            "${GPU_DX12_F64_STORAGE_US}"
  )
  set_tests_properties(dx12-native-dxil-f64-storage PROPERTIES
    ENVIRONMENT
      "GPU_DX12_NATIVE_DXIL=1;GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
  )

  add_executable(gpu-f64-math-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/f64-math-dx12-usl/main.c
  )
  target_compile_definitions(gpu-f64-math-dx12-usl PRIVATE
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(gpu-f64-math-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-f64-math-dx12-usl
                            gpu-dx12-fixtures
                            "${GPU_DX12_F64_MATH_US}"
                            "${GPU_DX12_F64_MATH_HLSL_US}")
  set_target_properties(gpu-f64-math-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-f64-math-dx12-usl PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-f64-math-dx12-usl PRIVATE
      -Wall -Wextra -Werror
    )
  endif()
  add_test(
    NAME dx12-native-dxil-f64-math
    COMMAND gpu-f64-math-dx12-usl
            "${GPU_DX12_F64_MATH_US}"
  )
  set_tests_properties(dx12-native-dxil-f64-math PROPERTIES
    ENVIRONMENT
      "GPU_DX12_NATIVE_DXIL=1;GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
  )
  add_test(
    NAME dx12-hlsl-f64-math
    COMMAND gpu-f64-math-dx12-usl
            "${GPU_DX12_F64_MATH_HLSL_US}"
  )
  set_tests_properties(dx12-hlsl-f64-math PROPERTIES
    ENVIRONMENT
      "GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
  )

  add_executable(gpu-f64-exponential-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/f64-exponential-dx12-usl/main.c
  )
  target_compile_definitions(gpu-f64-exponential-dx12-usl PRIVATE
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(gpu-f64-exponential-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-f64-exponential-dx12-usl
                            gpu-dx12-fixtures
                            "${GPU_DX12_F64_EXPONENTIAL_US}")
  set_target_properties(gpu-f64-exponential-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-f64-exponential-dx12-usl PRIVATE /W4 /WX)
  else()
    target_compile_options(gpu-f64-exponential-dx12-usl PRIVATE
      -Wall -Wextra -Werror
    )
  endif()
  add_test(
    NAME dx12-hlsl-f64-exponential
    COMMAND gpu-f64-exponential-dx12-usl
            "${GPU_DX12_F64_EXPONENTIAL_US}"
  )
  set_tests_properties(dx12-hlsl-f64-exponential PROPERTIES
    ENVIRONMENT
      "GPU_USL_LOG=1;GPU_USL_TEST_DISABLE_DISK_CACHE=1"
  )

  add_executable(gpu-async-copy-dx12-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/main.c
  )
  target_compile_definitions(gpu-async-copy-dx12-usl PRIVATE
    GPU_ASYNC_COPY_BACKEND=GPU_BACKEND_DX12
  )
  target_link_libraries(gpu-async-copy-dx12-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-async-copy-dx12-usl
                            gpu-dx12-fixtures
                            "${GPU_DX12_ASYNC_COPY_US}")
  set_target_properties(gpu-async-copy-dx12-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  add_test(
    NAME dx12-async-copy
    COMMAND gpu-async-copy-dx12-usl
            "${GPU_DX12_ASYNC_COPY_US}"
  )
  set_tests_properties(dx12-async-copy PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 30
  )
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_VULKAN)
  add_executable(gpu-compute-buffer-vulkan-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-vulkan-usl/main.c
  )
  target_link_libraries(gpu-compute-buffer-vulkan-usl PRIVATE gpu)
  if(NOT WIN32)
    target_link_libraries(gpu-compute-buffer-vulkan-usl PRIVATE m)
  endif()
  gpu_attach_usl_validation(gpu-compute-buffer-vulkan-usl
                        gpu-vulkan-fixtures
                        "${GPU_VULKAN_COMPUTE_BUFFER_US}")
  set_target_properties(gpu-compute-buffer-vulkan-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  add_executable(gpu-compute-atomics-vulkan-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/main.c
  )
  target_compile_definitions(gpu-compute-atomics-vulkan-usl PRIVATE
    GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
  )
  target_link_libraries(gpu-compute-atomics-vulkan-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-compute-atomics-vulkan-usl
                        gpu-vulkan-fixtures
                        "${GPU_VULKAN_COMPUTE_ATOMICS_US}")
  set_target_properties(gpu-compute-atomics-vulkan-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  if(GPU_BUILD_TESTS)
    add_test(
      NAME vulkan-compute-atomics
      COMMAND gpu-compute-atomics-vulkan-usl
              "${GPU_VULKAN_COMPUTE_ATOMICS_US}"
    )
    add_test(
      NAME vulkan-compute-buffer
      COMMAND gpu-compute-buffer-vulkan-usl
              "${GPU_VULKAN_COMPUTE_BUFFER_US}"
    )
  endif()

  if(APPLE)
    add_executable(gpu-triangle-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/triangle-vulkan-usl/main.m
    )
    target_compile_options(gpu-triangle-vulkan-usl PRIVATE -fobjc-arc)
    target_link_libraries(gpu-triangle-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-triangle-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_TRIANGLE_US}")

    add_executable(gpu-textured-quad-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/textured-quad-vulkan-usl/main.m
    )
    target_compile_options(gpu-textured-quad-vulkan-usl PRIVATE -fobjc-arc)
    target_link_libraries(gpu-textured-quad-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-textured-quad-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_TEXTURED_QUAD_US}")

    add_executable(gpu-shadow-compare-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/shadow-compare-usl/main.m
      ${PROJECT_SOURCE_DIR}/samples/common/ShadowCompare.c
    )
    target_compile_options(gpu-shadow-compare-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-shadow-compare-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_link_libraries(gpu-shadow-compare-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-shadow-compare-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_SHADOW_COMPARE_US}")

    add_executable(gpu-vrs-compare-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/vrs-compare-usl/main.m
      ${PROJECT_SOURCE_DIR}/samples/common/VRSCompare.c
    )
    target_compile_options(gpu-vrs-compare-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-vrs-compare-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_link_libraries(gpu-vrs-compare-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-vrs-compare-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_VRS_COMPARE_US}")

    add_executable(gpu-vrs-attachment-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/vrs-compare-usl/main.m
      ${PROJECT_SOURCE_DIR}/samples/common/VRSCompare.c
    )
    target_compile_options(gpu-vrs-attachment-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-vrs-attachment-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
      GPU_SAMPLE_VRS_ATTACHMENT=1
    )
    target_link_libraries(gpu-vrs-attachment-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-vrs-attachment-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_VRS_COMPARE_US}")

    add_executable(gpu-textured-cube-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/textured-cube-usl/main.m
    )
    target_compile_options(gpu-textured-cube-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-textured-cube-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_include_directories(gpu-textured-cube-vulkan-usl PRIVATE
      "${GPU_CGLM_INCLUDE_DIR}"
    )
    target_link_libraries(gpu-textured-cube-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-textured-cube-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_TEXTURED_CUBE_US}")

    add_executable(gpu-mrt-blend-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/mrt-blend-usl/main.m
      ${PROJECT_SOURCE_DIR}/samples/common/MRTBlend.c
    )
    target_compile_options(gpu-mrt-blend-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-mrt-blend-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_link_libraries(gpu-mrt-blend-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-mrt-blend-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_MRT_BLEND_US}")

    add_executable(gpu-bindless-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/bindless-usl/main.m
      ${PROJECT_SOURCE_DIR}/samples/common/BindlessTexture.c
    )
    target_compile_options(gpu-bindless-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-bindless-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_link_libraries(gpu-bindless-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-bindless-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_BINDLESS_US}")

    add_executable(gpu-mesh-triangle-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/mesh-triangle-usl/main.m
      ${PROJECT_SOURCE_DIR}/samples/common/MeshTriangle.c
    )
    target_compile_options(gpu-mesh-triangle-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-mesh-triangle-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_link_libraries(gpu-mesh-triangle-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-mesh-triangle-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_MESH_TRIANGLE_US}")

    add_executable(gpu-compute-render-vulkan-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-usl/main.m
    )
    target_compile_options(gpu-compute-render-vulkan-usl PRIVATE -fobjc-arc)
    target_compile_definitions(gpu-compute-render-vulkan-usl PRIVATE
      GPU_SAMPLE_BACKEND=GPU_BACKEND_VULKAN
    )
    target_link_libraries(gpu-compute-render-vulkan-usl PRIVATE
      gpu
      "-framework AppKit"
      "-framework Foundation"
      "-framework QuartzCore"
    )
    gpu_attach_usl_validation(gpu-compute-render-vulkan-usl
                          gpu-vulkan-fixtures
                          "${GPU_VULKAN_COMPUTE_BUFFER_US}")

    if(GPU_BUILD_TESTS)
      add_test(NAME vulkan-triangle
               COMMAND gpu-triangle-vulkan-usl)
      add_test(NAME vulkan-textured-quad
               COMMAND gpu-textured-quad-vulkan-usl)
      add_test(NAME vulkan-shadow-compare
               COMMAND gpu-shadow-compare-vulkan-usl)
      add_test(NAME vulkan-vrs-compare
               COMMAND gpu-vrs-compare-vulkan-usl)
      add_test(NAME vulkan-vrs-attachment
               COMMAND gpu-vrs-attachment-vulkan-usl)
      add_test(NAME vulkan-textured-cube
               COMMAND gpu-textured-cube-vulkan-usl)
      add_test(NAME vulkan-mrt-blend
               COMMAND gpu-mrt-blend-vulkan-usl)
      add_test(NAME vulkan-bindless
               COMMAND gpu-bindless-vulkan-usl)
      add_test(NAME vulkan-mesh-triangle
               COMMAND gpu-mesh-triangle-vulkan-usl)
      add_test(NAME vulkan-compute-render
               COMMAND gpu-compute-render-vulkan-usl)
      add_test(NAME vulkan-resize-lifecycle
               COMMAND gpu-triangle-vulkan-usl)
      set_tests_properties(
        vulkan-triangle
        vulkan-textured-quad
        vulkan-shadow-compare
        vulkan-vrs-compare
        vulkan-vrs-attachment
        vulkan-textured-cube
        vulkan-mrt-blend
        vulkan-bindless
        vulkan-mesh-triangle
        vulkan-compute-render
        PROPERTIES
          ENVIRONMENT
            "GPU_SAMPLE_EXIT_AFTER_FRAMES=96;GPU_SAMPLE_ASSERT_ZERO_ALLOC=1"
          SKIP_RETURN_CODE 77
          TIMEOUT 30
      )
      set_tests_properties(vulkan-resize-lifecycle PROPERTIES
        ENVIRONMENT
          "GPU_SAMPLE_EXIT_AFTER_FRAMES=4;GPU_SAMPLE_RESIZE_AFTER_FRAMES=1"
        TIMEOUT 30
      )
    endif()
  endif()
endif()

if(GPU_BUILD_VULKAN AND GPU_BUILD_TESTS)
  add_executable(gpu-async-copy-vulkan-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/main.c
  )
  target_compile_definitions(gpu-async-copy-vulkan-usl PRIVATE
    GPU_ASYNC_COPY_BACKEND=GPU_BACKEND_VULKAN
  )
  target_link_libraries(gpu-async-copy-vulkan-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-async-copy-vulkan-usl
                            gpu-vulkan-fixtures
                            "${GPU_VULKAN_ASYNC_COPY_US}")
  set_target_properties(gpu-async-copy-vulkan-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  add_test(
    NAME vulkan-async-copy
    COMMAND gpu-async-copy-vulkan-usl
            "${GPU_VULKAN_ASYNC_COPY_US}"
  )
  set_tests_properties(vulkan-async-copy PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 30
  )
endif()

if(GPU_BUILD_METAL AND GPU_BUILD_TESTS AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gpu-async-copy-metal-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/main.c
  )
  target_compile_definitions(gpu-async-copy-metal-usl PRIVATE
    GPU_ASYNC_COPY_BACKEND=GPU_BACKEND_METAL
  )
  target_link_libraries(gpu-async-copy-metal-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-async-copy-metal-usl
                            gpu-metal-async-copy-fixture
                            "${GPU_METAL_ASYNC_COPY_US}")
  set_target_properties(gpu-async-copy-metal-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  add_test(
    NAME metal-async-copy
    COMMAND gpu-async-copy-metal-usl
            "${GPU_METAL_ASYNC_COPY_US}"
  )
  set_tests_properties(metal-async-copy PROPERTIES
    SKIP_RETURN_CODE 77
    TIMEOUT 30
  )
endif()

if(GPU_BUILD_CUDA AND (GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES))
  add_executable(gpu-compute-buffer-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-buffer-cuda-usl/main.c
  )
  target_link_libraries(gpu-compute-buffer-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-compute-buffer-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_COMPUTE_BUFFER_US}")
  set_target_properties(gpu-compute-buffer-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-compute-buffer
      COMMAND gpu-compute-buffer-cuda-usl
              "${GPU_CUDA_COMPUTE_BUFFER_US}"
    )
    set_tests_properties(cuda-compute-buffer PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-compute-atomics-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/compute-atomics-usl/main.c
  )
  target_compile_definitions(gpu-compute-atomics-cuda-usl PRIVATE
    GPU_SAMPLE_BACKEND=GPU_BACKEND_CUDA
    GPU_SAMPLE_SKIP_MISSING_ADAPTER=1
  )
  target_link_libraries(gpu-compute-atomics-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-compute-atomics-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_ATOMIC32_US}")
  set_target_properties(gpu-compute-atomics-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-compute-atomics
      COMMAND gpu-compute-atomics-cuda-usl "${GPU_CUDA_ATOMIC32_US}"
    )
    set_tests_properties(cuda-compute-atomics PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-subgroup-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/subgroup-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/api/subgroup.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
  )
  target_include_directories(gpu-subgroup-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/api
  )
  target_link_libraries(gpu-subgroup-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-subgroup-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_SUBGROUP_US}")
  set_target_properties(gpu-subgroup-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  gpu_enable_c11_atomics(gpu-subgroup-cuda-usl)
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-subgroup
      COMMAND gpu-subgroup-cuda-usl
              "${GPU_CUDA_SUBGROUP_US}"
              "${GPU_CUDA_SUBGROUP_RELATIVE_US}"
    )
    set_tests_properties(cuda-subgroup PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-async-copy-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/async-copy-usl/main.c
  )
  target_compile_definitions(gpu-async-copy-cuda-usl PRIVATE
    GPU_ASYNC_COPY_BACKEND=GPU_BACKEND_CUDA
  )
  target_link_libraries(gpu-async-copy-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-async-copy-cuda-usl
                            gpu-cuda-async-copy-fixture
                            "${GPU_CUDA_ASYNC_COPY_US}")
  set_target_properties(gpu-async-copy-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-async-copy
      COMMAND gpu-async-copy-cuda-usl
              "${GPU_CUDA_ASYNC_COPY_US}"
    )
    set_tests_properties(cuda-async-copy PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-storage-texture-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/storage-texture-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/validation/storage-texture-cuda-usl/ptx_metadata.c
  )
  target_include_directories(gpu-storage-texture-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_link_libraries(gpu-storage-texture-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-storage-texture-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_STORAGE_TEXTURE_US}")
  set_target_properties(gpu-storage-texture-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-storage-texture
      COMMAND gpu-storage-texture-cuda-usl
              "${GPU_CUDA_STORAGE_TEXTURE_US}"
    )
    set_tests_properties(cuda-storage-texture PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-sampled-texture-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/sampled-texture-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/validation/sampled-texture-cuda-usl/ptx_metadata.c
  )
  target_include_directories(gpu-sampled-texture-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_link_libraries(gpu-sampled-texture-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-sampled-texture-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_SAMPLED_TEXTURE_US}")
  set_target_properties(gpu-sampled-texture-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-sampled-texture
      COMMAND gpu-sampled-texture-cuda-usl
              "${GPU_CUDA_SAMPLED_TEXTURE_US}"
    )
    set_tests_properties(cuda-sampled-texture PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-sampled-format-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/sampled-format-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/validation/sampled-format-cuda-usl/ptx_metadata.c
  )
  target_include_directories(gpu-sampled-format-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_link_libraries(gpu-sampled-format-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-sampled-format-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_SAMPLED_FORMAT_US}")
  set_target_properties(gpu-sampled-format-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-sampled-format
      COMMAND gpu-sampled-format-cuda-usl
              "${GPU_CUDA_SAMPLED_FORMAT_US}"
    )
    set_tests_properties(cuda-sampled-format PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-texture-geometry-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/texture-geometry-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/validation/texture-geometry-cuda-usl/ptx_metadata.c
  )
  target_include_directories(gpu-texture-geometry-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_link_libraries(gpu-texture-geometry-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-texture-geometry-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_TEXTURE_GEOMETRY_US}")
  set_target_properties(gpu-texture-geometry-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-texture-geometry
      COMMAND gpu-texture-geometry-cuda-usl
              "${GPU_CUDA_TEXTURE_GEOMETRY_US}"
    )
    set_tests_properties(cuda-texture-geometry PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-storage-geometry-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/storage-geometry-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/validation/storage-geometry-cuda-usl/ptx_metadata.c
  )
  target_include_directories(gpu-storage-geometry-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_link_libraries(gpu-storage-geometry-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-storage-geometry-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_STORAGE_GEOMETRY_US}")
  set_target_properties(gpu-storage-geometry-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-storage-geometry
      COMMAND gpu-storage-geometry-cuda-usl
              "${GPU_CUDA_STORAGE_GEOMETRY_US}"
    )
    set_tests_properties(cuda-storage-geometry PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-descriptor-array-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/descriptor-array-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
  )
  target_include_directories(gpu-descriptor-array-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/api
  )
  target_link_libraries(gpu-descriptor-array-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-descriptor-array-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_US}"
                            "${GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_US}")
  set_target_properties(gpu-descriptor-array-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  gpu_enable_c11_atomics(gpu-descriptor-array-cuda-usl)
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-descriptor-array
      COMMAND gpu-descriptor-array-cuda-usl
              "${GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_US}"
              "${GPU_CUDA_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_US}"
    )
    set_tests_properties(cuda-descriptor-array PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-resource-descriptor-array-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/resource-descriptor-array-cuda-usl/main.c
  )
  target_link_libraries(gpu-resource-descriptor-array-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-resource-descriptor-array-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_RESOURCE_DESCRIPTOR_ARRAY_US}")
  set_target_properties(gpu-resource-descriptor-array-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-resource-descriptor-array
      COMMAND gpu-resource-descriptor-array-cuda-usl
              "${GPU_CUDA_RESOURCE_DESCRIPTOR_ARRAY_US}"
    )
    set_tests_properties(cuda-resource-descriptor-array PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-subgroup-matrix-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/subgroup-matrix-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/api/subgroup_matrix.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
  )
  target_include_directories(gpu-subgroup-matrix-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/api
  )
  target_link_libraries(gpu-subgroup-matrix-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-subgroup-matrix-cuda-usl
                            gpu-cuda-subgroup-matrix-fixture
                            "${GPU_CUDA_SUBGROUP_MATRIX_USL_FIXTURE}")
  set_target_properties(gpu-subgroup-matrix-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  gpu_enable_c11_atomics(gpu-subgroup-matrix-cuda-usl)
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-subgroup-matrix
      COMMAND gpu-subgroup-matrix-cuda-usl
              "${GPU_CUDA_SUBGROUP_MATRIX_USL_FIXTURE}"
    )
    set_tests_properties(cuda-subgroup-matrix PROPERTIES
      ENVIRONMENT "GPU_SUBGROUP_MATRIX_PROFILE=16x16x16"
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-atomic64-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/atomic64-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/api/atomic64.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
  )
  target_include_directories(gpu-atomic64-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/api
  )
  target_link_libraries(gpu-atomic64-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-atomic64-cuda-usl
                            gpu-cuda-atomic64-fixture
                            "${GPU_CUDA_ATOMIC64_USL_FIXTURE}")
  set_target_properties(gpu-atomic64-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  gpu_enable_c11_atomics(gpu-atomic64-cuda-usl)
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-atomic64
      COMMAND gpu-atomic64-cuda-usl
              "${GPU_CUDA_ATOMIC64_USL_FIXTURE}"
    )
    set_tests_properties(cuda-atomic64 PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-memory-cuda
    ${PROJECT_SOURCE_DIR}/tests/validation/memory-cuda/main.c
    ${PROJECT_SOURCE_DIR}/tests/api/memory.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
  )
  target_include_directories(gpu-memory-cuda PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/api
  )
  target_link_libraries(gpu-memory-cuda PRIVATE gpu)
  set_target_properties(gpu-memory-cuda PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  gpu_enable_c11_atomics(gpu-memory-cuda)
  if(GPU_BUILD_TESTS)
    add_test(NAME cuda-memory COMMAND gpu-memory-cuda)
    set_tests_properties(cuda-memory PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  add_executable(gpu-clock-cuda-usl
    ${PROJECT_SOURCE_DIR}/tests/validation/clock-cuda-usl/main.c
    ${PROJECT_SOURCE_DIR}/tests/api/clock_derivative.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
  )
  target_include_directories(gpu-clock-cuda-usl PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/api
  )
  target_link_libraries(gpu-clock-cuda-usl PRIVATE gpu)
  gpu_attach_usl_validation(gpu-clock-cuda-usl
                            gpu-cuda-fixtures
                            "${GPU_CUDA_SUBGROUP_CLOCK_US}"
                            "${GPU_CUDA_DEVICE_CLOCK_US}"
                            "${GPU_CUDA_DERIVATIVE_QUADS_US}"
                            "${GPU_CUDA_DERIVATIVE_LINEAR_US}")
  set_target_properties(gpu-clock-cuda-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  gpu_enable_c11_atomics(gpu-clock-cuda-usl)
  if(GPU_BUILD_TESTS)
    add_test(
      NAME cuda-clock
      COMMAND gpu-clock-cuda-usl
              "${GPU_CUDA_SUBGROUP_CLOCK_US}"
              "${GPU_CUDA_DEVICE_CLOCK_US}"
              "${GPU_CUDA_DERIVATIVE_QUADS_US}"
              "${GPU_CUDA_DERIVATIVE_LINEAR_US}"
    )
    set_tests_properties(cuda-clock PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 30
    )
  endif()

  if(GPU_BUILD_DX12 OR GPU_BUILD_VULKAN)
    add_executable(gpu-graphics-cuda-interop-usl
      ${PROJECT_SOURCE_DIR}/tests/validation/graphics-cuda-interop-usl/main.c
    )
    target_link_libraries(gpu-graphics-cuda-interop-usl PRIVATE gpu)
    gpu_attach_usl_validation(gpu-graphics-cuda-interop-usl
                              gpu-cuda-fixtures
                              "${GPU_CUDA_COMPUTE_BUFFER_US}"
                              "${GPU_CUDA_INTEROP_TEXTURE_US}")
    set_target_properties(gpu-graphics-cuda-interop-usl PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(GPU_BUILD_TESTS)
      add_test(
        NAME cuda-graphics-interop
        COMMAND gpu-graphics-cuda-interop-usl
                "${GPU_CUDA_COMPUTE_BUFFER_US}"
                "${GPU_CUDA_INTEROP_TEXTURE_US}"
      )
      set_tests_properties(cuda-graphics-interop PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 30
      )
      if(WIN32 AND GPU_BUILD_VULKAN)
        add_test(
          NAME cuda-vulkan-graphics-interop
          COMMAND gpu-graphics-cuda-interop-usl
                  "${GPU_CUDA_COMPUTE_BUFFER_US}"
                  "${GPU_CUDA_INTEROP_TEXTURE_US}"
        )
        set_tests_properties(cuda-vulkan-graphics-interop PROPERTIES
          ENVIRONMENT "GPU_GRAPHICS_BACKEND=vulkan"
          SKIP_RETURN_CODE 77
          TIMEOUT 30
        )
      endif()
    endif()
  endif()
endif()
