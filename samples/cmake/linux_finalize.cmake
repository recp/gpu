function(gpu_linux_gallery_shell windowSystem)
  string(TOLOWER "${windowSystem}" windowSystemLower)
  if(windowSystem STREQUAL "XLIB")
    set(gdkBackend "x11")
    set(galleryDefinitions GPU_LINUX_GALLERY_XLIB=1)
  else()
    set(gdkBackend "${windowSystemLower}")
    set(galleryDefinitions)
  endif()
  get_property(sampleIds GLOBAL PROPERTY
    "GPU_LINUX_${windowSystem}_SAMPLE_IDS")
  get_property(sampleTargets GLOBAL PROPERTY
    "GPU_LINUX_${windowSystem}_SAMPLE_TARGETS")
  list(LENGTH sampleIds sampleCount)
  list(LENGTH sampleTargets targetCount)
  if(NOT sampleCount EQUAL targetCount)
    message(FATAL_ERROR
      "${windowSystem} gallery sample catalog is inconsistent")
  endif()

  set(nativeEntries "")
  if(sampleCount GREATER 0)
    math(EXPR lastSample "${sampleCount} - 1")
    foreach(sampleIndex RANGE ${lastSample})
      list(GET sampleIds ${sampleIndex} sampleId)
      list(GET sampleTargets ${sampleIndex} sampleTarget)
      string(APPEND nativeEntries
        "  {\"${sampleId}\", "
        "\"$<TARGET_FILE:${sampleTarget}>\", "
        "\"${PROJECT_SOURCE_DIR}/samples/shell/web/previews/${sampleId}.png\"},\n")
    endforeach()
  endif()

  set(nativeSamples
      "${GPU_LINUX_GALLERY_GENERATED_DIR}/NativeSamples-${windowSystemLower}.c")
  string(CONCAT nativeSamplesContent
      "#include \"NativeSamples.h\"\n\n"
      "const GPUNativeSample gpuNativeSamples[] = {\n"
      "${nativeEntries}"
      "};\n\n"
      "const size_t gpuNativeSampleCount =\n"
      "  sizeof(gpuNativeSamples) / sizeof(gpuNativeSamples[0]);\n")
  file(GENERATE
    OUTPUT "${nativeSamples}"
    CONTENT "${nativeSamplesContent}"
  )

  set(target "gpu-gallery-${windowSystemLower}")
  add_executable(${target}
    "${PROJECT_SOURCE_DIR}/samples/shell/linux/Gallery.c"
    "${nativeSamples}"
  )
  target_compile_definitions(${target} PRIVATE
    "GPU_LINUX_GDK_BACKEND=\"${gdkBackend}\""
    "GPU_LINUX_GALLERY_TITLE=\"GPU + USL ${windowSystem} Samples\""
    "GPU_LINUX_APPLICATION_ID=\"gpu.samples.${windowSystemLower}\""
    ${galleryDefinitions}
  )
  target_include_directories(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/samples/shell/linux"
  )
  target_link_libraries(${target} PRIVATE PkgConfig::GPU_GTK3)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
    RUNTIME_OUTPUT_DIRECTORY
      "${GPU_LINUX_GALLERY_OUTPUT_DIR}/${windowSystemLower}"
  )
  add_dependencies(${target} ${sampleTargets})
endfunction()

if(GPU_VULKAN_HAS_XLIB)
  gpu_linux_gallery_shell(XLIB)
endif()
if(GPU_VULKAN_HAS_WAYLAND)
  gpu_linux_gallery_shell(WAYLAND)
endif()
