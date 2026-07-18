if(NOT GPU_PACKAGE_BUILD_DIR OR
   NOT GPU_PACKAGE_SOURCE_DIR OR
   NOT GPU_PACKAGE_TEST_DIR)
  message(FATAL_ERROR "GPU package test paths are required")
endif()

set(package_prefix "${GPU_PACKAGE_TEST_DIR}/prefix")
set(consumer_build "${GPU_PACKAGE_TEST_DIR}/build")
file(REMOVE_RECURSE "${GPU_PACKAGE_TEST_DIR}")
file(MAKE_DIRECTORY "${GPU_PACKAGE_TEST_DIR}")

set(config_args)
if(GPU_PACKAGE_CONFIG)
  list(APPEND config_args --config "${GPU_PACKAGE_CONFIG}")
endif()

set(generator_args)
if(GPU_PACKAGE_GENERATOR)
  list(APPEND generator_args -G "${GPU_PACKAGE_GENERATOR}")
endif()
if(GPU_PACKAGE_GENERATOR_PLATFORM)
  list(APPEND generator_args -A "${GPU_PACKAGE_GENERATOR_PLATFORM}")
endif()
if(GPU_PACKAGE_GENERATOR_TOOLSET)
  list(APPEND generator_args -T "${GPU_PACKAGE_GENERATOR_TOOLSET}")
endif()
if(GPU_PACKAGE_MAKE_PROGRAM)
  list(APPEND generator_args
       "-DCMAKE_MAKE_PROGRAM=${GPU_PACKAGE_MAKE_PROGRAM}")
endif()

set(build_type_arg)
if(NOT GPU_PACKAGE_MULTI_CONFIG AND GPU_PACKAGE_CONFIG)
  set(build_type_arg "-DCMAKE_BUILD_TYPE=${GPU_PACKAGE_CONFIG}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${GPU_PACKAGE_BUILD_DIR}"
          --prefix "${package_prefix}" ${config_args}
  RESULT_VARIABLE result
)
if(result)
  message(FATAL_ERROR "GPU package installation failed: ${result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}"
          -S "${GPU_PACKAGE_SOURCE_DIR}/tests/package"
          -B "${consumer_build}"
          ${generator_args}
          "-DCMAKE_PREFIX_PATH=${package_prefix}"
          ${build_type_arg}
  RESULT_VARIABLE result
)
if(result)
  message(FATAL_ERROR "GPU package consumer configuration failed: ${result}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_build}" ${config_args}
  RESULT_VARIABLE result
)
if(result)
  message(FATAL_ERROR "GPU package consumer build failed: ${result}")
endif()

set(executable_suffix)
if(WIN32)
  set(executable_suffix ".exe")
endif()
set(consumer "${consumer_build}/gpu-package-consumer${executable_suffix}")
if(GPU_PACKAGE_CONFIG AND NOT EXISTS "${consumer}")
  set(consumer
      "${consumer_build}/${GPU_PACKAGE_CONFIG}/gpu-package-consumer${executable_suffix}")
endif()
if(NOT EXISTS "${consumer}")
  message(FATAL_ERROR "GPU package consumer executable was not created")
endif()

if(WIN32)
  set(ENV{PATH}
      "${package_prefix}/bin;${package_prefix}/lib;$ENV{PATH}")
else()
  set(ENV{PATH}
      "${package_prefix}/bin:${package_prefix}/lib:$ENV{PATH}")
endif()
execute_process(
  COMMAND "${consumer}"
  RESULT_VARIABLE result
)
if(result)
  message(FATAL_ERROR "GPU package consumer execution failed: ${result}")
endif()
