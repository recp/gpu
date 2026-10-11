add_executable(gpu-constant-lookup-test
  constants_lookup.c
  ${PROJECT_SOURCE_DIR}/src/api/constants.c
)

target_include_directories(gpu-constant-lookup-test PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${PROJECT_SOURCE_DIR}/src
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
