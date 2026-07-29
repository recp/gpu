include(ExternalProject)

find_package(CURL REQUIRED)
find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPU_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

set(GPU_LINUX_GALLERY_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/linux/generated")
set(GPU_LINUX_GALLERY_OUTPUT_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/linux")
file(MAKE_DIRECTORY "${GPU_LINUX_GALLERY_GENERATED_DIR}")

if(GPU_VULKAN_HAS_XLIB)
  find_package(X11 REQUIRED)
endif()

if(GPU_VULKAN_HAS_WAYLAND)
  pkg_check_modules(GPU_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
  pkg_check_modules(GPU_LIBDECOR REQUIRED IMPORTED_TARGET libdecor-0)
endif()

if(EXISTS "${GPU_ASSETKIT_ROOT}/CMakeLists.txt")
  set(GPU_ASSETKIT_LINUX_BINARY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/assetkit-linux")
  set(GPU_ASSETKIT_LINUX_LIBRARY
      "${GPU_ASSETKIT_LINUX_BINARY_DIR}/libassetkit.a")
  set(GPU_ASSETKIT_LINUX_DS_LIBRARY
      "${GPU_ASSETKIT_LINUX_BINARY_DIR}/deps/ds/libds.a")
  set(GPU_ASSETKIT_LINUX_DEFLATE_LIBRARY
      "${GPU_ASSETKIT_LINUX_BINARY_DIR}/libdeflate.a")
  ExternalProject_Add(gpu-assetkit-linux
    SOURCE_DIR "${GPU_ASSETKIT_ROOT}"
    BINARY_DIR "${GPU_ASSETKIT_LINUX_BINARY_DIR}"
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
    BUILD_BYPRODUCTS
      "${GPU_ASSETKIT_LINUX_LIBRARY}"
      "${GPU_ASSETKIT_LINUX_DS_LIBRARY}"
      "${GPU_ASSETKIT_LINUX_DEFLATE_LIBRARY}"
    INSTALL_COMMAND ""
  )
endif()

function(gpu_linux_gallery_artifact target
         sampleId
         shaderSource
         usesStdlib
         shaderCaps
         shaderFallbackCaps
         outArtifact)
  get_filename_component(shaderStem "${shaderSource}" NAME_WE)
  set(shaderDir
      "${CMAKE_CURRENT_BINARY_DIR}/linux/artifacts/${sampleId}")
  set(fixtureSource "${shaderDir}/${shaderStem}.usl")
  set(artifact "${shaderDir}/${shaderStem}.us")
  set(shaderEnvironment USL_EMIT_BYTECODE=1 USL_NO_BACKEND_SIDECAR=1)
  set(packCommands
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=$<TARGET_FILE:gpu-uslpack>"
            "-DGPU_USL_SOURCE=${fixtureSource}"
            -DGPU_USL_TARGET=spirv
            -DGPU_USL_TARGET_PROFILE=vulkan1.1
            "-DGPU_USL_CAPS=${shaderCaps}"
            -P "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
  )

  if(usesStdlib)
    list(APPEND shaderEnvironment
      "USL_STDLIB_PATH=${GPU_USL_ROOT}/stdlib")
  endif()
  if(shaderCaps)
    list(APPEND shaderEnvironment "USL_TARGET_CAPS=${shaderCaps}")
  endif()
  if(shaderFallbackCaps)
    list(APPEND packCommands
      COMMAND "${CMAKE_COMMAND}"
              "-DGPU_USL_PACKER=$<TARGET_FILE:gpu-uslpack>"
              "-DGPU_USL_SOURCE=${fixtureSource}"
              -DGPU_USL_TARGET=spirv
              -DGPU_USL_TARGET_PROFILE=vulkan1.1
              "-DGPU_USL_CAPS=${shaderFallbackCaps}"
              -P "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
    )
  endif()

  add_custom_command(
    OUTPUT "${artifact}"
    BYPRODUCTS "${fixtureSource}.spvasm"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${shaderDir}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${shaderSource}"
            "${fixtureSource}"
    COMMAND ${CMAKE_COMMAND} -E env
            ${shaderEnvironment}
            $<TARGET_FILE:gpu-usl-fixture>
            vulkan
            "${fixtureSource}"
    ${packCommands}
    DEPENDS
      gpu-usl-fixture
      gpu-uslpack
      "${shaderSource}"
      "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
    VERBATIM
  )
  set(${outArtifact} "${artifact}" PARENT_SCOPE)
endfunction()

function(gpu_linux_gallery_target windowSystem
         sampleDir
         sampleId
         sampleSources
         shaderArtifacts
         sampleAssets
         usesAssetKit)
  string(TOLOWER "${windowSystem}" windowSystemLower)
  set(target
      "gpu-gallery-${sampleId}-${windowSystemLower}-usl")
  set(wrapper
      "${GPU_LINUX_GALLERY_GENERATED_DIR}/${sampleId}-${windowSystemLower}.c")
  file(WRITE "${wrapper}"
    "#define main gpu_linux_sample_start\n"
    "#include \"${sampleDir}/main.c\"\n"
  )

  if(windowSystem STREQUAL "XLIB")
    set(host
        "${PROJECT_SOURCE_DIR}/samples/shell/linux/NativeHostXlib.c")
    set(windowLibraries X11::X11)
    set(windowSources)
  elseif(windowSystem STREQUAL "WAYLAND")
    set(host
        "${PROJECT_SOURCE_DIR}/samples/shell/linux/NativeHostWayland.c")
    set(windowLibraries
        PkgConfig::GPU_WAYLAND
        PkgConfig::GPU_LIBDECOR)
    set(windowSources)
  else()
    message(FATAL_ERROR "Unknown Linux window system: ${windowSystem}")
  endif()

  add_executable(${target}
    "${host}"
    "${PROJECT_SOURCE_DIR}/samples/common/linux.c"
    "${PROJECT_SOURCE_DIR}/samples/common/sample_orbit.c"
    "${wrapper}"
    ${sampleSources}
    ${windowSources}
  )
  target_compile_definitions(${target} PRIVATE
    GPU_SAMPLE_GALLERY_LINUX=1
    "GPU_LINUX_SAMPLE_NAME=\"${sampleId}\""
  )
  target_include_directories(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/common"
    "${GPU_CGLM_INCLUDE_DIR}"
    "${GPU_LINUX_GALLERY_GENERATED_DIR}"
  )
  target_link_libraries(${target} PRIVATE
    gpu
    CURL::libcurl
    JPEG::JPEG
    PNG::PNG
    ${windowLibraries}
  )
  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_LINUX_GALLERY_OUTPUT_DIR}/${windowSystemLower}/samples/${sampleId}"
  )

  if(usesAssetKit)
    if(NOT TARGET gpu-assetkit-linux)
      message(FATAL_ERROR
        "Linux AssetKit sample requires GPU_ASSETKIT_ROOT")
    endif()
    target_compile_definitions(${target} PRIVATE AK_STATIC)
    target_include_directories(${target} PRIVATE
      "${GPU_ASSETKIT_ROOT}/include"
    )
    target_link_libraries(${target} PRIVATE
      "${GPU_ASSETKIT_LINUX_LIBRARY}"
      "${GPU_ASSETKIT_LINUX_DS_LIBRARY}"
      "${GPU_ASSETKIT_LINUX_DEFLATE_LIBRARY}"
      dl
      m
    )
    add_dependencies(${target} gpu-assetkit-linux)
  endif()

  set(stageTarget "${target}-artifacts")
  set(stageStamp
      "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${stageTarget}.stamp")
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
    COMMAND ${CMAKE_COMMAND} -E touch "${stageStamp}")
  add_custom_command(
    OUTPUT "${stageStamp}"
    ${stageCommands}
    DEPENDS ${shaderArtifacts} ${sampleAssets}
    VERBATIM
  )
  add_custom_target(${stageTarget} DEPENDS "${stageStamp}")
  add_dependencies(${target} ${stageTarget})

  set_property(GLOBAL APPEND PROPERTY
    "GPU_LINUX_${windowSystem}_SAMPLE_IDS" "${sampleId}")
  set_property(GLOBAL APPEND PROPERTY
    "GPU_LINUX_${windowSystem}_SAMPLE_TARGETS" "${target}")
endfunction()

function(gpu_linux_gallery_sample sampleDir)
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

    gpu_linux_gallery_artifact(
      "gpu-linux-gallery-${sampleId}-${shaderStem}"
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

  if(GPU_VULKAN_HAS_XLIB)
    gpu_linux_gallery_target(
      XLIB
      "${sampleDir}"
      "${sampleId}"
      "${sampleSources}"
      "${shaderArtifacts}"
      "${sampleAssets}"
      "${GPU_SAMPLE_ASSETKIT}"
    )
  endif()
  if(GPU_VULKAN_HAS_WAYLAND)
    gpu_linux_gallery_target(
      WAYLAND
      "${sampleDir}"
      "${sampleId}"
      "${sampleSources}"
      "${shaderArtifacts}"
      "${sampleAssets}"
      "${GPU_SAMPLE_ASSETKIT}"
    )
  endif()
endfunction()
