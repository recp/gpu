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
      NAME dx12-compute-buffer
      COMMAND gpu-compute-buffer-dx12-usl
              "${GPU_DX12_COMPUTE_BUFFER_US}"
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
