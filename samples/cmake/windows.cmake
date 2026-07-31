include(ExternalProject)

set(GPU_WINDOWS_GALLERY_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/windows/generated")
set(GPU_WINDOWS_GALLERY_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/windows")
set(GPU_WINDOWS_GALLERY_RUNTIME_DIR
    "${GPU_WINDOWS_GALLERY_OUTPUT_DIR}/bin")
file(MAKE_DIRECTORY "${GPU_WINDOWS_GALLERY_GENERATED_DIR}")

set(gpuWindowsRuntimeCommands
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${GPU_WINDOWS_GALLERY_RUNTIME_DIR}/$<CONFIG>"
)
set(gpuWindowsRuntimeTargets)
foreach(runtimeTarget gpu us ds)
  if(NOT TARGET ${runtimeTarget})
    continue()
  endif()

  get_target_property(runtimeType ${runtimeTarget} TYPE)
  if(runtimeType STREQUAL "SHARED_LIBRARY" OR
     runtimeType STREQUAL "MODULE_LIBRARY")
    list(APPEND gpuWindowsRuntimeCommands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:${runtimeTarget}>
              "${GPU_WINDOWS_GALLERY_RUNTIME_DIR}/$<CONFIG>"
    )
    list(APPEND gpuWindowsRuntimeTargets ${runtimeTarget})
  endif()
endforeach()
if(GPU_DX12_AGILITY_CORE)
  list(APPEND gpuWindowsRuntimeCommands
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WINDOWS_GALLERY_RUNTIME_DIR}/$<CONFIG>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_AGILITY_CORE}"
            "${GPU_WINDOWS_GALLERY_RUNTIME_DIR}/$<CONFIG>/D3D12/D3D12Core.dll"
  )
  if(GPU_BUILD_WITH_VALIDATION)
    list(APPEND gpuWindowsRuntimeCommands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${GPU_DX12_AGILITY_LAYERS}"
              "${GPU_WINDOWS_GALLERY_RUNTIME_DIR}/$<CONFIG>/D3D12/d3d12SDKLayers.dll"
    )
  endif()
endif()
add_custom_target(gpu-gallery-windows-runtime
  ${gpuWindowsRuntimeCommands}
  VERBATIM
)
if(gpuWindowsRuntimeTargets)
  add_dependencies(gpu-gallery-windows-runtime
    ${gpuWindowsRuntimeTargets}
  )
endif()

