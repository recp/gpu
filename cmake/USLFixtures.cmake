if((GPU_BUILD_TESTS OR GPU_BUILD_SAMPLES OR GPU_BUILD_BENCHMARKS) AND
   NOT EMSCRIPTEN)
  add_executable(gpu-usl-fixture tests/usl_fixture.c)
  if(TARGET us)
    target_link_libraries(gpu-usl-fixture PRIVATE us)
  elseif(TARGET gpu_usl)
    target_link_libraries(gpu-usl-fixture PRIVATE gpu_usl)
  endif()
  set_target_properties(gpu-usl-fixture PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  if(GPU_USL_ROOT AND EXISTS "${GPU_USL_ROOT}/tools/uslpack.c")
    add_executable(gpu-uslpack "${GPU_USL_ROOT}/tools/uslpack.c")
    target_include_directories(gpu-uslpack PRIVATE "${GPU_USL_ROOT}/us/include")
    if(TARGET us)
      target_link_libraries(gpu-uslpack PRIVATE us)
    elseif(TARGET gpu_usl)
      target_link_libraries(gpu-uslpack PRIVATE gpu_usl)
    endif()
    set_target_properties(gpu-uslpack PROPERTIES
      OUTPUT_NAME uslpack
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
    )
  endif()

  set(runtimeStageCommands)
  set(runtimeStageDependencies gpu-usl-fixture)
  foreach(runtimeTarget us ds)
    if(TARGET ${runtimeTarget})
      list(APPEND runtimeStageCommands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtimeTarget}>
                $<TARGET_FILE_DIR:gpu-usl-fixture>
      )
      list(APPEND runtimeStageDependencies ${runtimeTarget})
    endif()
  endforeach()
  add_custom_target(gpu-usl-runtime-stage
    ${runtimeStageCommands}
    DEPENDS ${runtimeStageDependencies}
  )

  function(gpu_add_usl_fixtures outVar backend group)
    set(targetCapsEnvironment)
    if(DEFINED GPU_USL_FIXTURE_TARGET_CAPS)
      list(APPEND targetCapsEnvironment
        "USL_TARGET_CAPS=${GPU_USL_FIXTURE_TARGET_CAPS}"
      )
    endif()
    set(outputs)
    foreach(source IN ITEMS ${ARGN})
      get_filename_component(name "${source}" NAME_WE)
      set(outputDir "${CMAKE_CURRENT_BINARY_DIR}/usl/${backend}/${group}")
      set(fixtureSource "${outputDir}/${name}.usl")
      set(fixtureOutput "${outputDir}/${name}.us")
      add_custom_command(
        OUTPUT "${fixtureOutput}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${outputDir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${source}"
                "${fixtureSource}"
        COMMAND ${CMAKE_COMMAND} -E env
                USL_EMIT_BYTECODE=1
                USL_NO_BACKEND_SIDECAR=1
                ${targetCapsEnvironment}
                $<TARGET_FILE:gpu-usl-fixture>
                "${backend}"
                "${fixtureSource}"
        DEPENDS gpu-usl-runtime-stage
                $<TARGET_FILE:gpu-usl-fixture>
                "${source}"
        VERBATIM
      )
      list(APPEND outputs "${fixtureOutput}")
    endforeach()
    set(${outVar} "${outputs}" PARENT_SCOPE)
  endfunction()

  function(gpu_attach_usl_validation target fixtureTarget)
    set_target_properties(${target} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY
        "${PROJECT_BINARY_DIR}/validation/${target}"
    )
    set(artifactTarget "${target}-artifacts")
    set(artifactStamp
      "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${artifactTarget}.stamp"
    )
    set(artifactDependencies ${ARGN})
    set(artifactCommands
      COMMAND ${CMAKE_COMMAND} -E make_directory
              $<TARGET_FILE_DIR:${target}>
    )
    if(WIN32)
      foreach(runtimeTarget gpu us ds)
        if(TARGET ${runtimeTarget})
          list(APPEND artifactDependencies $<TARGET_FILE:${runtimeTarget}>)
          list(APPEND artifactCommands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${runtimeTarget}>
                    $<TARGET_FILE_DIR:${target}>
          )
        endif()
      endforeach()
      if(GPU_BUILD_DX12 AND COMMAND gpu_stage_dx12_agility)
        gpu_stage_dx12_agility(${target})
      endif()
      if(GPU_BUILD_DX12 AND COMMAND gpu_stage_dx12_dxc)
        gpu_stage_dx12_dxc(${target})
      endif()
    endif()
    foreach(artifact IN ITEMS ${ARGN})
      list(APPEND artifactCommands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${artifact}"
                $<TARGET_FILE_DIR:${target}>
      )
    endforeach()
    list(APPEND artifactCommands
      COMMAND ${CMAKE_COMMAND} -E touch "${artifactStamp}"
    )
    add_custom_command(
      OUTPUT "${artifactStamp}"
      ${artifactCommands}
      DEPENDS ${artifactDependencies}
      VERBATIM
    )
    add_custom_target(${artifactTarget} DEPENDS "${artifactStamp}")
    add_dependencies(${artifactTarget} ${fixtureTarget})
    add_dependencies(${target} ${artifactTarget})
    if(WIN32)
      foreach(runtimeTarget gpu us ds)
        if(TARGET ${runtimeTarget})
          add_dependencies(${artifactTarget} ${runtimeTarget})
        endif()
      endforeach()
    endif()
  endfunction()
endif()
