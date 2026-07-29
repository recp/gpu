get_property(GPU_WINDOWS_GALLERY_SAMPLE_IDS
             GLOBAL PROPERTY GPU_WINDOWS_GALLERY_SAMPLE_IDS)
get_property(GPU_WINDOWS_GALLERY_SAMPLE_TARGETS
             GLOBAL PROPERTY GPU_WINDOWS_GALLERY_SAMPLE_TARGETS)
list(LENGTH GPU_WINDOWS_GALLERY_SAMPLE_IDS
     GPU_WINDOWS_GALLERY_SAMPLE_COUNT)
list(LENGTH GPU_WINDOWS_GALLERY_SAMPLE_TARGETS
     GPU_WINDOWS_GALLERY_TARGET_COUNT)
if(NOT GPU_WINDOWS_GALLERY_SAMPLE_COUNT EQUAL
       GPU_WINDOWS_GALLERY_TARGET_COUNT)
  message(FATAL_ERROR "Windows gallery sample catalog is inconsistent")
endif()

set(GPU_WINDOWS_GALLERY_NATIVE_ENTRIES "")
if(GPU_WINDOWS_GALLERY_SAMPLE_COUNT GREATER 0)
  math(EXPR GPU_WINDOWS_GALLERY_LAST_SAMPLE
       "${GPU_WINDOWS_GALLERY_SAMPLE_COUNT} - 1")
  foreach(sampleIndex RANGE ${GPU_WINDOWS_GALLERY_LAST_SAMPLE})
    list(GET GPU_WINDOWS_GALLERY_SAMPLE_IDS
         ${sampleIndex}
         sampleId)
    list(GET GPU_WINDOWS_GALLERY_SAMPLE_TARGETS
         ${sampleIndex}
         sampleTarget)
    string(APPEND GPU_WINDOWS_GALLERY_NATIVE_ENTRIES
      "  {\"${sampleId}\", "
      "\"$<TARGET_FILE:${sampleTarget}>\", "
      "\"$<TARGET_FILE_DIR:gpu-gallery-windows>/previews/${sampleId}.png\"},\n")
  endforeach()
endif()

set(GPU_WINDOWS_GALLERY_NATIVE_SAMPLES
    "${GPU_WINDOWS_GALLERY_GENERATED_DIR}/NativeSamples-$<CONFIG>.c")
string(CONCAT GPU_WINDOWS_GALLERY_NATIVE_CONTENT
  "#include \"NativeSamples.h\"\n\n"
  "const GPUNativeSample gpuNativeSamples[] = {\n"
  "${GPU_WINDOWS_GALLERY_NATIVE_ENTRIES}"
  "};\n\n"
  "const size_t gpuNativeSampleCount =\n"
  "  sizeof(gpuNativeSamples) / sizeof(gpuNativeSamples[0]);\n"
)
file(GENERATE
  OUTPUT "${GPU_WINDOWS_GALLERY_NATIVE_SAMPLES}"
  CONTENT "${GPU_WINDOWS_GALLERY_NATIVE_CONTENT}"
)

add_executable(gpu-gallery-windows WIN32
  "${PROJECT_SOURCE_DIR}/samples/shell/windows/Gallery.c"
  "${PROJECT_SOURCE_DIR}/samples/common/Win32Image.c"
  "${GPU_WINDOWS_GALLERY_NATIVE_SAMPLES}"
)
target_compile_definitions(gpu-gallery-windows PRIVATE
  _CRT_SECURE_NO_WARNINGS
)
target_compile_options(gpu-gallery-windows PRIVATE /W3)
target_include_directories(gpu-gallery-windows PRIVATE
  "${PROJECT_SOURCE_DIR}/samples/shell/windows"
  "${PROJECT_SOURCE_DIR}/samples/common"
)
target_link_libraries(gpu-gallery-windows PRIVATE
  dwmapi
  gdi32
  ole32
  user32
  windowscodecs
)
set_target_properties(gpu-gallery-windows PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
  OUTPUT_NAME "GPU + USL Samples"
  RUNTIME_OUTPUT_DIRECTORY
    "${GPU_WINDOWS_GALLERY_OUTPUT_DIR}"
)

file(GLOB GPU_WINDOWS_GALLERY_PREVIEWS CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/samples/shell/web/previews/*.png"
)
add_custom_target(gpu-gallery-windows-resources
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_FILE_DIR:gpu-gallery-windows>/previews"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${PROJECT_SOURCE_DIR}/samples/shell/web/previews"
          "$<TARGET_FILE_DIR:gpu-gallery-windows>/previews"
  DEPENDS ${GPU_WINDOWS_GALLERY_PREVIEWS}
  VERBATIM
)
add_dependencies(gpu-gallery-windows
  gpu-gallery-windows-resources
  ${GPU_WINDOWS_GALLERY_SAMPLE_TARGETS}
)
