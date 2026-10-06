include(ExternalProject)

set(GPU_APPLE_GALLERY_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/apple/generated")
set(GPU_APPLE_GALLERY_HOST
    "${PROJECT_SOURCE_DIR}/samples/shell/apple/NativeHost.m")
set(GPU_APPLE_GALLERY_RUNTIME
    "${PROJECT_SOURCE_DIR}/samples/common/apple.m")
set(GPU_APPLE_GALLERY_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/apple/samples")

file(MAKE_DIRECTORY "${GPU_APPLE_GALLERY_GENERATED_DIR}")

if(EXISTS "${GPU_ASSETKIT_ROOT}/CMakeLists.txt")
  get_property(GPU_APPLE_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  set(GPU_ASSETKIT_APPLE_CONFIG_DIR "")
  if(GPU_APPLE_MULTI_CONFIG)
    set(GPU_ASSETKIT_APPLE_CONFIG_DIR "/Release")
  endif()
  set(GPU_ASSETKIT_APPLE_BINARY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/assetkit-apple")
  set(GPU_ASSETKIT_APPLE_LIBRARY
      "${GPU_ASSETKIT_APPLE_BINARY_DIR}${GPU_ASSETKIT_APPLE_CONFIG_DIR}/libassetkit.a")
  set(GPU_ASSETKIT_APPLE_DS_LIBRARY
      "${GPU_ASSETKIT_APPLE_BINARY_DIR}/deps/ds${GPU_ASSETKIT_APPLE_CONFIG_DIR}/libds.a")
  set(GPU_ASSETKIT_APPLE_DEFLATE_LIBRARY
      "${GPU_ASSETKIT_APPLE_BINARY_DIR}${GPU_ASSETKIT_APPLE_CONFIG_DIR}/libdeflate.a")
  ExternalProject_Add(gpu-assetkit-apple
    SOURCE_DIR "${GPU_ASSETKIT_ROOT}"
    BINARY_DIR "${GPU_ASSETKIT_APPLE_BINARY_DIR}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
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
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release
    BUILD_BYPRODUCTS
      "${GPU_ASSETKIT_APPLE_LIBRARY}"
      "${GPU_ASSETKIT_APPLE_DS_LIBRARY}"
      "${GPU_ASSETKIT_APPLE_DEFLATE_LIBRARY}"
    INSTALL_COMMAND ""
  )
endif()

function(gpu_apple_gallery_sample sampleDir)
  set(options ASSETKIT USES_STDLIB)
  set(multiValueArgs CAPS)
  cmake_parse_arguments(GPU_SAMPLE
    "${options}" "" "${multiValueArgs}" ${ARGN})

  get_filename_component(sampleId "${sampleDir}" NAME)
  set(target "gpu-gallery-${sampleId}-metal-usl")
  set(wrapper "${GPU_APPLE_GALLERY_GENERATED_DIR}/${sampleId}.c")

  file(WRITE "${wrapper}"
    "#define main gpu_apple_sample_start\n"
    "#include \"${sampleDir}/main.c\"\n"
  )

  file(GLOB extraSources CONFIGURE_DEPENDS
    "${sampleDir}/*.c"
  )
  list(REMOVE_ITEM extraSources "${sampleDir}/main.c")

  add_executable(${target} MACOSX_BUNDLE
    "${GPU_APPLE_GALLERY_HOST}"
    "${GPU_APPLE_GALLERY_RUNTIME}"
    "${PROJECT_SOURCE_DIR}/samples/common/sample_orbit.c"
    "${wrapper}"
    ${extraSources}
  )
  target_compile_definitions(${target} PRIVATE
    GPU_SAMPLE_GALLERY_APPLE=1
    "GPU_APPLE_SAMPLE_NAME=\"${sampleId}\""
  )
  target_compile_options(${target} PRIVATE -fobjc-arc)
  target_include_directories(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/common"
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  target_link_libraries(${target} PRIVATE
    gpu
    "-framework AppKit"
    "-framework CoreGraphics"
    "-framework Foundation"
    "-framework ImageIO"
    "-framework QuartzCore"
  )
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    MACOSX_BUNDLE_GUI_IDENTIFIER
      "gpu.samples.${sampleId}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER
      "gpu.samples.${sampleId}"
    MACOSX_BUNDLE_BUNDLE_NAME "${sampleId}"
    OUTPUT_NAME "${sampleId}"
    RUNTIME_OUTPUT_DIRECTORY "${GPU_APPLE_GALLERY_OUTPUT_DIR}/${sampleId}"
  )

  if(GPU_SAMPLE_ASSETKIT)
    if(NOT TARGET gpu-assetkit-apple)
      message(FATAL_ERROR
        "Apple AssetKit sample requires GPU_ASSETKIT_ROOT")
    endif()
    target_compile_definitions(${target} PRIVATE AK_STATIC)
    target_include_directories(${target} PRIVATE
      "${GPU_ASSETKIT_ROOT}/include"
    )
    target_link_libraries(${target} PRIVATE
      "${GPU_ASSETKIT_APPLE_LIBRARY}"
      "${GPU_ASSETKIT_APPLE_DS_LIBRARY}"
      "${GPU_ASSETKIT_APPLE_DEFLATE_LIBRARY}"
    )
    add_dependencies(${target} gpu-assetkit-apple)
  endif()

  file(GLOB shaderSources CONFIGURE_DEPENDS "${sampleDir}/*.usl")
  set(resourceDir "$<TARGET_BUNDLE_CONTENT_DIR:${target}>/Resources")
  set(artifactOutputs)
  set(artifactCommands)
  foreach(shaderSource IN LISTS shaderSources)
    get_filename_component(shaderStem "${shaderSource}" NAME_WE)
    set(shaderDir
        "${CMAKE_CURRENT_BINARY_DIR}/apple/artifacts/${sampleId}")
    set(fixtureSource "${shaderDir}/${shaderStem}.usl")
    set(artifact "${shaderDir}/${shaderStem}.us")
    set(shaderEnvironment USL_EMIT_BYTECODE=1 USL_NO_BACKEND_SIDECAR=1)
    set(shaderDependencies)

    if(GPU_SAMPLE_USES_STDLIB)
      list(APPEND shaderEnvironment
        "USL_STDLIB_PATH=${GPU_USL_ROOT}/stdlib")
      gpu_sample_stdlib_dependencies(shaderDependencies
        "${GPU_USL_ROOT}/stdlib")
    endif()
    foreach(capability IN LISTS GPU_SAMPLE_CAPS)
      string(REPLACE "=" ";" capabilityPair "${capability}")
      list(GET capabilityPair 0 capabilityStem)
      if(capabilityStem STREQUAL shaderStem)
        list(GET capabilityPair 1 shaderCaps)
        list(APPEND shaderEnvironment "USL_TARGET_CAPS=${shaderCaps}")
      endif()
    endforeach()

    add_custom_command(
      OUTPUT "${artifact}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${shaderDir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${shaderSource}"
              "${fixtureSource}"
      COMMAND ${CMAKE_COMMAND} -E env
              ${shaderEnvironment}
              $<TARGET_FILE:gpu-usl-fixture>
              metal
              "${fixtureSource}"
      DEPENDS gpu-usl-fixture "${shaderSource}" ${shaderDependencies}
      VERBATIM
    )
    list(APPEND artifactOutputs "${artifact}")
    list(APPEND artifactCommands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${artifact}"
              "${resourceDir}"
      COMMAND ${CMAKE_COMMAND} -E rm -f
              "$<TARGET_FILE_DIR:${target}>/${shaderStem}.us"
    )
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
  foreach(asset IN LISTS sampleAssets)
    get_filename_component(assetName "${asset}" NAME)
    list(APPEND artifactCommands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${asset}"
              "${resourceDir}"
      COMMAND ${CMAKE_COMMAND} -E rm -f
              "$<TARGET_FILE_DIR:${target}>/${assetName}"
    )
  endforeach()

  set(artifactTarget "${target}-artifacts")
  add_custom_target(${artifactTarget}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${resourceDir}"
    ${artifactCommands}
    DEPENDS ${artifactOutputs}
    VERBATIM
  )
  add_dependencies(${target} ${artifactTarget})

  set_property(GLOBAL APPEND PROPERTY
    GPU_APPLE_GALLERY_SAMPLE_IDS "${sampleId}")
  set_property(GLOBAL APPEND PROPERTY
    GPU_APPLE_GALLERY_SAMPLE_TARGETS "${target}")
endfunction()