if(EXISTS "${GPU_ASSETKIT_ROOT}/CMakeLists.txt")
  set(GPU_ASSETKIT_WINDOWS_BINARY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/assetkit-windows")
  set(GPU_ASSETKIT_WINDOWS_INSTALL_DIR
      "${GPU_ASSETKIT_WINDOWS_BINARY_DIR}/install")
  set(GPU_ASSETKIT_WINDOWS_LIBRARY
      "${GPU_ASSETKIT_WINDOWS_INSTALL_DIR}/lib/assetkit.lib")
  set(GPU_ASSETKIT_WINDOWS_DS_LIBRARY
      "${GPU_ASSETKIT_WINDOWS_INSTALL_DIR}/lib/ds.lib")
  if(CMAKE_CONFIGURATION_TYPES)
    set(GPU_ASSETKIT_WINDOWS_DEFLATE_LIBRARY
        "${GPU_ASSETKIT_WINDOWS_BINARY_DIR}/build/$<CONFIG>/deflatestatic.lib")
  else()
    set(GPU_ASSETKIT_WINDOWS_DEFLATE_LIBRARY
        "${GPU_ASSETKIT_WINDOWS_BINARY_DIR}/build/deflatestatic.lib")
  endif()
  ExternalProject_Add(gpu-assetkit-windows
    SOURCE_DIR "${GPU_ASSETKIT_ROOT}"
    BINARY_DIR "${GPU_ASSETKIT_WINDOWS_BINARY_DIR}/build"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_GENERATOR_PLATFORM "${CMAKE_GENERATOR_PLATFORM}"
    CMAKE_ARGS
      "-DCMAKE_INSTALL_PREFIX=${GPU_ASSETKIT_WINDOWS_INSTALL_DIR}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      -DAK_SHARED=OFF
      -DAK_STATIC=ON
      -DAK_BUILD_EXPORTERS=OFF
      -DAK_BUILD_DECODER_SHIMS=OFF
      -DAK_FETCH_DEPS=OFF
      -DAK_BUILD_CLI=OFF
      -DAK_BUILD_TOOLS=OFF
      -DAK_BUILD_SAMPLES=OFF
      -DAK_USE_TEST=OFF
      -DAK_ENABLE_LTO=OFF
      -DGIT_SUBMODULE=OFF
    BUILD_COMMAND
      "${CMAKE_COMMAND}" --build <BINARY_DIR> --config $<CONFIG>
    INSTALL_COMMAND
      "${CMAKE_COMMAND}" --install <BINARY_DIR> --config $<CONFIG>
    BUILD_BYPRODUCTS
      "${GPU_ASSETKIT_WINDOWS_LIBRARY}"
      "${GPU_ASSETKIT_WINDOWS_DS_LIBRARY}"
      "${GPU_ASSETKIT_WINDOWS_DEFLATE_LIBRARY}"
  )
endif()

function(gpu_windows_gallery_artifact sampleId
         shaderSource
         usesStdlib
         shaderCaps
         shaderFallbackCaps
         outArtifact)
  get_filename_component(shaderStem "${shaderSource}" NAME_WE)
  set(shaderDir
      "${CMAKE_CURRENT_BINARY_DIR}/windows/artifacts/${sampleId}")
  set(fixtureSource "${shaderDir}/${shaderStem}.usl")
  set(artifact "${shaderDir}/${shaderStem}.us")
  set(shaderEnvironment USL_EMIT_BYTECODE=1 USL_NO_BACKEND_SIDECAR=1)
  set(runtimeCommands)
  set(runtimeDependencies)

  if(usesStdlib)
    list(APPEND shaderEnvironment
      "USL_STDLIB_PATH=${GPU_USL_ROOT}/stdlib")
  endif()
  if(shaderCaps)
    list(APPEND shaderEnvironment "USL_TARGET_CAPS=${shaderCaps}")
  endif()
  foreach(runtimeTarget us ds)
    if(TARGET ${runtimeTarget})
      list(APPEND runtimeCommands
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${runtimeTarget}>
                $<TARGET_FILE_DIR:gpu-usl-fixture>
      )
      list(APPEND runtimeDependencies $<TARGET_FILE:${runtimeTarget}>)
    endif()
  endforeach()

  add_custom_command(
    OUTPUT "${artifact}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${shaderDir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${shaderSource}"
            "${fixtureSource}"
    ${runtimeCommands}
    COMMAND ${CMAKE_COMMAND} -E env
            ${shaderEnvironment}
            $<TARGET_FILE:gpu-usl-fixture>
            dx12
            "${fixtureSource}"
    DEPENDS gpu-usl-fixture "${shaderSource}" ${runtimeDependencies}
    VERBATIM
  )
  set(${outArtifact} "${artifact}" PARENT_SCOPE)
endfunction()

function(gpu_windows_gallery_sample sampleDir)
  set(options ASSETKIT USES_STDLIB)
  set(multiValueArgs CAPS FALLBACK_CAPS)
  cmake_parse_arguments(GPU_SAMPLE
    "${options}" "" "${multiValueArgs}" ${ARGN})

  get_filename_component(sampleId "${sampleDir}" NAME)
  file(GLOB sampleSources CONFIGURE_DEPENDS "${sampleDir}/*.c")
  list(REMOVE_ITEM sampleSources "${sampleDir}/main.c")
  file(GLOB shaderSources CONFIGURE_DEPENDS "${sampleDir}/*.usl")
  set(shaderArtifacts)

  foreach(shaderSource IN LISTS shaderSources)
    get_filename_component(shaderStem "${shaderSource}" NAME_WE)
    set(shaderCaps)
    set(shaderFallbackCaps)
    foreach(capability IN LISTS GPU_SAMPLE_CAPS)
      string(REPLACE "=" ";" capabilityPair "${capability}")
      list(GET capabilityPair 0 capabilityStem)
      if(capabilityStem STREQUAL shaderStem)
        list(GET capabilityPair 1 shaderCaps)
      endif()
    endforeach()
    foreach(capability IN LISTS GPU_SAMPLE_FALLBACK_CAPS)
      string(REPLACE "=" ";" capabilityPair "${capability}")
      list(GET capabilityPair 0 capabilityStem)
      if(capabilityStem STREQUAL shaderStem)
        list(GET capabilityPair 1 shaderFallbackCaps)
      endif()
    endforeach()

    gpu_windows_gallery_artifact(
      "${sampleId}"
      "${shaderSource}"
      "${GPU_SAMPLE_USES_STDLIB}"
      "${shaderCaps}"
      "${shaderFallbackCaps}"
      artifact
    )
    list(APPEND shaderArtifacts "${artifact}")
  endforeach()

  file(GLOB sampleAssets CONFIGURE_DEPENDS
    "${sampleDir}/*.astc"
    "${sampleDir}/*.bc1"
    "${sampleDir}/*.etc2"
    "${sampleDir}/*.png"
    "${sampleDir}/*.rgba"
    "${sampleDir}/*.rgba16f"
  )
  if(GPU_SAMPLE_ASSETKIT)
    list(APPEND sampleAssets
      "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/lut_ggx.png"
      "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_diffuse.rgba16f"
      "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_specular.rgba16f"
    )
  endif()

  set(target "gpu-gallery-${sampleId}-dx12-usl")
  set(wrapper
      "${GPU_WINDOWS_GALLERY_GENERATED_DIR}/${sampleId}.c")
  file(WRITE "${wrapper}"
    "#define main gpu_win32_sample_start\n"
    "#include \"${sampleDir}/main.c\"\n"
  )
  set_property(SOURCE "${wrapper}" APPEND PROPERTY
    OBJECT_DEPENDS "${sampleDir}/main.c"
  )

  add_executable(${target} WIN32
    "${PROJECT_SOURCE_DIR}/samples/shell/windows/NativeHost.c"
    "${PROJECT_SOURCE_DIR}/samples/common/win32.c"
    "${PROJECT_SOURCE_DIR}/samples/common/Win32Image.c"
    "${PROJECT_SOURCE_DIR}/samples/common/sample_orbit.c"
    "${wrapper}"
    ${sampleSources}
  )
  target_compile_definitions(${target} PRIVATE
    GPU_SAMPLE_GALLERY_WINDOWS=1
    _CRT_SECURE_NO_WARNINGS
    "GPU_WINDOWS_SAMPLE_NAME=\"${sampleId}\""
  )
  target_include_directories(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/common"
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  target_link_libraries(${target} PRIVATE
    gpu
    gdi32
    ole32
    user32
    windowscodecs
    winhttp
  )
  target_compile_options(${target} PRIVATE /W3)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WINDOWS_GALLERY_RUNTIME_DIR}/$<CONFIG>"
  )
  add_dependencies(${target} gpu-gallery-windows-runtime)

  if(GPU_SAMPLE_ASSETKIT)
    if(NOT TARGET gpu-assetkit-windows)
      message(FATAL_ERROR
        "Windows AssetKit sample requires GPU_ASSETKIT_ROOT")
    endif()
    target_compile_definitions(${target} PRIVATE AK_STATIC)
    target_include_directories(${target} PRIVATE
      "${GPU_ASSETKIT_ROOT}/include"
    )
    target_link_libraries(${target} PRIVATE
      "${GPU_ASSETKIT_WINDOWS_LIBRARY}"
      "${GPU_ASSETKIT_WINDOWS_DS_LIBRARY}"
      "${GPU_ASSETKIT_WINDOWS_DEFLATE_LIBRARY}"
    )
    add_dependencies(${target} gpu-assetkit-windows)
  endif()

  set(stageTarget "${target}-artifacts")
  set(stageStamp
      "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${stageTarget}-bin.stamp")
  set(stageCommands
    COMMAND ${CMAKE_COMMAND} -E make_directory
            $<TARGET_FILE_DIR:${target}>
  )
  foreach(artifact IN LISTS shaderArtifacts)
    list(APPEND stageCommands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${artifact}"
              $<TARGET_FILE_DIR:${target}>
    )
  endforeach()
  foreach(asset IN LISTS sampleAssets)
    list(APPEND stageCommands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${asset}"
              $<TARGET_FILE_DIR:${target}>
    )
  endforeach()
  list(APPEND stageCommands
    COMMAND ${CMAKE_COMMAND} -E touch "${stageStamp}"
  )
  add_custom_command(
    OUTPUT "${stageStamp}"
    ${stageCommands}
    DEPENDS ${shaderArtifacts} ${sampleAssets}
    VERBATIM
  )
  add_custom_target(${stageTarget} DEPENDS "${stageStamp}")
  add_dependencies(${target} ${stageTarget})

  set_property(GLOBAL APPEND PROPERTY
    GPU_WINDOWS_GALLERY_SAMPLE_IDS "${sampleId}")
  set_property(GLOBAL APPEND PROPERTY
    GPU_WINDOWS_GALLERY_SAMPLE_TARGETS "${target}")
endfunction()
