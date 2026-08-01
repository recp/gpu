if(MSVC)
  add_compile_options(/experimental:c11atomics)
endif()

if(GPU_BUILD_BENCHMARKS)
  set(GPU_BENCHMARK_USL_SOURCES
    "${PROJECT_SOURCE_DIR}/tests/api/render_mrt.usl"
    "${PROJECT_SOURCE_DIR}/tests/validation/triangle-usl/triangle.usl"
    "${PROJECT_SOURCE_DIR}/benchmarks/binding_churn.usl"
  )
  set(GPU_BENCHMARK_BINDLESS_USL_SOURCE
    "${PROJECT_SOURCE_DIR}/benchmarks/bindless_dispatch.usl"
  )
  set(GPU_BENCHMARK_USL_FIXTURES)

  if(GPU_BUILD_METAL)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_METAL_USL_FIXTURES
      metal
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_METAL_BINDLESS_USL_FIXTURE
      metal
      benchmarks
      ${GPU_BENCHMARK_BINDLESS_USL_SOURCE}
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    list(APPEND GPU_BENCHMARK_METAL_USL_FIXTURES
      ${GPU_BENCHMARK_METAL_BINDLESS_USL_FIXTURE}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_METAL_USL_FIXTURES}
    )
  endif()
  if(GPU_BUILD_VULKAN)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_VULKAN_USL_FIXTURES
      vulkan
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_VULKAN_BINDLESS_USL_FIXTURE
      vulkan
      benchmarks
      ${GPU_BENCHMARK_BINDLESS_USL_SOURCE}
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    list(APPEND GPU_BENCHMARK_VULKAN_USL_FIXTURES
      ${GPU_BENCHMARK_VULKAN_BINDLESS_USL_FIXTURE}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_VULKAN_USL_FIXTURES}
    )
  endif()
  if(GPU_BUILD_DX12)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_DX12_USL_FIXTURES
      dx12
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    set(GPU_USL_FIXTURE_TARGET_CAPS descriptor_indexing)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_DX12_BINDLESS_USL_FIXTURE
      dx12
      benchmarks
      ${GPU_BENCHMARK_BINDLESS_USL_SOURCE}
    )
    unset(GPU_USL_FIXTURE_TARGET_CAPS)
    list(APPEND GPU_BENCHMARK_DX12_USL_FIXTURES
      ${GPU_BENCHMARK_DX12_BINDLESS_USL_FIXTURE}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_DX12_USL_FIXTURES}
    )
  endif()
  if(GPU_BUILD_WEBGPU AND NOT EMSCRIPTEN)
    gpu_add_usl_fixtures(
      GPU_BENCHMARK_WEBGPU_USL_FIXTURES
      webgpu
      benchmarks
      ${GPU_BENCHMARK_USL_SOURCES}
    )
    list(APPEND GPU_BENCHMARK_USL_FIXTURES
      ${GPU_BENCHMARK_WEBGPU_USL_FIXTURES}
    )
  endif()

  if(GPU_BUILD_METAL)
    set(GPU_BENCHMARK_BACKEND metal)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_METAL_USL_FIXTURES}
    )
  elseif(GPU_BUILD_DX12)
    set(GPU_BENCHMARK_BACKEND dx12)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_DX12_USL_FIXTURES}
    )
  elseif(GPU_BUILD_VULKAN)
    set(GPU_BENCHMARK_BACKEND vulkan)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_VULKAN_USL_FIXTURES}
    )
  elseif(GPU_BUILD_WEBGPU AND NOT EMSCRIPTEN)
    set(GPU_BENCHMARK_BACKEND webgpu)
    set(GPU_BENCHMARK_SELECTED_USL_FIXTURES
      ${GPU_BENCHMARK_WEBGPU_USL_FIXTURES}
    )
  endif()
  if(GPU_BENCHMARK_SELECTED_USL_FIXTURES)
    list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
         0
         GPU_BENCHMARK_RENDER_MRT_US)
    list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
         1
         GPU_BENCHMARK_TRIANGLE_US)
    list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
         2
         GPU_BENCHMARK_BINDING_US)
    list(LENGTH GPU_BENCHMARK_SELECTED_USL_FIXTURES
         GPU_BENCHMARK_SELECTED_USL_FIXTURE_COUNT)
    if(GPU_BENCHMARK_SELECTED_USL_FIXTURE_COUNT GREATER 3)
      list(GET GPU_BENCHMARK_SELECTED_USL_FIXTURES
           3
           GPU_BENCHMARK_BINDLESS_DISPATCH_US)
    endif()
  endif()

  add_custom_target(gpu-benchmark-fixtures
    DEPENDS ${GPU_BENCHMARK_USL_FIXTURES}
  )
