add_executable(gpu-constant-lookup-test
  constants_lookup.c
  ${PROJECT_SOURCE_DIR}/src/api/constants.c
)

target_include_directories(gpu-constant-lookup-test PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(gpu-constant-lookup-test PRIVATE
  GPU_STATIC
  calloc=constantTestCalloc
  free=constantTestFree
)

if(TARGET us)
  target_link_libraries(gpu-constant-lookup-test PRIVATE us)
else()
  target_link_libraries(gpu-constant-lookup-test PRIVATE gpu_usl)
endif()

set_target_properties(gpu-constant-lookup-test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
)

if(NOT MSVC)
  target_compile_options(gpu-constant-lookup-test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME api-constant-lookup COMMAND gpu-constant-lookup-test)

add_executable(gpu-pipeline-key-test
  pipeline_key.c
  ${PROJECT_SOURCE_DIR}/src/api/constants.c
  ${PROJECT_SOURCE_DIR}/src/api/pipeline_cache.c
)

target_include_directories(gpu-pipeline-key-test PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(gpu-pipeline-key-test PRIVATE
  GPU_STATIC
  malloc=pipelineKeyTestAllocate
  free=pipelineKeyTestFree
)

target_link_libraries(gpu-pipeline-key-test PRIVATE gpu)

if(NOT WIN32)
  find_package(Threads REQUIRED)

  target_link_libraries(gpu-pipeline-key-test PRIVATE Threads::Threads)
endif()

if(TARGET us)
  target_link_libraries(gpu-pipeline-key-test PRIVATE us)
else()
  target_link_libraries(gpu-pipeline-key-test PRIVATE gpu_usl)
endif()

set_target_properties(gpu-pipeline-key-test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
)

gpu_enable_c11_atomics(gpu-pipeline-key-test)

if(NOT MSVC)
  target_compile_options(gpu-pipeline-key-test PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME api-pipeline-key COMMAND gpu-pipeline-key-test)

if(GPU_BUILD_METAL)
  gpu_add_usl_fixtures(GPU_MATH_POLICY_FIXTURE metal api-math-policy
    "${CMAKE_CURRENT_SOURCE_DIR}/compute.usl"
  )

  add_executable(gpu-math-policy-test math_policy.c ${GPU_MATH_POLICY_FIXTURE})

  target_link_libraries(gpu-math-policy-test PRIVATE gpu)

  set_target_properties(gpu-math-policy-test PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  if(NOT MSVC)
    target_compile_options(gpu-math-policy-test PRIVATE -Wall -Wextra -Werror)
  endif()

  foreach(mode default strict relaxed fast-alias metal-strict backend-strict)
    set(strict 0)
    set(relaxed 0)
    set(fast 0)
    set(metalStrict 0)
    set(backendStrict 0)
    set(expected safe)

    if(mode STREQUAL "strict")
      set(strict 1)
    elseif(mode STREQUAL "relaxed")
      set(relaxed 1)
      set(expected relaxed)
    elseif(mode STREQUAL "fast-alias")
      set(fast 1)
      set(expected relaxed)
    elseif(mode STREQUAL "metal-strict")
      set(metalStrict 1)
      set(relaxed 1)
    elseif(mode STREQUAL "backend-strict")
      set(backendStrict 1)
    endif()

    add_test(NAME api-math-policy-${mode}
      COMMAND gpu-math-policy-test ${GPU_MATH_POLICY_FIXTURE} ${expected}
    )

    set_tests_properties(api-math-policy-${mode} PROPERTIES
      ENVIRONMENT
        "USL_STRICT_IEEE=${strict};USL_RELAXED_FP=${relaxed};USL_FAST_MATH=${fast};USL_METAL_STRICT_IEEE=${metalStrict};USL_BACKEND_STRICT_IEEE=${backendStrict}"
      LABELS "usl;metal;math-policy"
      TIMEOUT 60
    )
  endforeach()
endif()
