if(GPU_BUILD_SAMPLES AND GPU_BUILD_WEBGPU AND EMSCRIPTEN)
  set(GPU_USL_HOST_FIXTURE "" CACHE FILEPATH
      "Host gpu-usl-fixture executable used for WebGPU samples")
  set(GPU_USL_HOST_PACKER "" CACHE FILEPATH
      "Host uslpack executable used for WebGPU samples")
  if(NOT GPU_USL_HOST_FIXTURE)
    set(_gpu_usl_host_fixture
        "${PROJECT_SOURCE_DIR}/out/build/release-check/gpu-usl-fixture")
    if(EXISTS "${_gpu_usl_host_fixture}")
      set(GPU_USL_HOST_FIXTURE "${_gpu_usl_host_fixture}")
    endif()
  endif()
  if(NOT GPU_USL_HOST_PACKER)
    set(_gpu_usl_host_packer
        "${PROJECT_SOURCE_DIR}/out/build/release-check/uslpack")
    if(EXISTS "${_gpu_usl_host_packer}")
      set(GPU_USL_HOST_PACKER "${_gpu_usl_host_packer}")
    endif()
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_FIXTURE}")
    message(FATAL_ERROR
      "WebGPU samples require a host gpu-usl-fixture executable. Set "
      "GPU_USL_HOST_FIXTURE to a native build of that target.")
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_PACKER}")
    message(FATAL_ERROR
      "WebGPU samples require a host uslpack executable. Set "
      "GPU_USL_HOST_PACKER to a native build of that target.")
  endif()
  set(GPU_USL_STDLIB_PATH "${GPU_USL_ROOT}/stdlib" CACHE PATH
      "USL standard library root used for WebGPU samples")
  if(NOT EXISTS "${GPU_USL_STDLIB_PATH}/STDLIB_VERSION")
    message(FATAL_ERROR
      "WebGPU samples require the USL standard library. Set "
      "GPU_USL_STDLIB_PATH to its stdlib directory.")
  endif()
  file(STRINGS "${GPU_USL_STDLIB_PATH}/STDLIB_VERSION"
       GPU_USL_STDLIB_VERSION LIMIT_COUNT 1)
  if(NOT GPU_USL_STDLIB_VERSION)
    message(FATAL_ERROR
      "WebGPU samples require a non-empty USL STDLIB_VERSION.")
  endif()
  file(GLOB_RECURSE GPU_USL_STDLIB_SOURCES CONFIGURE_DEPENDS
       "${GPU_USL_STDLIB_PATH}/*.usl")

  if(EXISTS "${GPU_ASSETKIT_ROOT}/CMakeLists.txt")
    include(ExternalProject)

    set(GPU_ASSETKIT_WEBGPU_BINARY_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/assetkit-webgpu")
    set(GPU_ASSETKIT_WEBGPU_LIBRARY
        "${GPU_ASSETKIT_WEBGPU_BINARY_DIR}/libassetkit.a")
    set(GPU_ASSETKIT_WEBGPU_DS_LIBRARY
        "${GPU_ASSETKIT_WEBGPU_BINARY_DIR}/deps/ds/libds.a")
    set(GPU_ASSETKIT_WEBGPU_DEFLATE_LIBRARY
        "${GPU_ASSETKIT_WEBGPU_BINARY_DIR}/libdeflate.a")
    ExternalProject_Add(gpu-assetkit-webgpu
      SOURCE_DIR "${GPU_ASSETKIT_ROOT}"
      BINARY_DIR "${GPU_ASSETKIT_WEBGPU_BINARY_DIR}"
      CMAKE_GENERATOR "${CMAKE_GENERATOR}"
      CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
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
        "${GPU_ASSETKIT_WEBGPU_LIBRARY}"
        "${GPU_ASSETKIT_WEBGPU_DS_LIBRARY}"
        "${GPU_ASSETKIT_WEBGPU_DEFLATE_LIBRARY}"
      INSTALL_COMMAND ""
    )
  endif()

  set(GPU_WEBGPU_GALLERY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/samples/webgpu")
  set(GPU_WEBGPU_GALLERY_SOURCE_DIR
      "${PROJECT_SOURCE_DIR}/samples/webgpu-gallery")
  set(GPU_WEBGPU_GALLERY_VERSION "44")
  set(GPU_WEBGPU_SAMPLE_WGSL_LABEL "Generated WGSL")
  set(GPU_WEBGPU_SAMPLE_COMMON_SOURCE
      "${PROJECT_SOURCE_DIR}/samples/common/webgpu.c")
  set(GPU_WEBGPU_PACK_SCRIPT
      "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake")
  file(GLOB GPU_WEBGPU_GALLERY_PREVIEWS CONFIGURE_DEPENDS
       "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/previews/*")
  file(MAKE_DIRECTORY
       "${GPU_WEBGPU_GALLERY_DIR}"
       "${GPU_WEBGPU_GALLERY_DIR}/sources"
       "${GPU_WEBGPU_GALLERY_DIR}/previews")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/index.html"
    "${GPU_WEBGPU_GALLERY_DIR}/index.html"
    @ONLY
  )
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/gallery.css"
    "${GPU_WEBGPU_GALLERY_DIR}/gallery.css"
    COPYONLY
  )
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample.js"
    "${GPU_WEBGPU_GALLERY_DIR}/sample.js"
    COPYONLY
  )
  foreach(preview ${GPU_WEBGPU_GALLERY_PREVIEWS})
    get_filename_component(previewName "${preview}" NAME)
    configure_file(
      "${preview}"
      "${GPU_WEBGPU_GALLERY_DIR}/previews/${previewName}"
      COPYONLY
    )
  endforeach()

  function(gpu_add_webgpu_gallery_sample id stem title kind description)
    set(options USES_STDLIB)
    set(oneValueArgs ASSET_SOURCE)
    set(multiValueArgs ASSETS)
    cmake_parse_arguments(GPU_WEBGPU_SAMPLE
      "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(sampleTarget "gpu-${id}-webgpu-usl")
    set(artifactTarget "gpu-webgpu-${id}-artifact")
    set(sampleDir
        "${PROJECT_SOURCE_DIR}/samples/gallery/${id}")
    set(artifactDir
        "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
    set(artifactSource "${artifactDir}/${stem}.usl")
    set(artifact "${artifactDir}/${stem}.us")
    set(shell
        "${GPU_WEBGPU_GALLERY_DIR}/${sampleTarget}-shell.html")
    set(GPU_WEBGPU_SAMPLE_TITLE "${title}")
    set(GPU_WEBGPU_SAMPLE_KIND "${kind}")
    set(GPU_WEBGPU_SAMPLE_DESCRIPTION "${description}")
    set(assetSource "${GPU_WEBGPU_SAMPLE_ASSET_SOURCE}")
    set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/${id}.c")
    set(GPU_WEBGPU_SAMPLE_ASSET_SOURCE "")
    set(GPU_WEBGPU_SAMPLE_ASSET_TAB "")
    set(GPU_WEBGPU_SAMPLE_WGSL_LABEL "Generated WGSL")
    set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/${id}.usl")
    set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/${id}.wgsl")
    set(sampleSources "${sampleDir}/main.c")
    set(assetSourceDependencies)
    if(assetSource)
      set(GPU_WEBGPU_SAMPLE_ASSET_SOURCE
          "sources/${id}-asset.c")
      set(GPU_WEBGPU_SAMPLE_ASSET_TAB
          "<button id=\"tab-asset\" role=\"tab\" aria-selected=\"false\" aria-controls=\"source-code\" data-source=\"asset\">Asset Load via AssetKit</button>")
      set(GPU_WEBGPU_SAMPLE_WGSL_LABEL "WGSL")
      list(APPEND sampleSources
        "${sampleDir}/${assetSource}")
      list(APPEND assetSourceDependencies
        "${sampleDir}/${assetSource}")
    endif()
    set(artifactDependencies)
    if(GPU_WEBGPU_SAMPLE_USES_STDLIB)
      list(APPEND artifactDependencies
        "${GPU_USL_STDLIB_PATH}/STDLIB_VERSION"
        ${GPU_USL_STDLIB_SOURCES}
      )
    endif()

    configure_file(
      "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
      "${shell}"
      @ONLY
    )
    add_custom_command(
      OUTPUT "${artifact}"
      BYPRODUCTS "${artifactSource}.wgsl"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${artifactDir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${sampleDir}/${stem}.usl"
              "${artifactSource}"
      COMMAND ${CMAKE_COMMAND} -E env
              USL_EMIT_BYTECODE=1
              "USL_STDLIB_PATH=${GPU_USL_STDLIB_PATH}"
              "USL_STDLIB_VERSION=${GPU_USL_STDLIB_VERSION}"
              "${GPU_USL_HOST_FIXTURE}"
              webgpu
              "${artifactSource}"
      COMMAND "${CMAKE_COMMAND}"
              "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
              "-DGPU_USL_SOURCE=${artifactSource}"
              -P "${GPU_WEBGPU_PACK_SCRIPT}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${sampleDir}/main.c"
              "${GPU_WEBGPU_GALLERY_DIR}/sources/${id}.c"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${artifactSource}"
              "${GPU_WEBGPU_GALLERY_DIR}/sources/${id}.usl"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${artifactSource}.wgsl"
              "${GPU_WEBGPU_GALLERY_DIR}/sources/${id}.wgsl"
      DEPENDS
        "${GPU_USL_HOST_FIXTURE}"
        "${GPU_USL_HOST_PACKER}"
        "${GPU_WEBGPU_PACK_SCRIPT}"
        "${sampleDir}/${stem}.usl"
        "${sampleDir}/main.c"
        ${assetSourceDependencies}
        ${artifactDependencies}
      VERBATIM
    )
    if(assetSource)
      add_custom_command(
        OUTPUT "${artifact}"
        APPEND
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${sampleDir}/${assetSource}"
                "${GPU_WEBGPU_GALLERY_DIR}/${GPU_WEBGPU_SAMPLE_ASSET_SOURCE}"
      )
    endif()
    add_custom_target(${artifactTarget} DEPENDS "${artifact}")

    set(assetOptions)
    foreach(asset IN LISTS GPU_WEBGPU_SAMPLE_ASSETS)
      list(APPEND assetOptions
        "--preload-file=${sampleDir}/${asset}@/${asset}")
    endforeach()

    add_executable(${sampleTarget}
      ${sampleSources}
      ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
    )
    target_link_libraries(${sampleTarget} PRIVATE gpu)
    target_link_options(${sampleTarget} PRIVATE
      --use-port=emdawnwebgpu
      "--preload-file=${artifact}@/${stem}.us"
      "--shell-file=${shell}"
      -sALLOW_MEMORY_GROWTH=1
      ${assetOptions}
    )
    add_dependencies(${sampleTarget} ${artifactTarget})
    set_target_properties(${sampleTarget} PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
      LINK_DEPENDS "${shell};${artifact}"
      SUFFIX ".html"
      RUNTIME_OUTPUT_DIRECTORY "${GPU_WEBGPU_GALLERY_DIR}"
    )
  endfunction()

  set(GPU_WEBGPU_TRIANGLE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TRIANGLE_SOURCE
      "${GPU_WEBGPU_TRIANGLE_DIR}/triangle.usl")
  set(GPU_WEBGPU_TRIANGLE_US
      "${GPU_WEBGPU_TRIANGLE_DIR}/triangle.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "First pixel")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / vertex-generated")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "A bufferless triangle compiled from USL and drawn through the portable render path.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/triangle.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/triangle.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/triangle.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-triangle-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TRIANGLE_US}"
    BYPRODUCTS "${GPU_WEBGPU_TRIANGLE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_TRIANGLE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/triangle/triangle.usl"
            "${GPU_WEBGPU_TRIANGLE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TRIANGLE_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_TRIANGLE_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/triangle/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/triangle.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TRIANGLE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/triangle.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TRIANGLE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/triangle.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/triangle/triangle.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/triangle/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-triangle-artifact
    DEPENDS "${GPU_WEBGPU_TRIANGLE_US}"
  )

  add_executable(gpu-triangle-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/triangle/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-triangle-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-triangle-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TRIANGLE_US}@/triangle.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-triangle-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-triangle-webgpu-usl gpu-webgpu-triangle-artifact)
  set_target_properties(gpu-triangle-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-triangle-webgpu-usl-shell.html;${GPU_WEBGPU_TRIANGLE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  if(GPU_BUILD_BENCHMARKS)
    set(GPU_WEBGPU_BENCHMARK_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/benchmarks/webgpu")
    file(MAKE_DIRECTORY "${GPU_WEBGPU_BENCHMARK_DIR}")
    add_executable(gpu-webgpu-render-bench
      benchmarks/webgpu_render.c
      ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
    )
    target_link_libraries(gpu-webgpu-render-bench PRIVATE gpu)
    target_link_options(gpu-webgpu-render-bench PRIVATE
      --use-port=emdawnwebgpu
      "--preload-file=${GPU_WEBGPU_TRIANGLE_US}@/triangle.us"
      "--shell-file=${PROJECT_SOURCE_DIR}/benchmarks/webgpu_render_shell.html"
      -sALLOW_MEMORY_GROWTH=1
    )
    add_dependencies(gpu-webgpu-render-bench
                     gpu-webgpu-triangle-artifact)
    set_target_properties(gpu-webgpu-render-bench PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
      LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/benchmarks/webgpu_render_shell.html;${GPU_WEBGPU_TRIANGLE_US}"
      SUFFIX ".html"
      RUNTIME_OUTPUT_DIRECTORY "${GPU_WEBGPU_BENCHMARK_DIR}"
    )
  endif()

  set(GPU_WEBGPU_PUSH_CONSTANTS_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_PUSH_CONSTANTS_SOURCE
      "${GPU_WEBGPU_PUSH_CONSTANTS_DIR}/push_constants.usl")
  set(GPU_WEBGPU_PUSH_CONSTANTS_US
      "${GPU_WEBGPU_PUSH_CONSTANTS_DIR}/push_constants.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Push constants")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / dynamic constants")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Per-draw constants lowered to a native fast path or a WebGPU dynamic uniform slice.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/push-constants.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/push-constants.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/push-constants.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-push-constants-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_PUSH_CONSTANTS_US}"
    BYPRODUCTS "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_PUSH_CONSTANTS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/push-constants/push_constants.usl"
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/push-constants/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/push-constants.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/push-constants.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_PUSH_CONSTANTS_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/push-constants.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/push-constants/push_constants.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/push-constants/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-push-constants-artifact
    DEPENDS "${GPU_WEBGPU_PUSH_CONSTANTS_US}"
  )

  add_executable(gpu-push-constants-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/push-constants/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-push-constants-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-push-constants-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_PUSH_CONSTANTS_US}@/push_constants.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-push-constants-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-push-constants-webgpu-usl
                   gpu-webgpu-push-constants-artifact)
  set_target_properties(gpu-push-constants-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-push-constants-webgpu-usl-shell.html;${GPU_WEBGPU_PUSH_CONSTANTS_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TEXTURED_QUAD_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TEXTURED_QUAD_SOURCE
      "${GPU_WEBGPU_TEXTURED_QUAD_DIR}/textured_quad.usl")
  set(GPU_WEBGPU_TEXTURED_QUAD_US
      "${GPU_WEBGPU_TEXTURED_QUAD_DIR}/textured_quad.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Transfer chain")
  set(GPU_WEBGPU_SAMPLE_KIND "Transfer + binding / buffer + texture")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "All four copy directions execute; the texture-to-texture result is then bound and sampled.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/textured-quad.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/textured-quad.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/textured-quad.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-quad-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TEXTURED_QUAD_US}"
    BYPRODUCTS "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TEXTURED_QUAD_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/textured-quad/textured_quad.usl"
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/textured-quad/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-quad.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-quad.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_QUAD_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-quad.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/textured-quad/textured_quad.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/textured-quad/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-textured-quad-artifact
    DEPENDS "${GPU_WEBGPU_TEXTURED_QUAD_US}"
  )

  add_executable(gpu-textured-quad-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/textured-quad/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-textured-quad-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-textured-quad-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TEXTURED_QUAD_US}@/textured_quad.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-quad-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-textured-quad-webgpu-usl
                   gpu-webgpu-textured-quad-artifact)
  set_target_properties(gpu-textured-quad-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-quad-webgpu-usl-shell.html;${GPU_WEBGPU_TEXTURED_QUAD_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_COMPUTE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_COMPUTE_SOURCE
      "${GPU_WEBGPU_COMPUTE_DIR}/compute.usl")
  set(GPU_WEBGPU_COMPUTE_US
      "${GPU_WEBGPU_COMPUTE_DIR}/compute.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Compute handoff")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute / storage buffer")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Compute uses per-dispatch constants to write the vertex stream; render consumes it without readback.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/compute.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/compute.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/compute.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-compute-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_COMPUTE_US}"
    BYPRODUCTS "${GPU_WEBGPU_COMPUTE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_COMPUTE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/compute/compute.usl"
            "${GPU_WEBGPU_COMPUTE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_COMPUTE_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_COMPUTE_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/compute/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/compute.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_COMPUTE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/compute.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_COMPUTE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/compute.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/compute/compute.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/compute/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-compute-artifact
    DEPENDS "${GPU_WEBGPU_COMPUTE_US}"
  )

  add_executable(gpu-compute-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/compute/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-compute-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-compute-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_COMPUTE_US}@/compute.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-compute-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-compute-webgpu-usl gpu-webgpu-compute-artifact)
  set_target_properties(gpu-compute-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-compute-webgpu-usl-shell.html;${GPU_WEBGPU_COMPUTE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TIMESTAMP_QUERY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TIMESTAMP_QUERY_SOURCE
      "${GPU_WEBGPU_TIMESTAMP_QUERY_DIR}/timestamp_query.usl")
  set(GPU_WEBGPU_TIMESTAMP_QUERY_US
      "${GPU_WEBGPU_TIMESTAMP_QUERY_DIR}/timestamp_query.us")
  set(GPU_WEBGPU_TIMESTAMP_QUERY_MAIN
      "${PROJECT_SOURCE_DIR}/samples/gallery/timestamp-query/main.c")
  set(GPU_WEBGPU_SAMPLE_TITLE "Pass timestamps")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute + render / timestamps")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Compute and render passes record boundary timestamps, then verify an 8-byte-aligned resolve through WebGPU.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/timestamp-query.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/timestamp-query.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/timestamp-query.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-timestamp-query-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TIMESTAMP_QUERY_US}"
    BYPRODUCTS "${GPU_WEBGPU_TIMESTAMP_QUERY_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TIMESTAMP_QUERY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/timestamp-query/timestamp_query.usl"
            "${GPU_WEBGPU_TIMESTAMP_QUERY_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TIMESTAMP_QUERY_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_TIMESTAMP_QUERY_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TIMESTAMP_QUERY_MAIN}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/timestamp-query.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TIMESTAMP_QUERY_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/timestamp-query.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TIMESTAMP_QUERY_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/timestamp-query.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/timestamp-query/timestamp_query.usl"
      "${GPU_WEBGPU_TIMESTAMP_QUERY_MAIN}"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-timestamp-query-artifact
    DEPENDS "${GPU_WEBGPU_TIMESTAMP_QUERY_US}"
  )

  add_executable(gpu-timestamp-query-webgpu-usl
    "${GPU_WEBGPU_TIMESTAMP_QUERY_MAIN}"
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_include_directories(gpu-timestamp-query-webgpu-usl PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/gallery/compute"
  )
  target_link_libraries(gpu-timestamp-query-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-timestamp-query-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TIMESTAMP_QUERY_US}@/timestamp_query.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-timestamp-query-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-timestamp-query-webgpu-usl
                   gpu-webgpu-timestamp-query-artifact)
  set_target_properties(gpu-timestamp-query-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-timestamp-query-webgpu-usl-shell.html;${GPU_WEBGPU_TIMESTAMP_QUERY_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_SUBGROUP_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_SUBGROUP_SOURCE
      "${GPU_WEBGPU_SUBGROUP_DIR}/subgroup.usl")
  set(GPU_WEBGPU_SUBGROUP_US
      "${GPU_WEBGPU_SUBGROUP_DIR}/subgroup.us")
  set(GPU_WEBGPU_SUBGROUP_FALLBACK_SOURCE
      "${GPU_WEBGPU_SUBGROUP_DIR}/subgroup_fallback.usl")
  set(GPU_WEBGPU_SUBGROUP_FALLBACK_US
      "${GPU_WEBGPU_SUBGROUP_DIR}/subgroup_fallback.us")
  set(GPU_WEBGPU_SUBGROUP_MAIN
      "${PROJECT_SOURCE_DIR}/samples/gallery/subgroup/main.c")
  set(GPU_WEBGPU_SAMPLE_TITLE "Subgroup exchange")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute / subgroup shuffle")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "A native subgroup shuffle changes the compute-generated vertex colors before render consumes the buffer.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/subgroup.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/subgroup.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/subgroup.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-subgroup-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT
      "${GPU_WEBGPU_SUBGROUP_US}"
      "${GPU_WEBGPU_SUBGROUP_FALLBACK_US}"
    BYPRODUCTS
      "${GPU_WEBGPU_SUBGROUP_SOURCE}.wgsl"
      "${GPU_WEBGPU_SUBGROUP_FALLBACK_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_SUBGROUP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/subgroup/subgroup.usl"
            "${GPU_WEBGPU_SUBGROUP_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/subgroup/subgroup_fallback.usl"
            "${GPU_WEBGPU_SUBGROUP_FALLBACK_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_TARGET_CAPS=subgroup
            "${GPU_USL_HOST_FIXTURE}"
              webgpu
              "${GPU_WEBGPU_SUBGROUP_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_SUBGROUP_SOURCE}"
            "-DGPU_USL_CAPS=subgroup"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E env
              USL_EMIT_BYTECODE=1
              "${GPU_USL_HOST_FIXTURE}"
              webgpu
              "${GPU_WEBGPU_SUBGROUP_FALLBACK_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_SUBGROUP_FALLBACK_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SUBGROUP_MAIN}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/subgroup.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SUBGROUP_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/subgroup.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SUBGROUP_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/subgroup.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/subgroup/subgroup.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/subgroup/subgroup_fallback.usl"
      "${GPU_WEBGPU_SUBGROUP_MAIN}"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-subgroup-artifact
    DEPENDS
      "${GPU_WEBGPU_SUBGROUP_US}"
      "${GPU_WEBGPU_SUBGROUP_FALLBACK_US}"
  )

  add_executable(gpu-subgroup-webgpu-usl
    "${GPU_WEBGPU_SUBGROUP_MAIN}"
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_include_directories(gpu-subgroup-webgpu-usl PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/gallery/compute"
  )
  target_link_libraries(gpu-subgroup-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-subgroup-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_SUBGROUP_US}@/subgroup.us"
    "--preload-file=${GPU_WEBGPU_SUBGROUP_FALLBACK_US}@/subgroup_fallback.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-subgroup-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-subgroup-webgpu-usl gpu-webgpu-subgroup-artifact)
  set_target_properties(gpu-subgroup-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-subgroup-webgpu-usl-shell.html;${GPU_WEBGPU_SUBGROUP_US};${GPU_WEBGPU_SUBGROUP_FALLBACK_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_SHADER_F16_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_SHADER_F16_SOURCE
      "${GPU_WEBGPU_SHADER_F16_DIR}/shader_f16.usl")
  set(GPU_WEBGPU_SHADER_F16_US
      "${GPU_WEBGPU_SHADER_F16_DIR}/shader_f16.us")
  set(GPU_WEBGPU_SHADER_F16_FALLBACK_SOURCE
      "${GPU_WEBGPU_SHADER_F16_DIR}/shader_f16_fallback.usl")
  set(GPU_WEBGPU_SHADER_F16_FALLBACK_US
      "${GPU_WEBGPU_SHADER_F16_DIR}/shader_f16_fallback.us")
  set(GPU_WEBGPU_SHADER_F16_MAIN
      "${PROJECT_SOURCE_DIR}/samples/gallery/shader-f16/main.c")
  set(GPU_WEBGPU_SAMPLE_TITLE "Half precision")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute / shader f16")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Native half-precision arithmetic colors a compute-generated vertex stream, with a bounded f32 fallback.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/shader-f16.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/shader-f16.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/shader-f16.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-shader-f16-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT
      "${GPU_WEBGPU_SHADER_F16_US}"
      "${GPU_WEBGPU_SHADER_F16_FALLBACK_US}"
    BYPRODUCTS
      "${GPU_WEBGPU_SHADER_F16_SOURCE}.wgsl"
      "${GPU_WEBGPU_SHADER_F16_FALLBACK_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_SHADER_F16_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/shader-f16/shader_f16.usl"
            "${GPU_WEBGPU_SHADER_F16_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/shader-f16/shader_f16_fallback.usl"
            "${GPU_WEBGPU_SHADER_F16_FALLBACK_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            USL_TARGET_CAPS=shader_f16
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_SHADER_F16_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_SHADER_F16_SOURCE}"
            "-DGPU_USL_CAPS=shader_f16"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_SHADER_F16_FALLBACK_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_SHADER_F16_FALLBACK_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SHADER_F16_MAIN}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/shader-f16.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SHADER_F16_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/shader-f16.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SHADER_F16_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/shader-f16.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/shader-f16/shader_f16.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/shader-f16/shader_f16_fallback.usl"
      "${GPU_WEBGPU_SHADER_F16_MAIN}"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-shader-f16-artifact
    DEPENDS
      "${GPU_WEBGPU_SHADER_F16_US}"
      "${GPU_WEBGPU_SHADER_F16_FALLBACK_US}"
  )

  add_executable(gpu-shader-f16-webgpu-usl
    "${GPU_WEBGPU_SHADER_F16_MAIN}"
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_include_directories(gpu-shader-f16-webgpu-usl PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/gallery/compute"
  )
  target_link_libraries(gpu-shader-f16-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-shader-f16-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_SHADER_F16_US}@/shader_f16.us"
    "--preload-file=${GPU_WEBGPU_SHADER_F16_FALLBACK_US}@/shader_f16_fallback.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-shader-f16-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-shader-f16-webgpu-usl
                   gpu-webgpu-shader-f16-artifact)
  set_target_properties(gpu-shader-f16-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-shader-f16-webgpu-usl-shell.html;${GPU_WEBGPU_SHADER_F16_US};${GPU_WEBGPU_SHADER_F16_FALLBACK_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_DISPATCH_INDIRECT_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_DISPATCH_INDIRECT_SOURCE
      "${GPU_WEBGPU_DISPATCH_INDIRECT_DIR}/dispatch_indirect.usl")
  set(GPU_WEBGPU_DISPATCH_INDIRECT_US
      "${GPU_WEBGPU_DISPATCH_INDIRECT_DIR}/dispatch_indirect.us")
  set(GPU_WEBGPU_DISPATCH_INDIRECT_MAIN
      "${PROJECT_SOURCE_DIR}/samples/gallery/dispatch-indirect/main.c")
  set(GPU_WEBGPU_SAMPLE_TITLE "Indirect dispatch")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute / indirect command")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "One packed dispatch record launches compute; render consumes the generated vertex stream.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/dispatch-indirect.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/dispatch-indirect.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/dispatch-indirect.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-dispatch-indirect-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_DISPATCH_INDIRECT_US}"
    BYPRODUCTS "${GPU_WEBGPU_DISPATCH_INDIRECT_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_DISPATCH_INDIRECT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/dispatch-indirect/dispatch_indirect.usl"
            "${GPU_WEBGPU_DISPATCH_INDIRECT_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_DISPATCH_INDIRECT_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_DISPATCH_INDIRECT_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_DISPATCH_INDIRECT_MAIN}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/dispatch-indirect.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_DISPATCH_INDIRECT_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/dispatch-indirect.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_DISPATCH_INDIRECT_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/dispatch-indirect.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/dispatch-indirect/dispatch_indirect.usl"
      "${GPU_WEBGPU_DISPATCH_INDIRECT_MAIN}"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-dispatch-indirect-artifact
    DEPENDS "${GPU_WEBGPU_DISPATCH_INDIRECT_US}"
  )

  add_executable(gpu-dispatch-indirect-webgpu-usl
    "${GPU_WEBGPU_DISPATCH_INDIRECT_MAIN}"
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_include_directories(gpu-dispatch-indirect-webgpu-usl PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/gallery/compute"
  )
  target_link_libraries(gpu-dispatch-indirect-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-dispatch-indirect-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_DISPATCH_INDIRECT_US}@/dispatch_indirect.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-dispatch-indirect-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-dispatch-indirect-webgpu-usl
                   gpu-webgpu-dispatch-indirect-artifact)
  set_target_properties(gpu-dispatch-indirect-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-dispatch-indirect-webgpu-usl-shell.html;${GPU_WEBGPU_DISPATCH_INDIRECT_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_MULTI_DRAW_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_MULTI_DRAW_SOURCE
      "${GPU_WEBGPU_MULTI_DRAW_DIR}/multi_draw.usl")
  set(GPU_WEBGPU_MULTI_DRAW_US
      "${GPU_WEBGPU_MULTI_DRAW_DIR}/multi_draw.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Multi-draw batch")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / indirect batch")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "One multi-draw command consumes two packed indirect records, using the native extension when available.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/multi-draw.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/multi-draw.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/multi-draw.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-multi-draw-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_MULTI_DRAW_US}"
    BYPRODUCTS "${GPU_WEBGPU_MULTI_DRAW_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_MULTI_DRAW_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/multi-draw/multi_draw.usl"
            "${GPU_WEBGPU_MULTI_DRAW_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_MULTI_DRAW_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_MULTI_DRAW_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/multi-draw/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/multi-draw.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MULTI_DRAW_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/multi-draw.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MULTI_DRAW_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/multi-draw.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/multi-draw/main.c"
      "${PROJECT_SOURCE_DIR}/samples/gallery/multi-draw/multi_draw.usl"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-multi-draw-artifact
    DEPENDS "${GPU_WEBGPU_MULTI_DRAW_US}"
  )

  add_executable(gpu-multi-draw-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/multi-draw/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-multi-draw-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-multi-draw-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_MULTI_DRAW_US}@/multi_draw.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-multi-draw-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-multi-draw-webgpu-usl
                   gpu-webgpu-multi-draw-artifact)
  set_target_properties(gpu-multi-draw-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-multi-draw-webgpu-usl-shell.html;${GPU_WEBGPU_MULTI_DRAW_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_STORAGE_TEXTURE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_STORAGE_TEXTURE_SOURCE
      "${GPU_WEBGPU_STORAGE_TEXTURE_DIR}/storage_texture.usl")
  set(GPU_WEBGPU_STORAGE_TEXTURE_US
      "${GPU_WEBGPU_STORAGE_TEXTURE_DIR}/storage_texture.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Storage texture")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute / typed storage texture")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Compute writes two typed RGBA8 textures; render samples their reflected fixed arrays in the same command buffer.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/storage-texture.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/storage-texture.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/storage-texture.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-storage-texture-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_STORAGE_TEXTURE_US}"
    BYPRODUCTS "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_STORAGE_TEXTURE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/storage-texture/storage_texture.usl"
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}"
            "-DGPU_USL_CAPS=storage_texture_extended_access"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/storage-texture/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/storage-texture.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/storage-texture.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_STORAGE_TEXTURE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/storage-texture.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/storage-texture/storage_texture.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/storage-texture/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-storage-texture-artifact
    DEPENDS "${GPU_WEBGPU_STORAGE_TEXTURE_US}"
  )

  add_executable(gpu-storage-texture-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/storage-texture/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-storage-texture-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-storage-texture-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_STORAGE_TEXTURE_US}@/storage_texture.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-storage-texture-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-storage-texture-webgpu-usl
                   gpu-webgpu-storage-texture-artifact)
  set_target_properties(gpu-storage-texture-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-storage-texture-webgpu-usl-shell.html;${GPU_WEBGPU_STORAGE_TEXTURE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_INDEXED_DEPTH_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_INDEXED_DEPTH_SOURCE
      "${GPU_WEBGPU_INDEXED_DEPTH_DIR}/indexed_depth.usl")
  set(GPU_WEBGPU_INDEXED_DEPTH_US
      "${GPU_WEBGPU_INDEXED_DEPTH_DIR}/indexed_depth.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Indexed depth")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / index + depth")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Indexed depth, occlusion resolve, barriers, and a query-tinted depth preview.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/indexed-depth.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/indexed-depth.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/indexed-depth.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-indexed-depth-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_INDEXED_DEPTH_US}"
    BYPRODUCTS "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_INDEXED_DEPTH_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/indexed-depth/indexed_depth.usl"
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/indexed-depth/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/indexed-depth.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/indexed-depth.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INDEXED_DEPTH_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/indexed-depth.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/indexed-depth/indexed_depth.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/indexed-depth/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-indexed-depth-artifact
    DEPENDS "${GPU_WEBGPU_INDEXED_DEPTH_US}"
  )

  add_executable(gpu-indexed-depth-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/indexed-depth/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-indexed-depth-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-indexed-depth-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_INDEXED_DEPTH_US}@/indexed_depth.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-indexed-depth-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-indexed-depth-webgpu-usl
                   gpu-webgpu-indexed-depth-artifact)
  set_target_properties(gpu-indexed-depth-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-indexed-depth-webgpu-usl-shell.html;${GPU_WEBGPU_INDEXED_DEPTH_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_SHADOW_COMPARE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_SHADOW_COMPARE_SOURCE
      "${GPU_WEBGPU_SHADOW_COMPARE_DIR}/shadow_compare.usl")
  set(GPU_WEBGPU_SHADOW_COMPARE_US
      "${GPU_WEBGPU_SHADOW_COMPARE_DIR}/shadow_compare.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Comparison shadow")
  set(GPU_WEBGPU_SAMPLE_KIND "Binding / depth comparison")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "A reflected depth texture and comparison sampler produce a filtered shadow in two render passes.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/shadow-compare.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/shadow-compare.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/shadow-compare.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-shadow-compare-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_SHADOW_COMPARE_US}"
    BYPRODUCTS "${GPU_WEBGPU_SHADOW_COMPARE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_SHADOW_COMPARE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
            "${GPU_WEBGPU_SHADOW_COMPARE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_SHADOW_COMPARE_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_SHADOW_COMPARE_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/shadow-compare.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SHADOW_COMPARE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/shadow-compare.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_SHADOW_COMPARE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/shadow-compare.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/shadow_compare.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-shadow-compare-artifact
    DEPENDS "${GPU_WEBGPU_SHADOW_COMPARE_US}"
  )

  add_executable(gpu-shadow-compare-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/shadow-compare/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-shadow-compare-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-shadow-compare-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_SHADOW_COMPARE_US}@/shadow_compare.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-shadow-compare-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-shadow-compare-webgpu-usl
                   gpu-webgpu-shadow-compare-artifact)
  set_target_properties(gpu-shadow-compare-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-shadow-compare-webgpu-usl-shell.html;${GPU_WEBGPU_SHADOW_COMPARE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_INSTANCING_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_INSTANCING_SOURCE
      "${GPU_WEBGPU_INSTANCING_DIR}/instancing.usl")
  set(GPU_WEBGPU_INSTANCING_US
      "${GPU_WEBGPU_INSTANCING_DIR}/instancing.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Instancing")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / instances + offsets")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Two dynamic uniform slices drive eight instances without rebuilding the bind group.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/instancing.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/instancing.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/instancing.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-instancing-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_INSTANCING_US}"
    BYPRODUCTS "${GPU_WEBGPU_INSTANCING_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_INSTANCING_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/instancing/instancing.usl"
            "${GPU_WEBGPU_INSTANCING_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_INSTANCING_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_INSTANCING_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/instancing/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/instancing.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INSTANCING_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/instancing.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_INSTANCING_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/instancing.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/instancing/instancing.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/instancing/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-instancing-artifact
    DEPENDS "${GPU_WEBGPU_INSTANCING_US}"
  )

  add_executable(gpu-instancing-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/instancing/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-instancing-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-instancing-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_INSTANCING_US}@/instancing.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-instancing-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-instancing-webgpu-usl gpu-webgpu-instancing-artifact)
  set_target_properties(gpu-instancing-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-instancing-webgpu-usl-shell.html;${GPU_WEBGPU_INSTANCING_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TEXTURED_CUBE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TEXTURED_CUBE_SOURCE
      "${GPU_WEBGPU_TEXTURED_CUBE_DIR}/textured_cube.usl")
  set(GPU_WEBGPU_TEXTURED_CUBE_US
      "${GPU_WEBGPU_TEXTURED_CUBE_DIR}/textured_cube.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Rotating cube")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / cglm + texture")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "<a class=\"project-link\" href=\"https://github.com/recp/cglm\" target=\"_blank\" rel=\"noreferrer\">cglm</a> drives a rotating model-view-projection matrix through an indexed, depth-tested textured draw.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/textured-cube.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/textured-cube.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/textured-cube.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-cube-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TEXTURED_CUBE_US}"
    BYPRODUCTS "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TEXTURED_CUBE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-cube.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-cube.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURED_CUBE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/textured-cube.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/textured_cube.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-textured-cube-artifact
    DEPENDS "${GPU_WEBGPU_TEXTURED_CUBE_US}"
  )

  add_executable(gpu-textured-cube-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/textured-cube/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_include_directories(gpu-textured-cube-webgpu-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  target_link_libraries(gpu-textured-cube-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-textured-cube-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TEXTURED_CUBE_US}@/textured_cube.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-cube-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-textured-cube-webgpu-usl
                   gpu-webgpu-textured-cube-artifact)
  set_target_properties(gpu-textured-cube-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-textured-cube-webgpu-usl-shell.html;${GPU_WEBGPU_TEXTURED_CUBE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_MSAA_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_MSAA_SOURCE
      "${GPU_WEBGPU_MSAA_DIR}/msaa.usl")
  set(GPU_WEBGPU_MSAA_US
      "${GPU_WEBGPU_MSAA_DIR}/msaa.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "MSAA resolve")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / 4x MSAA")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Four-sample rasterization resolves directly into the swapchain and discards the multisample attachment contents.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/msaa.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/msaa.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/msaa.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_MSAA_US}"
    BYPRODUCTS "${GPU_WEBGPU_MSAA_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_MSAA_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/msaa/msaa.usl"
            "${GPU_WEBGPU_MSAA_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_MSAA_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_MSAA_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/msaa/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MSAA_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MSAA_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/msaa/msaa.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/msaa/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-msaa-artifact
    DEPENDS "${GPU_WEBGPU_MSAA_US}"
  )

  add_executable(gpu-msaa-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/msaa/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-msaa-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-msaa-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_MSAA_US}@/msaa.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-msaa-webgpu-usl gpu-webgpu-msaa-artifact)
  set_target_properties(gpu-msaa-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-webgpu-usl-shell.html;${GPU_WEBGPU_MSAA_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_MRT_BLEND_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_MRT_BLEND_SOURCE
      "${GPU_WEBGPU_MRT_BLEND_DIR}/mrt_blend.usl")
  set(GPU_WEBGPU_MRT_BLEND_US
      "${GPU_WEBGPU_MRT_BLEND_DIR}/mrt_blend.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "MRT blend")
  set(GPU_WEBGPU_SAMPLE_KIND "Render / MRT + blending")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Two offscreen color targets use independent alpha and additive blend state, then composite into the swapchain.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/mrt-blend.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/mrt-blend.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/mrt-blend.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-mrt-blend-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_MRT_BLEND_US}"
    BYPRODUCTS "${GPU_WEBGPU_MRT_BLEND_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GPU_WEBGPU_MRT_BLEND_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_MRT_BLEND_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/mrt-blend.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/mrt-blend.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MRT_BLEND_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/mrt-blend.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/mrt_blend.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-mrt-blend-artifact
    DEPENDS "${GPU_WEBGPU_MRT_BLEND_US}"
  )

  add_executable(gpu-mrt-blend-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/mrt-blend/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-mrt-blend-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-mrt-blend-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_MRT_BLEND_US}@/mrt_blend.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-mrt-blend-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-mrt-blend-webgpu-usl
                   gpu-webgpu-mrt-blend-artifact)
  set_target_properties(gpu-mrt-blend-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-mrt-blend-webgpu-usl-shell.html;${GPU_WEBGPU_MRT_BLEND_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TEXTURE_ARRAY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TEXTURE_ARRAY_SOURCE
      "${GPU_WEBGPU_TEXTURE_ARRAY_DIR}/texture_array.usl")
  set(GPU_WEBGPU_TEXTURE_ARRAY_US
      "${GPU_WEBGPU_TEXTURE_ARRAY_DIR}/texture_array.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Texture array")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute + binding / 1D + 2D arrays")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Compute reads, queries, and writes logical 1D-array layers before render samples them beside a 2D array.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/texture-array.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/texture-array.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/texture-array.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-array-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TEXTURE_ARRAY_US}"
    BYPRODUCTS "${GPU_WEBGPU_TEXTURE_ARRAY_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TEXTURE_ARRAY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/texture-array/texture_array.usl"
            "${GPU_WEBGPU_TEXTURE_ARRAY_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TEXTURE_ARRAY_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_TEXTURE_ARRAY_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/texture-array/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-array.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURE_ARRAY_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-array.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURE_ARRAY_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-array.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/texture-array/texture_array.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/texture-array/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-texture-array-artifact
    DEPENDS "${GPU_WEBGPU_TEXTURE_ARRAY_US}"
  )

  add_executable(gpu-texture-array-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/texture-array/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-texture-array-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-texture-array-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TEXTURE_ARRAY_US}@/texture_array.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-array-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-texture-array-webgpu-usl
                   gpu-webgpu-texture-array-artifact)
  set_target_properties(gpu-texture-array-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-array-webgpu-usl-shell.html;${GPU_WEBGPU_TEXTURE_ARRAY_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TEXTURE_LINE_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TEXTURE_LINE_SOURCE
      "${GPU_WEBGPU_TEXTURE_LINE_DIR}/texture_line.usl")
  set(GPU_WEBGPU_TEXTURE_LINE_US
      "${GPU_WEBGPU_TEXTURE_LINE_DIR}/texture_line.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Texture line")
  set(GPU_WEBGPU_SAMPLE_KIND "Compute + binding / 1D texture")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Compute transforms a logical 1D texture before render reads the result beside the uploaded source line.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/texture-line.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/texture-line.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/texture-line.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-line-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TEXTURE_LINE_US}"
    BYPRODUCTS "${GPU_WEBGPU_TEXTURE_LINE_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TEXTURE_LINE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/texture-line/texture_line.usl"
            "${GPU_WEBGPU_TEXTURE_LINE_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TEXTURE_LINE_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_TEXTURE_LINE_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/texture-line/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-line.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURE_LINE_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-line.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURE_LINE_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-line.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/texture-line/texture_line.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/texture-line/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-texture-line-artifact
    DEPENDS "${GPU_WEBGPU_TEXTURE_LINE_US}"
  )

  add_executable(gpu-texture-line-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/texture-line/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-texture-line-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-texture-line-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TEXTURE_LINE_US}@/texture_line.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-line-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-texture-line-webgpu-usl
                   gpu-webgpu-texture-line-artifact)
  set_target_properties(gpu-texture-line-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-line-webgpu-usl-shell.html;${GPU_WEBGPU_TEXTURE_LINE_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_MSAA_SAMPLES_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_MSAA_SAMPLES_SOURCE
      "${GPU_WEBGPU_MSAA_SAMPLES_DIR}/msaa_samples.usl")
  set(GPU_WEBGPU_MSAA_SAMPLES_US
      "${GPU_WEBGPU_MSAA_SAMPLES_DIR}/msaa_samples.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "MSAA resolve + samples")
  set(GPU_WEBGPU_SAMPLE_KIND "Render + binding / resolve + per-sample read")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "One 4x color target is stored, resolved, and rebound so the hardware resolve can be compared with each explicit sample read.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/msaa-samples.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/msaa-samples.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/msaa-samples.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-samples-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_MSAA_SAMPLES_US}"
    BYPRODUCTS "${GPU_WEBGPU_MSAA_SAMPLES_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_MSAA_SAMPLES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/msaa-samples/msaa_samples.usl"
            "${GPU_WEBGPU_MSAA_SAMPLES_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_MSAA_SAMPLES_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_MSAA_SAMPLES_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/msaa-samples/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa-samples.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MSAA_SAMPLES_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa-samples.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_MSAA_SAMPLES_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/msaa-samples.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/msaa-samples/msaa_samples.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/msaa-samples/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-msaa-samples-artifact
    DEPENDS "${GPU_WEBGPU_MSAA_SAMPLES_US}"
  )

  add_executable(gpu-msaa-samples-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/msaa-samples/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-msaa-samples-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-msaa-samples-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_MSAA_SAMPLES_US}@/msaa_samples.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-samples-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-msaa-samples-webgpu-usl
                   gpu-webgpu-msaa-samples-artifact)
  set_target_properties(gpu-msaa-samples-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-msaa-samples-webgpu-usl-shell.html;${GPU_WEBGPU_MSAA_SAMPLES_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_DESCRIPTOR_ARRAY_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_DESCRIPTOR_ARRAY_SOURCE
      "${GPU_WEBGPU_DESCRIPTOR_ARRAY_DIR}/descriptor_array.usl")
  set(GPU_WEBGPU_DESCRIPTOR_ARRAY_US
      "${GPU_WEBGPU_DESCRIPTOR_ARRAY_DIR}/descriptor_array.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Descriptor arrays")
  set(GPU_WEBGPU_SAMPLE_KIND "Binding / fixed descriptor arrays")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "Dynamic texture, sampler, and buffer indices lower to bounded WebGPU binding ranges.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/descriptor-array.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/descriptor-array.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/descriptor-array.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-descriptor-array-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_DESCRIPTOR_ARRAY_US}"
    BYPRODUCTS "${GPU_WEBGPU_DESCRIPTOR_ARRAY_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_DESCRIPTOR_ARRAY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/descriptor-array/descriptor_array.usl"
            "${GPU_WEBGPU_DESCRIPTOR_ARRAY_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_DESCRIPTOR_ARRAY_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_DESCRIPTOR_ARRAY_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/descriptor-array/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/descriptor-array.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_DESCRIPTOR_ARRAY_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/descriptor-array.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_DESCRIPTOR_ARRAY_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/descriptor-array.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/descriptor-array/descriptor_array.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/descriptor-array/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-descriptor-array-artifact
    DEPENDS "${GPU_WEBGPU_DESCRIPTOR_ARRAY_US}"
  )

  add_executable(gpu-descriptor-array-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/descriptor-array/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-descriptor-array-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-descriptor-array-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_DESCRIPTOR_ARRAY_US}@/descriptor_array.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-descriptor-array-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-descriptor-array-webgpu-usl
                   gpu-webgpu-descriptor-array-artifact)
  set_target_properties(gpu-descriptor-array-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-descriptor-array-webgpu-usl-shell.html;${GPU_WEBGPU_DESCRIPTOR_ARRAY_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  set(GPU_WEBGPU_TEXTURE_SHAPES_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/usl/webgpu/samples")
  set(GPU_WEBGPU_TEXTURE_SHAPES_SOURCE
      "${GPU_WEBGPU_TEXTURE_SHAPES_DIR}/texture_shapes.usl")
  set(GPU_WEBGPU_TEXTURE_SHAPES_US
      "${GPU_WEBGPU_TEXTURE_SHAPES_DIR}/texture_shapes.us")
  set(GPU_WEBGPU_SAMPLE_TITLE "Texture shapes")
  set(GPU_WEBGPU_SAMPLE_KIND "Binding / cube + 3D")
  set(GPU_WEBGPU_SAMPLE_DESCRIPTION
      "A reflected cubemap and 3D volume share one bind group and one draw.")
  set(GPU_WEBGPU_SAMPLE_C_SOURCE "sources/texture-shapes.c")
  set(GPU_WEBGPU_SAMPLE_USL_SOURCE "sources/texture-shapes.usl")
  set(GPU_WEBGPU_SAMPLE_WGSL_SOURCE "sources/texture-shapes.wgsl")
  configure_file(
    "${GPU_WEBGPU_GALLERY_SOURCE_DIR}/sample-shell.html.in"
    "${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-shapes-webgpu-usl-shell.html"
    @ONLY
  )
  add_custom_command(
    OUTPUT "${GPU_WEBGPU_TEXTURE_SHAPES_US}"
    BYPRODUCTS "${GPU_WEBGPU_TEXTURE_SHAPES_SOURCE}.wgsl"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${GPU_WEBGPU_TEXTURE_SHAPES_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/texture-shapes/texture_shapes.usl"
            "${GPU_WEBGPU_TEXTURE_SHAPES_SOURCE}"
    COMMAND ${CMAKE_COMMAND} -E env
            USL_EMIT_BYTECODE=1
            "${GPU_USL_HOST_FIXTURE}"
            webgpu
            "${GPU_WEBGPU_TEXTURE_SHAPES_SOURCE}"
    COMMAND "${CMAKE_COMMAND}"
            "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
            "-DGPU_USL_SOURCE=${GPU_WEBGPU_TEXTURE_SHAPES_SOURCE}"
            -P "${GPU_WEBGPU_PACK_SCRIPT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/samples/gallery/texture-shapes/main.c"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-shapes.c"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURE_SHAPES_SOURCE}"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-shapes.usl"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_WEBGPU_TEXTURE_SHAPES_SOURCE}.wgsl"
            "${GPU_WEBGPU_GALLERY_DIR}/sources/texture-shapes.wgsl"
    DEPENDS
      "${GPU_USL_HOST_FIXTURE}"
      "${GPU_USL_HOST_PACKER}"
      "${GPU_WEBGPU_PACK_SCRIPT}"
      "${PROJECT_SOURCE_DIR}/samples/gallery/texture-shapes/texture_shapes.usl"
      "${PROJECT_SOURCE_DIR}/samples/gallery/texture-shapes/main.c"
    VERBATIM
  )
  add_custom_target(gpu-webgpu-texture-shapes-artifact
    DEPENDS "${GPU_WEBGPU_TEXTURE_SHAPES_US}"
  )

  add_executable(gpu-texture-shapes-webgpu-usl
    ${PROJECT_SOURCE_DIR}/samples/gallery/texture-shapes/main.c
    ${GPU_WEBGPU_SAMPLE_COMMON_SOURCE}
  )
  target_link_libraries(gpu-texture-shapes-webgpu-usl PRIVATE gpu)
  target_link_options(gpu-texture-shapes-webgpu-usl PRIVATE
    --use-port=emdawnwebgpu
    "--preload-file=${GPU_WEBGPU_TEXTURE_SHAPES_US}@/texture_shapes.us"
    "--shell-file=${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-shapes-webgpu-usl-shell.html"
    -sALLOW_MEMORY_GROWTH=1
  )
  add_dependencies(gpu-texture-shapes-webgpu-usl
                   gpu-webgpu-texture-shapes-artifact)
  set_target_properties(gpu-texture-shapes-webgpu-usl PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    LINK_DEPENDS
      "${GPU_WEBGPU_GALLERY_DIR}/gpu-texture-shapes-webgpu-usl-shell.html;${GPU_WEBGPU_TEXTURE_SHAPES_US}"
    SUFFIX ".html"
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_WEBGPU_GALLERY_DIR}"
  )

  gpu_add_webgpu_gallery_sample(
    blit
    blit
    "Texture blit"
    "Blit / source + nearest + linear"
    "The 32x32 source is magnified at left; nearest and linear 256x256 blit targets are shown at center and right."
  )
  gpu_add_webgpu_gallery_sample(
    compute-particles
    particles
    "Compute particles"
    "Compute / storage + indirect"
    "Compute updates a persistent particle field that render consumes through an indirect instanced draw."
  )
  gpu_add_webgpu_gallery_sample(
    mip-lod
    mip_lod
    "Mip chain + LOD"
    "Binding / explicit mip selection"
    "Five instanced panels sample distinct mip levels from one texture and one reflected sampler."
  )
  gpu_add_webgpu_gallery_sample(
    stencil-outline
    stencil_outline
    "Stencil outline"
    "Render / stencil mask"
    "A fill pass writes stencil, then a larger draw keeps only the visible outline."
  )
  gpu_add_webgpu_gallery_sample(
    bloom
    bloom
    "Separable bloom"
    "Compute / post-processing"
    "Two compute passes blur a bright source before render composites the glow."
  )
  gpu_add_webgpu_gallery_sample(
    image-texture
    image_texture
    "Image texture"
    "Binding / decoded image"
    "The browser decodes a real PNG; GPU uploads and samples the sRGB pixels."
    ASSETS
      texture-coordinate.png
  )
  gpu_add_webgpu_gallery_sample(
    integer-cube
    integer_cube
    "Integer cubemap"
    "Binding / typed cubemap fallback"
    "Nearest, explicit LOD, gradient, and bias sampling share one reflected unsigned cubemap."
  )
  gpu_add_webgpu_gallery_sample(
    color-pipeline
    color_pipeline
    "Color pipeline"
    "Render / HDR + tone mapping"
    "The left panel decodes sRGB; the right treats the same bytes as linear before HDR tone mapping."
  )
  gpu_add_webgpu_gallery_sample(
    compressed-texture
    compressed_texture
    "Compressed texture"
    "Binding / portable compression"
    "The adapter selects ASTC, BC, or ETC2 once; RGBA8 remains the portable fallback."
    ASSETS
      texture.astc
      texture.bc1
      texture.etc2
      texture.rgba
  )
  gpu_add_webgpu_gallery_sample(
    skinning
    skinning
    "GPU skinning"
    "Render / skeletal animation"
    "Four bone matrices deform an indexed mesh through one reflected dynamic uniform ring."
  )
  target_include_directories(gpu-skinning-webgpu-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  gpu_add_webgpu_gallery_sample(
    pbr-material
    pbr_material
    "PBR material"
    "Render / physically based material"
    "glTF metallic-roughness, tangent normals, split-sum IBL, and Khronos tone mapping share one reflected layout."
    USES_STDLIB
    ASSETS
      lut_ggx.png
      studio_diffuse.rgba16f
      studio_specular.rgba16f
  )
  target_include_directories(gpu-pbr-material-webgpu-usl PRIVATE
    "${GPU_CGLM_INCLUDE_DIR}"
  )
  target_link_options(gpu-pbr-material-webgpu-usl PRIVATE
    --use-preload-plugins
  )
  set_property(TARGET gpu-pbr-material-webgpu-usl APPEND PROPERTY
    LINK_DEPENDS
      "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/lut_ggx.png"
      "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_diffuse.rgba16f"
      "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_specular.rgba16f"
  )
  if(TARGET gpu-assetkit-webgpu)
    gpu_add_webgpu_gallery_sample(
      assetkit-damaged-helmet
      damaged_helmet
      "AssetKit DamagedHelmet"
      "Render / glTF asset pipeline"
      "<a class=\"project-link\" href=\"https://github.com/recp/assetkit\" target=\"_blank\" rel=\"noreferrer\">AssetKit</a> downloads Khronos DamagedHelmet, resolves its glTF material, and uploads the real indexed mesh and textures."
      USES_STDLIB
      ASSET_SOURCE asset.c
    )
    target_compile_definitions(
      gpu-assetkit-damaged-helmet-webgpu-usl PRIVATE AK_STATIC
    )
    target_include_directories(
      gpu-assetkit-damaged-helmet-webgpu-usl PRIVATE
        "${GPU_ASSETKIT_ROOT}/include"
        "${GPU_CGLM_INCLUDE_DIR}"
    )
    target_link_libraries(
      gpu-assetkit-damaged-helmet-webgpu-usl PRIVATE
        "${GPU_ASSETKIT_WEBGPU_LIBRARY}"
        "${GPU_ASSETKIT_WEBGPU_DS_LIBRARY}"
        "${GPU_ASSETKIT_WEBGPU_DEFLATE_LIBRARY}"
    )
    target_link_options(
      gpu-assetkit-damaged-helmet-webgpu-usl PRIVATE
        --use-preload-plugins
        -sFETCH=1
        "--preload-file=${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/lut_ggx.png@/lut_ggx.png"
        "--preload-file=${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_diffuse.rgba16f@/studio_diffuse.rgba16f"
        "--preload-file=${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_specular.rgba16f@/studio_specular.rgba16f"
    )
    set_property(
      TARGET gpu-assetkit-damaged-helmet-webgpu-usl APPEND PROPERTY
      LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/lut_ggx.png"
        "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_diffuse.rgba16f"
        "${PROJECT_SOURCE_DIR}/samples/gallery/pbr-material/studio_specular.rgba16f"
    )
    add_dependencies(
      gpu-assetkit-damaged-helmet-webgpu-usl gpu-assetkit-webgpu
    )
    target_sources(
      gpu-assetkit-damaged-helmet-webgpu-usl PRIVATE
        "${PROJECT_SOURCE_DIR}/samples/common/asset_io_web.c"
    )
  else()
    message(STATUS
      "AssetKit WebGPU sample disabled: GPU_ASSETKIT_ROOT is unavailable")
  endif()
  target_link_options(gpu-image-texture-webgpu-usl PRIVATE
    --use-preload-plugins
  )
  set_property(TARGET gpu-image-texture-webgpu-usl APPEND PROPERTY
    LINK_DEPENDS
      "${PROJECT_SOURCE_DIR}/samples/gallery/image-texture/texture-coordinate.png"
  )
endif()
