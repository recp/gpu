if(UNIX AND NOT EMSCRIPTEN)
  add_executable(gpu-frame-feedback-test
    frame_feedback.c
    "${PROJECT_SOURCE_DIR}/src/api/frame_feedback.c"
  )

  target_include_directories(gpu-frame-feedback-test PRIVATE "${PROJECT_SOURCE_DIR}/include")
  target_link_libraries(gpu-frame-feedback-test PRIVATE Threads::Threads)
  set_target_properties(gpu-frame-feedback-test PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
  target_compile_options(gpu-frame-feedback-test PRIVATE -Wall -Wextra -Werror)

  add_test(NAME api-frame-feedback COMMAND gpu-frame-feedback-test)
  set_tests_properties(api-frame-feedback PROPERTIES LABELS "frame-feedback;threading" TIMEOUT 20)
endif()

if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gpu-metal-uploader-layout-test metal_uploader.m)

  target_link_libraries(gpu-metal-uploader-layout-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-uploader-layout-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")

  foreach(mode classic metal4)
    add_test(NAME metal-uploader-layout-${mode} COMMAND gpu-metal-uploader-layout-test)
    set_tests_properties(metal-uploader-layout-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "upload;layout" TIMEOUT 30 RUN_SERIAL TRUE)
  endforeach()
endif()

if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(GPU_USL_FIXTURE_TARGET_CAPS framebuffer_fetch)
  gpu_add_usl_fixtures(GPU_TILE_FIXTURE metal api-tile
    "${CMAKE_CURRENT_SOURCE_DIR}/tile_shader.usl"
  )
  gpu_add_usl_fixtures(GPU_TILE_RESOURCE_FIXTURE metal api-tile-resources
    "${CMAKE_CURRENT_SOURCE_DIR}/tile_resources.usl"
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS framebuffer_fetch,descriptor_indexing)
  gpu_add_usl_fixtures(GPU_TILE_ARRAY_FIXTURE metal api-tile-arrays
    "${CMAKE_CURRENT_SOURCE_DIR}/tile_array_resources.usl"
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS imageblock)
  gpu_add_usl_fixtures(GPU_IMAGEBLOCK_FIXTURE metal api-imageblock
    "${CMAKE_CURRENT_SOURCE_DIR}/imageblock_shader.usl"
  )
  set(GPU_USL_FIXTURE_TARGET_CAPS imageblock,tile_shader)
  gpu_add_usl_fixtures(GPU_TILE_KERNEL_FIXTURE metal api-tile-kernel
    "${CMAKE_CURRENT_SOURCE_DIR}/tile_kernel.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)

  add_executable(gpu-metal-tile-test metal_tile.m
    ${GPU_TILE_FIXTURE} ${GPU_IMAGEBLOCK_FIXTURE} ${GPU_TILE_RESOURCE_FIXTURE} ${GPU_TILE_ARRAY_FIXTURE})

  target_link_libraries(gpu-metal-tile-test PRIVATE gpu)
  target_compile_options(gpu-metal-tile-test PRIVATE -fobjc-arc)
  gpu_link_apple_framework(gpu-metal-tile-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")

  foreach(mode classic metal4)
    add_test(NAME metal-tile-${mode}
      COMMAND gpu-metal-tile-test ${GPU_TILE_FIXTURE} ${GPU_IMAGEBLOCK_FIXTURE} ${GPU_TILE_RESOURCE_FIXTURE} ${GPU_TILE_ARRAY_FIXTURE})
    set_tests_properties(metal-tile-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "tile;imageblock" TIMEOUT 30 RUN_SERIAL TRUE)
  endforeach()

  add_executable(gpu-metal-tile-kernel-test metal_tile_kernel.m ${GPU_TILE_KERNEL_FIXTURE})

  target_link_libraries(gpu-metal-tile-kernel-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-tile-kernel-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")
  gpu_link_apple_framework(gpu-metal-tile-kernel-test Metal "${GPU_FRAMEWORK_METAL}")

  foreach(mode classic metal4)
    add_test(NAME metal-tile-kernel-${mode}
      COMMAND gpu-metal-tile-kernel-test ${GPU_TILE_KERNEL_FIXTURE})
    set_tests_properties(metal-tile-kernel-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "tile;imageblock;kernel" TIMEOUT 30 RUN_SERIAL TRUE)
  endforeach()
endif()

if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(GPU_USL_FIXTURE_TARGET_CAPS framebuffer_fetch)
  gpu_add_usl_fixtures(GPU_FRAMEBUFFER_FETCH_FIXTURE metal api-framebuffer-fetch
    "${CMAKE_CURRENT_SOURCE_DIR}/framebuffer_fetch.usl"
  )
  unset(GPU_USL_FIXTURE_TARGET_CAPS)

  add_executable(gpu-metal-framebuffer-fetch-test
    metal_framebuffer_fetch.m
    ${GPU_FRAMEBUFFER_FETCH_FIXTURE}
  )

  target_link_libraries(gpu-metal-framebuffer-fetch-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-framebuffer-fetch-test Metal "${GPU_FRAMEWORK_METAL}")
  gpu_link_apple_framework(gpu-metal-framebuffer-fetch-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")

  foreach(mode classic metal4)
    add_test(NAME metal-framebuffer-fetch-${mode}
      COMMAND gpu-metal-framebuffer-fetch-test ${GPU_FRAMEBUFFER_FETCH_FIXTURE})
    set_tests_properties(metal-framebuffer-fetch-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "framebuffer-fetch" TIMEOUT 30 RUN_SERIAL TRUE)
  endforeach()

endif()

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

if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gpu-metal-frame-pacer-test metal_frame_pacer.m)

  target_link_libraries(gpu-metal-frame-pacer-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-frame-pacer-test AppKit "${GPU_FRAMEWORK_UI}")
  gpu_link_apple_framework(gpu-metal-frame-pacer-test Metal "${GPU_FRAMEWORK_METAL}")
  gpu_link_apple_framework(gpu-metal-frame-pacer-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")
  gpu_link_apple_framework(gpu-metal-frame-pacer-test QuartzCore "${GPU_FRAMEWORK_QUARTZCORE}")

  foreach(mode classic metal4)
    add_test(NAME metal-frame-pacer-${mode} COMMAND gpu-metal-frame-pacer-test)
    set_tests_properties(metal-frame-pacer-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "frame-pacer" TIMEOUT 20 RUN_SERIAL TRUE)
  endforeach()

endif()

if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gpu-metal-renderpass-lifetime-test metal_renderpass_lifetime.m)

  target_include_directories(gpu-metal-renderpass-lifetime-test PRIVATE ${PROJECT_SOURCE_DIR}/src)

  target_link_libraries(gpu-metal-renderpass-lifetime-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-renderpass-lifetime-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")
  gpu_link_apple_framework(gpu-metal-renderpass-lifetime-test Metal "${GPU_FRAMEWORK_METAL}")

  foreach(mode classic metal4)
    add_test(NAME metal-renderpass-lifetime-${mode} COMMAND gpu-metal-renderpass-lifetime-test)
    set_tests_properties(metal-renderpass-lifetime-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "renderpass;memory" TIMEOUT 20 RUN_SERIAL TRUE)
  endforeach()
endif()

if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gpu-metal-submit-options-test metal_submit_options.m)

  target_compile_options(gpu-metal-submit-options-test PRIVATE -Wall -Wextra -Werror)

  target_link_libraries(gpu-metal-submit-options-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-submit-options-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")
  gpu_link_apple_framework(gpu-metal-submit-options-test Metal "${GPU_FRAMEWORK_METAL}")

  foreach(mode classic metal4)
    add_test(NAME metal-submit-options-${mode} COMMAND gpu-metal-submit-options-test)
    set_tests_properties(metal-submit-options-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      LABELS "queue;completion;memory" SKIP_RETURN_CODE 77 TIMEOUT 30 RUN_SERIAL TRUE)
  endforeach()

  add_executable(gpu-metal-residency-test metal_residency.m)

  target_link_libraries(gpu-metal-residency-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-residency-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")
  gpu_link_apple_framework(gpu-metal-residency-test Metal "${GPU_FRAMEWORK_METAL}")

  foreach(mode classic metal4)
    add_test(NAME metal-residency-${mode} COMMAND gpu-metal-residency-test)
    set_tests_properties(metal-residency-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "residency;memory" TIMEOUT 20 RUN_SERIAL TRUE)
  endforeach()
endif()

if(GPU_BUILD_METAL AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  if(NOT BUILD_SHARED_LIBS)
    add_executable(gpu-metal-completion-timing-test metal_completion_timing.m)

    target_compile_definitions(gpu-metal-completion-timing-test PRIVATE
      $<TARGET_PROPERTY:gpu,COMPILE_DEFINITIONS>)
    target_link_libraries(gpu-metal-completion-timing-test PRIVATE gpu)
    target_link_options(gpu-metal-completion-timing-test PRIVATE -Wl,-dead_strip)
    set_target_properties(gpu-metal-completion-timing-test PROPERTIES
      OBJC_STANDARD 11 OBJC_STANDARD_REQUIRED YES OBJC_EXTENSIONS NO)

    add_test(NAME metal-completion-timing COMMAND gpu-metal-completion-timing-test)
    set_tests_properties(metal-completion-timing PROPERTIES LABELS "frame-feedback;timing" TIMEOUT 10)
  endif()

  add_executable(gpu-metal-frame-feedback-test metal_frame_feedback.m)

  target_link_libraries(gpu-metal-frame-feedback-test PRIVATE gpu)
  gpu_link_apple_framework(gpu-metal-frame-feedback-test AppKit "${GPU_FRAMEWORK_UI}")
  gpu_link_apple_framework(gpu-metal-frame-feedback-test Foundation "${GPU_FRAMEWORK_FOUNDATION}")
  gpu_link_apple_framework(gpu-metal-frame-feedback-test Metal "${GPU_FRAMEWORK_METAL}")
  gpu_link_apple_framework(gpu-metal-frame-feedback-test QuartzCore "${GPU_FRAMEWORK_QUARTZCORE}")

  foreach(mode classic metal4)
    add_test(NAME metal-frame-feedback-${mode} COMMAND gpu-metal-frame-feedback-test)
    set_tests_properties(metal-frame-feedback-${mode} PROPERTIES
      ENVIRONMENT "GPU_METAL_MODE=${mode};MTL_DEBUG_LAYER=1"
      SKIP_RETURN_CODE 77 LABELS "frame-feedback;timing" TIMEOUT 30 RUN_SERIAL TRUE)
  endforeach()
endif()
