set(GPU_ANDROID_GALLERY_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/android/gpu-android-gallery")
set(GPU_ANDROID_GALLERY_ASSET_DIR
    "${GPU_ANDROID_GALLERY_DIR}/assets")
set(GPU_ANDROID_GALLERY_GENERATED_DIR
    "${GPU_ANDROID_GALLERY_DIR}/generated")
set(GPU_ANDROID_GALLERY_CLASSES
    "${GPU_ANDROID_GALLERY_DIR}/classes")
set(GPU_ANDROID_GALLERY_DEX_DIR
    "${GPU_ANDROID_GALLERY_DIR}/dex")
set(GPU_ANDROID_GALLERY_DEX
    "${GPU_ANDROID_GALLERY_DEX_DIR}/classes.dex")
set(GPU_ANDROID_GALLERY_JAR
    "${GPU_ANDROID_GALLERY_DIR}/gallery.jar")
set(GPU_ANDROID_GALLERY_JAVA_ROOT
    "${PROJECT_SOURCE_DIR}/samples/shell/android/java")
set(GPU_ANDROID_GALLERY_JAVA
    "${GPU_ANDROID_GALLERY_JAVA_ROOT}/gpu/samples/GalleryActivity.java"
    "${GPU_ANDROID_GALLERY_JAVA_ROOT}/gpu/samples/SampleActivity.java")

file(MAKE_DIRECTORY "${GPU_ANDROID_GALLERY_GENERATED_DIR}")

if(EXISTS "${GPU_ASSETKIT_ROOT}/CMakeLists.txt")
  include(ExternalProject)

  set(GPU_ASSETKIT_ANDROID_BINARY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/assetkit-android")
  set(GPU_ASSETKIT_ANDROID_LIBRARY
      "${GPU_ASSETKIT_ANDROID_BINARY_DIR}/libassetkit.a")
  set(GPU_ASSETKIT_ANDROID_DS_LIBRARY
      "${GPU_ASSETKIT_ANDROID_BINARY_DIR}/deps/ds/libds.a")
  set(GPU_ASSETKIT_ANDROID_DEFLATE_LIBRARY
      "${GPU_ASSETKIT_ANDROID_BINARY_DIR}/libdeflate.a")
  ExternalProject_Add(gpu-assetkit-android
    SOURCE_DIR "${GPU_ASSETKIT_ROOT}"
    BINARY_DIR "${GPU_ASSETKIT_ANDROID_BINARY_DIR}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      "-DANDROID_ABI=${ANDROID_ABI}"
      "-DANDROID_PLATFORM=${ANDROID_PLATFORM}"
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
    BUILD_BYPRODUCTS
      "${GPU_ASSETKIT_ANDROID_LIBRARY}"
      "${GPU_ASSETKIT_ANDROID_DS_LIBRARY}"
      "${GPU_ASSETKIT_ANDROID_DEFLATE_LIBRARY}"
    INSTALL_COMMAND ""
  )
endif()

function(gpu_android_gallery_source id source)
  string(REPLACE "-" "_" symbol "${id}")
  set(wrapper "${GPU_ANDROID_GALLERY_GENERATED_DIR}/${id}.c")
  file(WRITE "${wrapper}"
    "#define main gpu_android_start_${symbol}\n"
    "#include \"${source}\"\n"
  )
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${source}"
  )
  set_property(GLOBAL APPEND PROPERTY
    GPU_ANDROID_GALLERY_SOURCES "${wrapper}" ${ARGN}
  )
endfunction()

function(gpu_android_gallery_shader id source)
  set(options USES_STDLIB)
  set(oneValueArgs CAPS FALLBACK_CAPS)
  cmake_parse_arguments(GPU_ANDROID_SHADER
    "${options}" "${oneValueArgs}" "" ${ARGN})

  set(artifactArgs)
  if(GPU_ANDROID_SHADER_USES_STDLIB)
    list(APPEND artifactArgs USES_STDLIB)
  endif()
  if(GPU_ANDROID_SHADER_CAPS)
    list(APPEND artifactArgs CAPS "${GPU_ANDROID_SHADER_CAPS}")
  endif()
  if(GPU_ANDROID_SHADER_FALLBACK_CAPS)
    list(APPEND artifactArgs
      FALLBACK_CAPS "${GPU_ANDROID_SHADER_FALLBACK_CAPS}")
  endif()

  set(target "gpu-android-gallery-${id}")
  gpu_add_android_usl_artifact(
    "${target}"
    "${source}"
    artifactDir
    artifact
    ${artifactArgs}
  )

  get_filename_component(stem "${source}" NAME_WE)
  set(stagedArtifact "${GPU_ANDROID_GALLERY_ASSET_DIR}/${stem}.us")
  set(stagedSpirv
      "${GPU_ANDROID_GALLERY_ASSET_DIR}/targets/${id}.spvasm")
  add_custom_command(
    OUTPUT "${stagedArtifact}" "${stagedSpirv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_ANDROID_GALLERY_ASSET_DIR}/targets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${artifact}"
            "${stagedArtifact}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${artifactDir}/${stem}.usl.spvasm"
            "${stagedSpirv}"
    DEPENDS
      ${target}-artifact
      "${artifact}"
      "${artifactDir}/${stem}.usl.spvasm"
    VERBATIM
  )
  add_custom_target(${target}-stage
    DEPENDS "${stagedArtifact}" "${stagedSpirv}"
  )
  set_property(GLOBAL APPEND PROPERTY
    GPU_ANDROID_GALLERY_ASSET_TARGETS ${target}-stage
  )
endfunction()
