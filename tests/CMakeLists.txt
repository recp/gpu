if(GPU_BUILD_ANDROID_TESTS)
  add_executable(gpu-android-vulkan-test
    ${PROJECT_SOURCE_DIR}/tests/android/vulkan.c
    ${PROJECT_SOURCE_DIR}/tests/api/copy.c
    ${PROJECT_SOURCE_DIR}/tests/api/texture_transfer.c
  )
  target_include_directories(gpu-android-vulkan-test PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
  )
  target_link_libraries(gpu-android-vulkan-test PRIVATE
    gpu
    Vulkan::Vulkan
    log
  )
  set_target_properties(gpu-android-vulkan-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  target_compile_options(gpu-android-vulkan-test PRIVATE
    -Wall
    -Wextra
    -Werror
  )
endif()

function(gpu_enable_c11_atomics target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /experimental:c11atomics)
  endif()
endfunction()

include("${PROJECT_SOURCE_DIR}/tests/validation/fixtures.cmake")

set(GPU_BUILD_GRAPHICS_API_TESTS OFF)
if(GPU_BUILD_METAL OR GPU_BUILD_VULKAN OR GPU_BUILD_DX12 OR GPU_BUILD_WEBGPU)
  set(GPU_BUILD_GRAPHICS_API_TESTS ON)
endif()

if(GPU_BUILD_TESTS)
  get_property(GPU_GENERATOR_IS_MULTI_CONFIG GLOBAL
               PROPERTY GENERATOR_IS_MULTI_CONFIG)

  if(WIN32)
    set(runtimeStageCommands)
    set(runtimeStageDependencies)
    foreach(runtimeTarget gpu us ds)
      if(TARGET ${runtimeTarget})
        list(APPEND runtimeStageCommands
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
                  $<TARGET_FILE:${runtimeTarget}>
                  "${CMAKE_CURRENT_BINARY_DIR}"
        )
        list(APPEND runtimeStageDependencies ${runtimeTarget})
      endif()
    endforeach()
    if(GPU_BUILD_DX12 AND COMMAND gpu_dx12_agility_stage_commands)
      gpu_dx12_agility_stage_commands(
        agilityStageCommands "${CMAKE_CURRENT_BINARY_DIR}"
      )
      list(APPEND runtimeStageCommands ${agilityStageCommands})
    endif()
    if(GPU_BUILD_DX12 AND COMMAND gpu_dx12_dxc_stage_commands)
      gpu_dx12_dxc_stage_commands(
        dxcStageCommands "${CMAKE_CURRENT_BINARY_DIR}"
      )
      list(APPEND runtimeStageCommands ${dxcStageCommands})
    endif()
    add_custom_target(gpu-test-runtime-stage ALL
      ${runtimeStageCommands}
      DEPENDS ${runtimeStageDependencies}
      VERBATIM
    )
  endif()
endif()

if(GPU_BUILD_TESTS AND GPU_BUILD_GRAPHICS_API_TESTS)
  set(GPU_API_USL_SOURCES
    "${GPU_VALIDATION_REFLECTION_USL_SOURCE}"
    "${PROJECT_SOURCE_DIR}/tests/api/render_mrt.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/compute.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/source_sampler.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/storage_texture.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/cube_texture.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/line_texture.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/volume_texture.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/descriptor_arrays.usl"
    "${PROJECT_SOURCE_DIR}/tests/api/coordinate.usl"
  )
  if(GPU_BUILD_METAL)
    set(GPU_API_DEFAULT_BACKEND metal)
  elseif(GPU_BUILD_DX12)
    set(GPU_API_DEFAULT_BACKEND dx12)
  elseif(GPU_BUILD_WEBGPU)
    set(GPU_API_DEFAULT_BACKEND webgpu)
  else()
    set(GPU_API_DEFAULT_BACKEND vulkan)
  endif()
  gpu_add_usl_fixtures(
    GPU_API_USL_FIXTURES
    "${GPU_API_DEFAULT_BACKEND}"
    api
    ${GPU_API_USL_SOURCES}
  )
  gpu_add_usl_fixtures(
    GPU_API_DX12_BINDING_PLAN_USL_FIXTURE
    "${GPU_API_DEFAULT_BACKEND}"
    api-dx12-binding-plan
    "${PROJECT_SOURCE_DIR}/tests/api/dx12_binding_plan.usl"
  )
  add_custom_target(
    gpu-api-dx12-binding-plan-fixture
    DEPENDS ${GPU_API_DX12_BINDING_PLAN_USL_FIXTURE}
  )
  gpu_add_usl_fixtures(
    GPU_API_MSAA_USL_FIXTURE
    "${GPU_API_DEFAULT_BACKEND}"
    api-msaa
    "${PROJECT_SOURCE_DIR}/tests/api/msaa.usl"
  )
  add_custom_target(
    gpu-api-msaa-fixture
    DEPENDS ${GPU_API_MSAA_USL_FIXTURE}
  )
  set(GPU_API_DESCRIPTOR_INDEXING_BACKEND
      "${GPU_API_DEFAULT_BACKEND}")
  if(GPU_API_DEFAULT_BACKEND STREQUAL "webgpu")
    set(GPU_API_DESCRIPTOR_INDEXING_BACKEND vulkan)
  endif()
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_API_DESCRIPTOR_INDEXING_USL_FIXTURE
    "${GPU_API_DESCRIPTOR_INDEXING_BACKEND}"
    api
    "${PROJECT_SOURCE_DIR}/tests/api/descriptor_indexing.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES
    ${GPU_API_DESCRIPTOR_INDEXING_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE
    "${GPU_API_DESCRIPTOR_INDEXING_BACKEND}"
    api-buffer-descriptor-array
    "${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-buffer-descriptor-array-fixture
    DEPENDS ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
  gpu_add_usl_fixtures(
    GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE
    "${GPU_API_DESCRIPTOR_INDEXING_BACKEND}"
    api-buffer-descriptor-array-dynamic
    "${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array_dynamic.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-buffer-descriptor-array-dynamic-fixture
    DEPENDS ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS subgroup)
  gpu_add_usl_fixtures(
    GPU_API_SUBGROUP_USL_FIXTURE
    vulkan
    api
    "${PROJECT_SOURCE_DIR}/tests/api/subgroup.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES ${GPU_API_SUBGROUP_USL_FIXTURE})
  set(GPU_API_SUBGROUP_MATRIX_USL_SOURCE
      "${PROJECT_SOURCE_DIR}/tests/api/subgroup_matrix.usl")
  set(GPU_API_SUBGROUP_MATRIX_PROFILE "8x8x8")
  set(GPU_API_SUBGROUP_MATRIX_BACKEND "${GPU_API_DEFAULT_BACKEND}")
  set(GPU_USL_FIXTURE_TARGET_CAPS subgroup_matrix)
  if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" OR
     GPU_API_DEFAULT_BACKEND STREQUAL "webgpu")
    set(GPU_API_SUBGROUP_MATRIX_BACKEND vulkan)
    set(GPU_API_SUBGROUP_MATRIX_USL_SOURCE
        "${PROJECT_SOURCE_DIR}/tests/api/subgroup_matrix_vulkan.usl")
    set(GPU_API_SUBGROUP_MATRIX_PROFILE "16x8x8")
  elseif(GPU_API_DEFAULT_BACKEND STREQUAL "dx12")
    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_10,subgroup_matrix)
  endif()
  gpu_add_usl_fixtures(
    GPU_API_SUBGROUP_MATRIX_USL_FIXTURE
    "${GPU_API_SUBGROUP_MATRIX_BACKEND}"
    api-subgroup-matrix
    "${GPU_API_SUBGROUP_MATRIX_USL_SOURCE}"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-subgroup-matrix-fixture
    DEPENDS ${GPU_API_SUBGROUP_MATRIX_USL_FIXTURE}
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,shader_f16)
  gpu_add_usl_fixtures(
    GPU_API_SHADER_F16_USL_FIXTURE
    vulkan
    api
    "${PROJECT_SOURCE_DIR}/tests/api/shader_f16.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  list(APPEND GPU_API_USL_FIXTURES ${GPU_API_SHADER_F16_USL_FIXTURE})
  set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_6,atomic64)
  gpu_add_usl_fixtures(
    GPU_API_ATOMIC64_USL_FIXTURE
    dx12
    api-atomic64
    "${PROJECT_SOURCE_DIR}/tests/api/atomic64.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)
  add_custom_target(
    gpu-api-atomic64-fixture
    DEPENDS ${GPU_API_ATOMIC64_USL_FIXTURE}
  )
  add_custom_target(gpu-api-fixtures DEPENDS ${GPU_API_USL_FIXTURES})

  if(GPU_BUILD_METAL OR GPU_BUILD_DX12)
    if(GPU_BUILD_METAL)
      set(GPU_API_RAY_QUERY_BACKEND metal)
    else()
      set(GPU_API_RAY_QUERY_BACKEND dx12)
    endif()
    set(GPU_USL_FIXTURE_TARGET_CAPS ray_query)
    gpu_add_usl_fixtures(
      GPU_API_RAY_QUERY_USL_FIXTURE
      "${GPU_API_RAY_QUERY_BACKEND}"
      api
      "${PROJECT_SOURCE_DIR}/tests/api/ray_query.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    unset(GPU_API_RAY_QUERY_BACKEND)
    add_custom_target(
      gpu-api-ray-query-fixture
      DEPENDS ${GPU_API_RAY_QUERY_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_METAL)
    set(GPU_USL_FIXTURE_TARGET_CAPS
        ray_query,intersection_function_table)
    gpu_add_usl_fixtures(
      GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE
      metal
      api-intersection-function
      "${PROJECT_SOURCE_DIR}/tests/api/intersection_function.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-intersection-function-fixture
      DEPENDS ${GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_DX12)
    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_3,ray_tracing_pipeline)
    gpu_add_usl_fixtures(
      GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE
      dx12
      api-ray-pipeline
      "${PROJECT_SOURCE_DIR}/tests/api/ray_pipeline.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-dx12-ray-pipeline-fixture
      DEPENDS ${GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_8,execution_graph)
    gpu_add_usl_fixtures(
      GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE
      dx12
      api-execution-graph
      "${PROJECT_SOURCE_DIR}/tests/api/execution_graph.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-dx12-execution-graph-fixture
      DEPENDS ${GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS sm_6_5,sampler_feedback)
    gpu_add_usl_fixtures(
      GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE
      dx12
      api-sampler-feedback
      "${PROJECT_SOURCE_DIR}/tests/api/sampler_feedback.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-dx12-sampler-feedback-fixture
      DEPENDS ${GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    gpu_add_usl_fixtures(
      GPU_METAL_MESH_TEST_USL_FIXTURE
      metal
      mesh-test
      "${GPU_VALIDATION_MESH_USL_SOURCE}"
    )
    list(GET GPU_METAL_MESH_TEST_USL_FIXTURE 0 GPU_METAL_MESH_TEST_US)
    add_custom_target(
      gpu-metal-mesh-test-fixture
      DEPENDS ${GPU_METAL_MESH_TEST_USL_FIXTURE}
    )
  endif()

  if(GPU_BUILD_VULKAN)
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,shader_subgroup_clock)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${PROJECT_SOURCE_DIR}/tests/api/shader_subgroup_clock.usl"
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,shader_device_clock)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${PROJECT_SOURCE_DIR}/tests/api/shader_device_clock.usl"
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,compute_derivatives_quads)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${PROJECT_SOURCE_DIR}/tests/api/compute_derivatives_quads.usl"
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,compute_derivatives_linear)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE
      vulkan
      api-clock-derivatives
      "${PROJECT_SOURCE_DIR}/tests/api/compute_derivatives_linear.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    set(GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES
      ${GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE}
      ${GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE}
      ${GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE}
      ${GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE}
    )
    add_custom_target(
      gpu-api-vulkan-clock-derivative-fixtures
      DEPENDS ${GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES}
    )

    gpu_add_usl_fixtures(
      GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE
      vulkan
      api-untyped-pointer
      "${PROJECT_SOURCE_DIR}/tests/api/untyped_pointer.usl"
    )
    add_custom_target(
      gpu-api-vulkan-untyped-pointer-fixture
      DEPENDS ${GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS ray_query)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE
      vulkan
      api-ray-query
      "${PROJECT_SOURCE_DIR}/tests/api/ray_query.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-vulkan-ray-query-fixture
      DEPENDS ${GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_2,ray_tracing_pipeline)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE
      vulkan
      api-ray-pipeline
      "${PROJECT_SOURCE_DIR}/tests/api/ray_pipeline.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-vulkan-ray-pipeline-fixture
      DEPENDS ${GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE}
    )

    set(GPU_USL_FIXTURE_TARGET_CAPS vulkan1_3,execution_graph)
    gpu_add_usl_fixtures(
      GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE
      vulkan
      api-execution-graph
      "${PROJECT_SOURCE_DIR}/tests/api/execution_graph.usl"
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    add_custom_target(
      gpu-api-vulkan-execution-graph-fixture
      DEPENDS ${GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE}
    )

    if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan")
      set(GPU_API_VULKAN_USL_FIXTURES ${GPU_API_USL_FIXTURES})
      set(GPU_API_VULKAN_MSAA_USL_FIXTURE
          ${GPU_API_MSAA_USL_FIXTURE})
      set(GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE
          ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE})
      set(GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE
          ${GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE})
    else()
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_USL_FIXTURES
        vulkan
        api
        ${GPU_API_USL_SOURCES}
      )
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_MSAA_USL_FIXTURE
        vulkan
        api-vulkan-msaa
        "${PROJECT_SOURCE_DIR}/tests/api/msaa.usl"
      )
      add_custom_target(
        gpu-api-vulkan-msaa-fixture
        DEPENDS ${GPU_API_VULKAN_MSAA_USL_FIXTURE}
      )
      set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_DESCRIPTOR_INDEXING_USL_FIXTURE
        vulkan
        api
        "${PROJECT_SOURCE_DIR}/tests/api/descriptor_indexing.usl"
      )
      unset(GPU_USL_FIXTURE_TARGET_CAPS)
      list(APPEND GPU_API_VULKAN_USL_FIXTURES
        ${GPU_API_VULKAN_DESCRIPTOR_INDEXING_USL_FIXTURE}
        ${GPU_API_SUBGROUP_USL_FIXTURE}
        ${GPU_API_SHADER_F16_USL_FIXTURE}
      )
      set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE
        vulkan
        api-vulkan-buffer-descriptor-array
        "${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array.usl"
      )
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE
        vulkan
        api-vulkan-buffer-descriptor-array-dynamic
        "${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array_dynamic.usl"
      )
      unset(GPU_USL_FIXTURE_TARGET_CAPS)
      add_custom_target(
        gpu-api-vulkan-fixtures
        DEPENDS
          ${GPU_API_VULKAN_USL_FIXTURES}
          ${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}
          ${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}
      )
    endif()
  endif()