endif()

if(GPU_BUILD_BENCHMARKS)
  add_executable(gpu-dispatch-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/dispatch.c
  )
  target_link_libraries(gpu-dispatch-bench PRIVATE gpu)
  target_include_directories(gpu-dispatch-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-dispatch-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
    GPU_BACKEND_METAL_ONLY=$<BOOL:${GPU_BACKEND_METAL_ONLY}>
    GPU_BACKEND_VULKAN_ONLY=$<BOOL:${GPU_BACKEND_VULKAN_ONLY}>
    GPU_BACKEND_DX12_ONLY=$<BOOL:${GPU_BACKEND_DX12_ONLY}>
  )
  set_target_properties(gpu-dispatch-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-dispatch-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-dispatch-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-ray-dispatch-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/ray_dispatch.c
  )
  target_link_libraries(gpu-ray-dispatch-bench PRIVATE gpu)
  target_include_directories(gpu-ray-dispatch-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-ray-dispatch-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
    GPU_BACKEND_METAL_ONLY=$<BOOL:${GPU_BACKEND_METAL_ONLY}>
    GPU_BACKEND_VULKAN_ONLY=$<BOOL:${GPU_BACKEND_VULKAN_ONLY}>
    GPU_BACKEND_DX12_ONLY=$<BOOL:${GPU_BACKEND_DX12_ONLY}>
  )
  set_target_properties(gpu-ray-dispatch-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-ray-dispatch-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-ray-dispatch-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-queue-upload-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/queue_upload.c
  )
  target_link_libraries(gpu-queue-upload-bench PRIVATE gpu)
  target_include_directories(gpu-queue-upload-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-queue-upload-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-queue-upload-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-queue-upload-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-queue-submit-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/queue_submit.c
  )
  target_link_libraries(gpu-queue-submit-bench PRIVATE gpu)
  target_include_directories(gpu-queue-submit-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-queue-submit-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-queue-submit-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-queue-submit-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-texture-upload-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/texture_upload.c
  )
  target_link_libraries(gpu-texture-upload-bench PRIVATE gpu)
  target_include_directories(gpu-texture-upload-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-texture-upload-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-texture-upload-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-texture-upload-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-static-scene-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/render.c
    ${PROJECT_SOURCE_DIR}/benchmarks/static_scene.c
  )
  target_link_libraries(gpu-static-scene-bench PRIVATE gpu)
  target_include_directories(gpu-static-scene-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-static-scene-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-static-scene-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-static-scene-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-static-scene-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-state-churn-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/render.c
    ${PROJECT_SOURCE_DIR}/benchmarks/state_churn.c
  )
  target_link_libraries(gpu-state-churn-bench PRIVATE gpu)
  target_include_directories(gpu-state-churn-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-state-churn-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-state-churn-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-state-churn-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-state-churn-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-binding-churn-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/render.c
    ${PROJECT_SOURCE_DIR}/benchmarks/binding_churn.c
  )
  target_link_libraries(gpu-binding-churn-bench PRIVATE gpu)
  target_include_directories(gpu-binding-churn-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-binding-churn-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-binding-churn-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-binding-churn-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-binding-churn-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-bindless-update-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/bindless_update.c
  )
  target_link_libraries(gpu-bindless-update-bench PRIVATE gpu)
  target_include_directories(gpu-bindless-update-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-bindless-update-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-bindless-update-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-bindless-update-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-bindless-dispatch-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/bindless_dispatch.c
  )
  target_link_libraries(gpu-bindless-dispatch-bench PRIVATE gpu)
  target_include_directories(gpu-bindless-dispatch-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  set_target_properties(gpu-bindless-dispatch-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-bindless-dispatch-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-bindless-dispatch-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-bindless-render-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/render.c
    ${PROJECT_SOURCE_DIR}/benchmarks/bindless_render.c
  )
  target_link_libraries(gpu-bindless-render-bench PRIVATE gpu)
  target_include_directories(gpu-bindless-render-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-bindless-render-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-bindless-render-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-bindless-render-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-bindless-render-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-upload-heavy-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/render.c
    ${PROJECT_SOURCE_DIR}/benchmarks/upload_heavy.c
  )
  target_link_libraries(gpu-upload-heavy-bench PRIVATE gpu)
  target_include_directories(gpu-upload-heavy-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-upload-heavy-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-upload-heavy-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-upload-heavy-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-upload-heavy-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-pipeline-stress-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/render.c
    ${PROJECT_SOURCE_DIR}/benchmarks/pipeline_stress.c
  )
  target_link_libraries(gpu-pipeline-stress-bench PRIVATE gpu)
  target_include_directories(gpu-pipeline-stress-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-pipeline-stress-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-pipeline-stress-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-pipeline-stress-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-pipeline-stress-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-validation-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/validation.c
  )
  target_link_libraries(gpu-validation-bench PRIVATE gpu)
  target_include_directories(gpu-validation-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-validation-bench PRIVATE
    GPU_BUILD_WITH_VALIDATION=$<BOOL:${GPU_BUILD_WITH_VALIDATION}>
  )
  set_target_properties(gpu-validation-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-validation-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-validation-bench PRIVATE -Wall -Wextra)
  endif()

  add_executable(gpu-marker-bench
    ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
    ${PROJECT_SOURCE_DIR}/benchmarks/markers.c
  )
  target_link_libraries(gpu-marker-bench PRIVATE gpu)
  target_include_directories(gpu-marker-bench PRIVATE
    ${PROJECT_SOURCE_DIR}/src
  )
  target_compile_definitions(gpu-marker-bench PRIVATE
    GPU_BUILD_WITH_DEBUG_MARKERS=$<BOOL:${GPU_BUILD_WITH_DEBUG_MARKERS}>
  )
  set_target_properties(gpu-marker-bench PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )
  if(MSVC)
    target_compile_options(gpu-marker-bench PRIVATE /W3)
  else()
    target_compile_options(gpu-marker-bench PRIVATE -Wall -Wextra)
  endif()

  foreach(gpu_benchmark_target
          gpu-dispatch-bench
          gpu-ray-dispatch-bench
          gpu-queue-upload-bench
          gpu-queue-submit-bench
          gpu-texture-upload-bench
          gpu-static-scene-bench
          gpu-state-churn-bench
          gpu-binding-churn-bench
          gpu-bindless-update-bench
          gpu-bindless-dispatch-bench
          gpu-bindless-render-bench
          gpu-upload-heavy-bench
          gpu-pipeline-stress-bench
          gpu-validation-bench
          gpu-marker-bench)
    target_sources(${gpu_benchmark_target} PRIVATE ${PROJECT_SOURCE_DIR}/benchmarks/bench_gpu.c)
  endforeach()

  if(APPLE AND GPU_BUILD_METAL)
    add_executable(gpu-native-metal-render-bench
      ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
      ${PROJECT_SOURCE_DIR}/benchmarks/native_metal_render.m
    )
    target_include_directories(gpu-native-metal-render-bench PRIVATE
      ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(gpu-native-metal-render-bench PRIVATE
      "-framework Foundation"
      "-framework Metal"
    )
    set_target_properties(gpu-native-metal-render-bench PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    target_compile_options(gpu-native-metal-render-bench PRIVATE
      -Wall
      -Wextra
    )
  endif()

  if(GPU_BUILD_VULKAN)
    add_executable(gpu-native-vulkan-render-bench
      ${PROJECT_SOURCE_DIR}/benchmarks/bench.c
      ${PROJECT_SOURCE_DIR}/benchmarks/native_vulkan_render.c
    )
    target_include_directories(gpu-native-vulkan-render-bench PRIVATE
      ${PROJECT_SOURCE_DIR}/include
    )
    target_link_libraries(gpu-native-vulkan-render-bench PRIVATE
      Vulkan::Vulkan
    )
    if(TARGET us)
      target_link_libraries(gpu-native-vulkan-render-bench PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-native-vulkan-render-bench PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-native-vulkan-render-bench PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
    if(MSVC)
      target_compile_options(gpu-native-vulkan-render-bench PRIVATE /W3)
    else()
      target_compile_options(gpu-native-vulkan-render-bench PRIVATE
        -Wall
        -Wextra
      )
    endif()
  endif()

  foreach(benchmark_target
          gpu-static-scene-bench
          gpu-state-churn-bench
          gpu-binding-churn-bench
          gpu-bindless-dispatch-bench
          gpu-bindless-render-bench
          gpu-upload-heavy-bench
          gpu-pipeline-stress-bench)
    add_dependencies(${benchmark_target} gpu-benchmark-fixtures)
  endforeach()
  if(TARGET gpu-native-vulkan-render-bench)
    add_dependencies(gpu-native-vulkan-render-bench gpu-benchmark-fixtures)
  endif()

  if(DEFINED GPU_BENCHMARK_BACKEND)
    set(GPU_BENCHMARK_TEXTURE_UPLOAD_MODE depth)
    if(GPU_BENCHMARK_BACKEND STREQUAL "webgpu")
      set(GPU_BENCHMARK_TEXTURE_UPLOAD_MODE color)
    endif()

    function(gpu_add_perf_smoke name target)
      add_test(NAME ${name} COMMAND ${target} ${ARGN})
      set_tests_properties(${name} PROPERTIES
        LABELS perf-smoke
        RUN_SERIAL TRUE
        TIMEOUT 60
      )
    endfunction()

    gpu_add_perf_smoke(
      perf-smoke-queue-submit
      gpu-queue-submit-bench
      ${GPU_BENCHMARK_BACKEND} 4 64 3
    )
    gpu_add_perf_smoke(
      perf-smoke-ray-dispatch
      gpu-ray-dispatch-bench
      100000
    )
    gpu_add_perf_smoke(
      perf-smoke-queue-upload
      gpu-queue-upload-bench
      ${GPU_BENCHMARK_BACKEND} 64 256
    )
    gpu_add_perf_smoke(
      perf-smoke-texture-upload
      gpu-texture-upload-bench
      ${GPU_BENCHMARK_BACKEND} 8 16 16
      ${GPU_BENCHMARK_TEXTURE_UPLOAD_MODE}
    )
    gpu_add_perf_smoke(
      perf-smoke-static-scene
      gpu-static-scene-bench
      "${GPU_BENCHMARK_RENDER_MRT_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-state-churn
      gpu-state-churn-bench
      "${GPU_BENCHMARK_RENDER_MRT_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-binding-churn
      gpu-binding-churn-bench
      "${GPU_BENCHMARK_BINDING_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-bindless-update
      gpu-bindless-update-bench
      ${GPU_BENCHMARK_BACKEND} 16 32 3
    )
    if(GPU_BENCHMARK_BINDLESS_DISPATCH_US)
      gpu_add_perf_smoke(
        perf-smoke-bindless-dispatch
        gpu-bindless-dispatch-bench
        "${GPU_BENCHMARK_BINDLESS_DISPATCH_US}"
        ${GPU_BENCHMARK_BACKEND} 32 3
      )
      gpu_add_perf_smoke(
        perf-smoke-bindless-render
        gpu-bindless-render-bench
        "${GPU_BENCHMARK_BINDLESS_DISPATCH_US}"
        ${GPU_BENCHMARK_BACKEND} 32 4 12 1
      )
    endif()
    gpu_add_perf_smoke(
      perf-smoke-upload-heavy
      gpu-upload-heavy-bench
      "${GPU_BENCHMARK_TRIANGLE_US}"
      ${GPU_BENCHMARK_BACKEND} 32 4 12 1
    )
    gpu_add_perf_smoke(
      perf-smoke-pipeline-stress
      gpu-pipeline-stress-bench
      "${GPU_BENCHMARK_RENDER_MRT_US}"
      ${GPU_BENCHMARK_BACKEND} 12 1
    )
    if(TARGET gpu-native-metal-render-bench)
      foreach(native_mode static state binding upload)
        gpu_add_perf_smoke(
          perf-smoke-native-metal-${native_mode}
          gpu-native-metal-render-bench
          ${native_mode} 32 4 12 1
        )
      endforeach()
    endif()
    if(TARGET gpu-native-vulkan-render-bench)
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-static
        gpu-native-vulkan-render-bench
        static "${GPU_BENCHMARK_RENDER_MRT_US}" 32 4 12 1
      )
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-state
        gpu-native-vulkan-render-bench
        state "${GPU_BENCHMARK_RENDER_MRT_US}" 32 4 12 1
      )
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-binding
        gpu-native-vulkan-render-bench
        binding "${GPU_BENCHMARK_BINDING_US}" 32 4 12 1
      )
      gpu_add_perf_smoke(
        perf-smoke-native-vulkan-upload
        gpu-native-vulkan-render-bench
        upload "${GPU_BENCHMARK_TRIANGLE_US}" 32 4 12 1
      )
    endif()

    if(GPU_BENCHMARK_BACKEND STREQUAL "vulkan" AND
       DEFINED GPU_VULKAN_ICD AND EXISTS "${GPU_VULKAN_ICD}")
      foreach(perfSmokeTest
              perf-smoke-queue-submit
              perf-smoke-queue-upload
              perf-smoke-texture-upload
              perf-smoke-static-scene
              perf-smoke-state-churn
              perf-smoke-binding-churn
              perf-smoke-bindless-update
              perf-smoke-bindless-dispatch
              perf-smoke-bindless-render
              perf-smoke-upload-heavy
              perf-smoke-pipeline-stress
              perf-smoke-native-vulkan-static
              perf-smoke-native-vulkan-state
              perf-smoke-native-vulkan-binding
              perf-smoke-native-vulkan-upload)
        set_property(TEST ${perfSmokeTest} APPEND PROPERTY ENVIRONMENT
          "VK_ICD_FILENAMES=${GPU_VULKAN_ICD}"
        )
      endforeach()
    endif()
  endif()

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
    add_custom_target(gpu-benchmark-runtime-stage
      ${runtimeStageCommands}
      DEPENDS ${runtimeStageDependencies}
      VERBATIM
    )

    foreach(benchmark_target
            gpu-dispatch-bench
            gpu-ray-dispatch-bench
            gpu-queue-upload-bench
            gpu-queue-submit-bench
            gpu-texture-upload-bench
            gpu-static-scene-bench
            gpu-state-churn-bench
            gpu-binding-churn-bench
            gpu-bindless-update-bench
            gpu-bindless-dispatch-bench
            gpu-bindless-render-bench
            gpu-upload-heavy-bench
            gpu-pipeline-stress-bench
            gpu-validation-bench
            gpu-marker-bench)
      add_dependencies(${benchmark_target} gpu-benchmark-runtime-stage)
    endforeach()
  endif()
endif()