endif()

if(GPU_BUILD_TESTS)
  if(GPU_BUILD_WEBGPU AND NOT EMSCRIPTEN AND
     _gpu_webgpu_provider STREQUAL "WGPU_NATIVE")
    add_executable(gpu-webgpu-native-queue-test
      ${PROJECT_SOURCE_DIR}/tests/webgpu/queue.c
    )
    target_link_libraries(gpu-webgpu-native-queue-test PRIVATE gpu)
    set_target_properties(gpu-webgpu-native-queue-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(NOT MSVC)
      target_compile_options(gpu-webgpu-native-queue-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
    endif()
    if(NOT WIN32)
      target_compile_definitions(gpu-webgpu-native-queue-test PRIVATE
        _POSIX_C_SOURCE=200809L
      )
    endif()
    add_test(NAME webgpu-native-queue
             COMMAND gpu-webgpu-native-queue-test)
  endif()

  add_executable(gpu-usl-target-test
    ${PROJECT_SOURCE_DIR}/tests/api/usl_target.c
  )
  target_include_directories(gpu-usl-target-test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
  )
  if(TARGET us)
    target_link_libraries(gpu-usl-target-test PRIVATE us)
  elseif(TARGET gpu_usl)
    target_link_libraries(gpu-usl-target-test PRIVATE gpu_usl)
  endif()
  set_target_properties(gpu-usl-target-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-usl-target-test PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME api-usl-target COMMAND gpu-usl-target-test)

  add_executable(gpu-public-layout-test
    ${PROJECT_SOURCE_DIR}/tests/api/layout.c
  )
  target_include_directories(gpu-public-layout-test PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
  )
  set_target_properties(gpu-public-layout-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-public-layout-test PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME api-public-layout COMMAND gpu-public-layout-test)

  set(GPU_PUBLIC_EXPORTS)
  foreach(header IN LISTS GPU_PUBLIC_HEADERS)
    file(READ "${header}" header_contents)
    string(REGEX MATCHALL
      "[\r\n]GPU_EXPORT[ \t\r\n]+[^;{}]+;"
      export_declarations
      "${header_contents}"
    )
    foreach(declaration IN LISTS export_declarations)
      if(NOT declaration)
        continue()
      endif()
      string(REGEX MATCH
        "(GPU[A-Z][A-Za-z0-9_]*)[ \t\r\n]*\\("
        export_match
        "${declaration}"
      )
      if(NOT export_match)
        message(FATAL_ERROR "Could not parse public export in ${header}")
      endif()
      list(APPEND GPU_PUBLIC_EXPORTS "${CMAKE_MATCH_1}")
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES GPU_PUBLIC_EXPORTS)
  list(SORT GPU_PUBLIC_EXPORTS)

  set(GPU_EXPORT_LINKAGE_SOURCE
      "${PROJECT_BINARY_DIR}/generated/export_linkage.c")
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/generated")
  file(WRITE "${GPU_EXPORT_LINKAGE_SOURCE}"
    "#include <gpu/gpu.h>\n\n"
    "typedef void (*GPUExportSymbol)(void);\n\n"
    "static GPUExportSymbol volatile gpuExportSymbols[] = {\n"
  )
  foreach(export_name IN LISTS GPU_PUBLIC_EXPORTS)
    file(APPEND "${GPU_EXPORT_LINKAGE_SOURCE}"
      "  (GPUExportSymbol)${export_name},\n"
    )
  endforeach()
  file(APPEND "${GPU_EXPORT_LINKAGE_SOURCE}"
    "};\n\n"
    "int\n"
    "main(void) {\n"
    "  for (size_t i = 0u; i < GPU_ARRAY_LEN(gpuExportSymbols); i++) {\n"
    "    if (!gpuExportSymbols[i]) {\n"
    "      return 1;\n"
    "    }\n"
    "  }\n"
    "  return 0;\n"
    "}\n"
  )

  add_executable(gpu-export-linkage-test
    "${GPU_EXPORT_LINKAGE_SOURCE}"
  )
  target_link_libraries(gpu-export-linkage-test PRIVATE gpu)
  set_target_properties(gpu-export-linkage-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-export-linkage-test PRIVATE
      -Wall
      -Wextra
      -Werror
    )
  endif()
  add_test(NAME api-export-linkage COMMAND gpu-export-linkage-test)

  add_test(
    NAME package-consumer
    COMMAND ${CMAKE_COMMAND}
      -DGPU_PACKAGE_BUILD_DIR=${PROJECT_BINARY_DIR}
      -DGPU_PACKAGE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
      -DGPU_PACKAGE_TEST_DIR=${PROJECT_BINARY_DIR}/package-consumer-test
      -DGPU_PACKAGE_VERSION=${PROJECT_VERSION}
      -DGPU_PACKAGE_CONFIG=$<CONFIG>
      -DGPU_PACKAGE_GENERATOR=${CMAKE_GENERATOR}
      -DGPU_PACKAGE_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}
      -DGPU_PACKAGE_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}
      -DGPU_PACKAGE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
      -DGPU_PACKAGE_MULTI_CONFIG=${GPU_GENERATOR_IS_MULTI_CONFIG}
      -DGPU_PACKAGE_RUNTIME_DIR=${GPU_VULKAN_LIBRARY_DIR}
      -DGPU_PACKAGE_VULKAN_ICD=${GPU_VULKAN_ICD}
      -P ${PROJECT_SOURCE_DIR}/cmake/package_test.cmake
  )
  set_tests_properties(package-consumer PROPERTIES LABELS package)
endif()

if(GPU_BUILD_TESTS AND GPU_BUILD_GRAPHICS_API_TESTS)
  add_executable(gpu-api-test
    ${PROJECT_SOURCE_DIR}/tests/api/atomic64.c
    ${PROJECT_SOURCE_DIR}/tests/api/barrier.c
    ${PROJECT_SOURCE_DIR}/tests/api/bindgroup.c
    ${PROJECT_SOURCE_DIR}/tests/api/buffer_descriptor_array.c
    ${PROJECT_SOURCE_DIR}/tests/api/compute.c
    ${PROJECT_SOURCE_DIR}/tests/api/clock_derivative.c
    ${PROJECT_SOURCE_DIR}/tests/api/coordinate.c
    ${PROJECT_SOURCE_DIR}/tests/api/copy.c
    ${PROJECT_SOURCE_DIR}/tests/api/cube_texture.c
    ${PROJECT_SOURCE_DIR}/tests/api/descriptor_array.c
    ${PROJECT_SOURCE_DIR}/tests/api/dx12_binding_plan.c
    ${PROJECT_SOURCE_DIR}/tests/api/execution_graph.c
    ${PROJECT_SOURCE_DIR}/tests/api/line_texture.c
    ${PROJECT_SOURCE_DIR}/tests/api/main.c
    ${PROJECT_SOURCE_DIR}/tests/api/memory.c
    ${PROJECT_SOURCE_DIR}/tests/api/metal_vertex_slots.c
    ${PROJECT_SOURCE_DIR}/tests/api/msaa.c
    ${PROJECT_SOURCE_DIR}/tests/api/multigpu.c
    ${PROJECT_SOURCE_DIR}/tests/api/query.c
    ${PROJECT_SOURCE_DIR}/tests/api/queue.c
    ${PROJECT_SOURCE_DIR}/tests/api/ray.c
    ${PROJECT_SOURCE_DIR}/tests/api/render.c
    ${PROJECT_SOURCE_DIR}/tests/api/resources.c
    ${PROJECT_SOURCE_DIR}/tests/api/runner.c
    ${PROJECT_SOURCE_DIR}/tests/api/runtime.c
    ${PROJECT_SOURCE_DIR}/tests/api/sampler.c
    ${PROJECT_SOURCE_DIR}/tests/api/sampler_feedback.c
    ${PROJECT_SOURCE_DIR}/tests/api/shader.c
    ${PROJECT_SOURCE_DIR}/tests/api/shader_f16.c
    ${PROJECT_SOURCE_DIR}/tests/api/shader_sampler.c
    ${PROJECT_SOURCE_DIR}/tests/api/storage_texture.c
    ${PROJECT_SOURCE_DIR}/tests/api/subgroup.c
    ${PROJECT_SOURCE_DIR}/tests/api/subgroup_matrix.c
    ${PROJECT_SOURCE_DIR}/tests/api/texture_integer_clear.c
    ${PROJECT_SOURCE_DIR}/tests/api/texture_transfer.c
    ${PROJECT_SOURCE_DIR}/tests/api/texture_view_render.c
    ${PROJECT_SOURCE_DIR}/tests/api/threading.c
    ${PROJECT_SOURCE_DIR}/tests/api/untyped_pointer.c
    ${PROJECT_SOURCE_DIR}/tests/api/volume_texture.c
    ${PROJECT_SOURCE_DIR}/tests/api/vrs.c
  )
  target_link_libraries(gpu-api-test PRIVATE gpu)
  add_dependencies(gpu-api-test gpu-api-fixtures)
  add_dependencies(gpu-api-test gpu-api-dx12-binding-plan-fixture)
  add_dependencies(gpu-api-test gpu-api-msaa-fixture)
  add_dependencies(gpu-api-test gpu-api-atomic64-fixture)
  add_dependencies(gpu-api-test gpu-api-buffer-descriptor-array-fixture)
  add_dependencies(gpu-api-test
                   gpu-api-buffer-descriptor-array-dynamic-fixture)
  if(TARGET gpu-api-subgroup-matrix-fixture)
    add_dependencies(gpu-api-test gpu-api-subgroup-matrix-fixture)
  endif()
  if(TARGET gpu-api-ray-query-fixture)
    add_dependencies(gpu-api-test gpu-api-ray-query-fixture)
  endif()
  if(TARGET gpu-api-intersection-function-fixture)
    add_dependencies(gpu-api-test gpu-api-intersection-function-fixture)
  endif()
  if(TARGET gpu-api-vulkan-fixtures)
    add_dependencies(gpu-api-test gpu-api-vulkan-fixtures)
  endif()
  if(TARGET gpu-api-vulkan-msaa-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-msaa-fixture)
  endif()
  if(TARGET gpu-api-vulkan-ray-query-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-ray-query-fixture)
  endif()
  if(TARGET gpu-api-dx12-ray-pipeline-fixture)
    add_dependencies(gpu-api-test gpu-api-dx12-ray-pipeline-fixture)
  endif()
  if(TARGET gpu-api-vulkan-ray-pipeline-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-ray-pipeline-fixture)
  endif()
  if(TARGET gpu-api-dx12-execution-graph-fixture)
    add_dependencies(gpu-api-test gpu-api-dx12-execution-graph-fixture)
  endif()
  if(TARGET gpu-api-dx12-sampler-feedback-fixture)
    add_dependencies(gpu-api-test gpu-api-dx12-sampler-feedback-fixture)
  endif()
  if(TARGET gpu-api-vulkan-execution-graph-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-execution-graph-fixture)
  endif()
  if(TARGET gpu-api-vulkan-clock-derivative-fixtures)
    add_dependencies(gpu-api-test gpu-api-vulkan-clock-derivative-fixtures)
  endif()
  if(TARGET gpu-api-vulkan-untyped-pointer-fixture)
    add_dependencies(gpu-api-test gpu-api-vulkan-untyped-pointer-fixture)
  endif()
  if(TARGET gpu-test-runtime-stage)
    add_dependencies(gpu-api-test gpu-test-runtime-stage)
  endif()
  target_compile_definitions(gpu-api-test PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
    GPU_BUILD_WITH_DEBUG_MARKERS=$<BOOL:${GPU_BUILD_WITH_DEBUG_MARKERS}>
  )
  set_target_properties(gpu-api-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(NOT MSVC)
    target_compile_options(gpu-api-test PRIVATE -Wall -Wextra -Werror)
  endif()
  gpu_enable_c11_atomics(gpu-api-test)
  if(NOT WIN32)
    target_compile_definitions(gpu-api-test PRIVATE
      _POSIX_C_SOURCE=200809L
    )
  endif()

  add_test(NAME api-validation
    COMMAND gpu-api-test
            ${GPU_API_USL_FIXTURES}
            ${GPU_API_DEFAULT_BACKEND}
  )
  set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
    "GPU_MSAA_USL_PATH=${GPU_API_MSAA_USL_FIXTURE}"
    "GPU_BUFFER_DESCRIPTOR_ARRAY_USL_PATH=${GPU_API_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}"
    "GPU_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_PATH=${GPU_API_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}"
    "GPU_DX12_BINDING_PLAN_USL_PATH=${GPU_API_DX12_BINDING_PLAN_USL_FIXTURE}"
  )
  if(GPU_API_RAY_QUERY_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_RAY_QUERY_USL_PATH=${GPU_API_RAY_QUERY_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_INTERSECTION_FUNCTION_USL_PATH=${GPU_API_INTERSECTION_FUNCTION_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "dx12" AND
     GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_RAY_PIPELINE_USL_PATH=${GPU_API_DX12_RAY_PIPELINE_USL_FIXTURE}"
    )
  elseif(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
         GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_RAY_PIPELINE_USL_PATH=${GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "dx12" AND
     GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_EXECUTION_GRAPH_USL_PATH=${GPU_API_DX12_EXECUTION_GRAPH_USL_FIXTURE}"
    )
  elseif(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
         GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_EXECUTION_GRAPH_USL_PATH=${GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "dx12" AND
     GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_SAMPLER_FEEDBACK_USL_PATH=${GPU_API_DX12_SAMPLER_FEEDBACK_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
     GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_SHADER_SUBGROUP_CLOCK_USL_PATH=${GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE}"
      "GPU_SHADER_DEVICE_CLOCK_USL_PATH=${GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE}"
      "GPU_COMPUTE_DERIVATIVE_QUADS_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE}"
      "GPU_COMPUTE_DERIVATIVE_LINEAR_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan" AND
     GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_UNTYPED_POINTER_USL_PATH=${GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE}"
    )
  endif()
  if(GPU_API_SUBGROUP_MATRIX_USL_FIXTURE)
    set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
      "GPU_SUBGROUP_MATRIX_USL_PATH=${GPU_API_SUBGROUP_MATRIX_USL_FIXTURE}"
      "GPU_SUBGROUP_MATRIX_PROFILE=${GPU_API_SUBGROUP_MATRIX_PROFILE}"
    )
  endif()
  set_property(TEST api-validation APPEND PROPERTY ENVIRONMENT
    "GPU_ATOMIC64_USL_PATH=${GPU_API_ATOMIC64_USL_FIXTURE}"
  )

  if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(gpu-metal-mesh-test
      ${PROJECT_SOURCE_DIR}/tests/api/mesh.c
      ${PROJECT_SOURCE_DIR}/tests/api/runner.c
    )
    target_link_libraries(gpu-metal-mesh-test PRIVATE gpu)
    add_dependencies(gpu-metal-mesh-test gpu-metal-mesh-test-fixture)
    set_target_properties(gpu-metal-mesh-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(NOT MSVC)
      target_compile_options(gpu-metal-mesh-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
    endif()
    add_test(NAME metal-mesh
      COMMAND gpu-metal-mesh-test
              metal
              "${GPU_METAL_MESH_TEST_US}"
    )
    set_tests_properties(metal-mesh PROPERTIES
      SKIP_RETURN_CODE 77
    )
  endif()

  if(GPU_BUILD_VULKAN)
    list(GET GPU_API_VULKAN_USL_FIXTURES 1 GPU_VULKAN_RENDER_MRT_US)

    add_test(NAME api-validation-vulkan
      COMMAND gpu-api-test
              ${GPU_API_VULKAN_USL_FIXTURES}
              vulkan
    )
    set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
      "GPU_MSAA_USL_PATH=${GPU_API_VULKAN_MSAA_USL_FIXTURE}"
      "GPU_BUFFER_DESCRIPTOR_ARRAY_USL_PATH=${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_USL_FIXTURE}"
      "GPU_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_PATH=${GPU_API_VULKAN_BUFFER_DESCRIPTOR_ARRAY_DYNAMIC_USL_FIXTURE}"
    )
    if(GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_RAY_QUERY_USL_PATH=${GPU_API_VULKAN_RAY_QUERY_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_RAY_PIPELINE_USL_PATH=${GPU_API_VULKAN_RAY_PIPELINE_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_EXECUTION_GRAPH_USL_PATH=${GPU_API_VULKAN_EXECUTION_GRAPH_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_CLOCK_DERIVATIVE_USL_FIXTURES)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_SHADER_SUBGROUP_CLOCK_USL_PATH=${GPU_API_VULKAN_SUBGROUP_CLOCK_USL_FIXTURE}"
        "GPU_SHADER_DEVICE_CLOCK_USL_PATH=${GPU_API_VULKAN_DEVICE_CLOCK_USL_FIXTURE}"
        "GPU_COMPUTE_DERIVATIVE_QUADS_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_QUADS_USL_FIXTURE}"
        "GPU_COMPUTE_DERIVATIVE_LINEAR_USL_PATH=${GPU_API_VULKAN_DERIVATIVE_LINEAR_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE)
      set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
        "GPU_UNTYPED_POINTER_USL_PATH=${GPU_API_VULKAN_UNTYPED_POINTER_USL_FIXTURE}"
      )
    endif()
    if(GPU_API_DEFAULT_BACKEND STREQUAL "vulkan")
      set(GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE
          ${GPU_API_SUBGROUP_MATRIX_USL_FIXTURE})
    else()
      set(GPU_USL_FIXTURE_TARGET_CAPS subgroup_matrix)
      gpu_add_usl_fixtures(
        GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE
        vulkan
        api-vulkan-subgroup-matrix
        "${PROJECT_SOURCE_DIR}/tests/api/subgroup_matrix_vulkan.usl"
      )
      unset(GPU_USL_FIXTURE_TARGET_CAPS)
      add_custom_target(
        gpu-api-vulkan-subgroup-matrix-fixture
        DEPENDS ${GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE}
      )
      add_dependencies(gpu-api-test
                       gpu-api-vulkan-subgroup-matrix-fixture)
    endif()
    set_property(TEST api-validation-vulkan APPEND PROPERTY ENVIRONMENT
      "GPU_SUBGROUP_MATRIX_USL_PATH=${GPU_API_VULKAN_SUBGROUP_MATRIX_USL_FIXTURE}"
      "GPU_SUBGROUP_MATRIX_PROFILE=16x8x8"
      "GPU_ATOMIC64_USL_PATH=${GPU_API_ATOMIC64_USL_FIXTURE}"
    )

    add_executable(gpu-vulkan-queue-test
      ${PROJECT_SOURCE_DIR}/tests/vulkan/queue.c
    )
    target_include_directories(gpu-vulkan-queue-test PRIVATE
      ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(gpu-vulkan-queue-test PRIVATE gpu Vulkan::Vulkan)
    if(TARGET us)
      target_link_libraries(gpu-vulkan-queue-test PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-vulkan-queue-test PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-vulkan-queue-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_executable(gpu-vulkan-shader-test
      ${PROJECT_SOURCE_DIR}/tests/vulkan/shader.c
      ${PROJECT_SOURCE_DIR}/tests/vulkan/texture.c
      ${PROJECT_SOURCE_DIR}/tests/api/copy.c
      ${PROJECT_SOURCE_DIR}/tests/api/texture_transfer.c
    )
    target_include_directories(gpu-vulkan-shader-test PRIVATE
      ${PROJECT_SOURCE_DIR}/src
    )
    target_link_libraries(gpu-vulkan-shader-test PRIVATE gpu Vulkan::Vulkan)
    add_dependencies(gpu-vulkan-shader-test gpu-vulkan-fixtures)
    set_target_properties(gpu-vulkan-shader-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_executable(gpu-vulkan-mesh-test
      ${PROJECT_SOURCE_DIR}/tests/api/mesh.c
      ${PROJECT_SOURCE_DIR}/tests/api/runner.c
    )
    target_link_libraries(gpu-vulkan-mesh-test PRIVATE gpu Vulkan::Vulkan)
    add_dependencies(gpu-vulkan-mesh-test gpu-vulkan-fixtures)
    set_target_properties(gpu-vulkan-mesh-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    gpu_enable_c11_atomics(gpu-vulkan-mesh-test)

    add_executable(gpu-vulkan-indirect-copy-test
      ${PROJECT_SOURCE_DIR}/tests/vulkan/indirect_copy.c
    )
    target_link_libraries(gpu-vulkan-indirect-copy-test PRIVATE
      gpu
      Vulkan::Vulkan
    )
    set_target_properties(gpu-vulkan-indirect-copy-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    if(NOT MSVC)
      target_compile_options(gpu-vulkan-queue-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
      target_compile_options(gpu-vulkan-shader-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
      target_compile_options(gpu-vulkan-mesh-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
      target_compile_options(gpu-vulkan-indirect-copy-test PRIVATE
        -Wall
        -Wextra
        -Werror
      )
    endif()

    add_test(NAME vulkan-queue
      COMMAND gpu-vulkan-queue-test
    )
    add_test(NAME vulkan-shader
      COMMAND gpu-vulkan-shader-test
              "${GPU_VULKAN_DYNAMIC_TRIANGLE_US}"
              "${GPU_VULKAN_RENDER_MRT_US}"
              "${GPU_VULKAN_TEXTURED_QUAD_US}"
    )
    add_test(NAME vulkan-mesh
      COMMAND gpu-vulkan-mesh-test
              vulkan
              "${GPU_VULKAN_MESH_TRIANGLE_US}"
    )
    set_tests_properties(vulkan-mesh PROPERTIES
      SKIP_RETURN_CODE 77
    )
    add_test(NAME vulkan-indirect-copy
      COMMAND gpu-vulkan-indirect-copy-test
    )
    set_tests_properties(vulkan-indirect-copy PROPERTIES
      SKIP_RETURN_CODE 77
    )
  endif()

  if(GPU_BUILD_DX12)
    add_executable(gpu-dx12-queue-test ${PROJECT_SOURCE_DIR}/tests/dx12/queue.c)
    target_link_libraries(gpu-dx12-queue-test PRIVATE gpu)
    set_target_properties(gpu-dx12-queue-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_test(NAME dx12-queue COMMAND gpu-dx12-queue-test)

    add_executable(gpu-dx12-barrier-test ${PROJECT_SOURCE_DIR}/tests/dx12/barrier.c)
    target_link_libraries(gpu-dx12-barrier-test PRIVATE gpu dxguid)
    if(TARGET us)
      target_link_libraries(gpu-dx12-queue-test PRIVATE us)
      target_link_libraries(gpu-dx12-barrier-test PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-dx12-queue-test PRIVATE gpu_usl)
      target_link_libraries(gpu-dx12-barrier-test PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-dx12-barrier-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_test(NAME dx12-barrier COMMAND gpu-dx12-barrier-test)

    add_executable(gpu-dx12-depth-test ${PROJECT_SOURCE_DIR}/tests/dx12/depth.c)
    target_link_libraries(gpu-dx12-depth-test PRIVATE gpu)
    add_dependencies(gpu-dx12-depth-test gpu-dx12-fixtures)
    set_target_properties(gpu-dx12-depth-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_test(NAME dx12-depth-pipeline
      COMMAND gpu-dx12-depth-test
              "${GPU_DX12_TRIANGLE_US}"
    )

    add_executable(gpu-dx12-texture-test ${PROJECT_SOURCE_DIR}/tests/dx12/texture.c)
    target_link_libraries(gpu-dx12-texture-test PRIVATE gpu)
    add_dependencies(gpu-dx12-texture-test gpu-dx12-fixtures)
    set_target_properties(gpu-dx12-texture-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )

    add_test(NAME dx12-texture
      COMMAND gpu-dx12-texture-test
              "${GPU_DX12_TEXTURED_QUAD_US}"
    )

    add_executable(gpu-dx12-mesh-test
      ${PROJECT_SOURCE_DIR}/tests/api/mesh.c
      ${PROJECT_SOURCE_DIR}/tests/api/runner.c
    )
    target_link_libraries(gpu-dx12-mesh-test PRIVATE gpu)
    add_dependencies(gpu-dx12-mesh-test gpu-dx12-fixtures)
    set_target_properties(gpu-dx12-mesh-test PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    gpu_enable_c11_atomics(gpu-dx12-mesh-test)

    add_test(NAME dx12-mesh
      COMMAND gpu-dx12-mesh-test
              dx12
              "${GPU_DX12_MESH_TRIANGLE_US}"
    )
    set_tests_properties(dx12-mesh PROPERTIES
      SKIP_RETURN_CODE 77
    )

    foreach(dx12Target IN ITEMS
            gpu-dx12-queue-test
            gpu-dx12-barrier-test
            gpu-dx12-depth-test
            gpu-dx12-texture-test
            gpu-dx12-mesh-test)
      add_dependencies(${dx12Target} gpu-test-runtime-stage)
    endforeach()
  endif()
endif()

if(GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES)
  add_subdirectory(validation)
endif()

if((GPU_BUILD_TESTS OR GPU_BUILD_BENCHMARKS) AND
   GPU_BUILD_VULKAN AND APPLE)
  if(EXISTS "${GPU_VULKAN_ICD}")
    foreach(vulkanTest
            api-validation-vulkan
            vulkan-queue
            vulkan-shader
            vulkan-mesh
            vulkan-indirect-copy
            vulkan-compute-buffer
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
            vulkan-resize-lifecycle)
      if(TEST ${vulkanTest})
        set_property(TEST ${vulkanTest} APPEND PROPERTY ENVIRONMENT
          "VK_ICD_FILENAMES=${GPU_VULKAN_ICD}"
        )
      endif()
    endforeach()
  endif()
endif()

if(WIN32)
  get_property(gpu_tests DIRECTORY PROPERTY TESTS)
  if(gpu_tests)
    set_tests_properties(${gpu_tests} PROPERTIES
      WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
    )
  endif()
endif()
