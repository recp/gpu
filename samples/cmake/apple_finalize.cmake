enable_language(Swift)

set(GPU_APPLE_SHELL_DIR
    "${PROJECT_SOURCE_DIR}/samples/shell/apple")
set(GPU_APPLE_GALLERY_GENERATED_DIR
    "${PROJECT_BINARY_DIR}/generated/shell/apple/$<CONFIG>")
set(GPU_APPLE_GALLERY_NATIVE_SAMPLES
    "${GPU_APPLE_GALLERY_GENERATED_DIR}/NativeSamples.swift")
get_property(GPU_APPLE_GALLERY_SAMPLE_IDS
             GLOBAL PROPERTY GPU_APPLE_GALLERY_SAMPLE_IDS)
get_property(GPU_APPLE_GALLERY_SAMPLE_TARGETS
             GLOBAL PROPERTY GPU_APPLE_GALLERY_SAMPLE_TARGETS)
list(LENGTH GPU_APPLE_GALLERY_SAMPLE_IDS
     GPU_APPLE_GALLERY_SAMPLE_COUNT)
list(LENGTH GPU_APPLE_GALLERY_SAMPLE_TARGETS
     GPU_APPLE_GALLERY_TARGET_COUNT)
if(NOT GPU_APPLE_GALLERY_SAMPLE_COUNT EQUAL
       GPU_APPLE_GALLERY_TARGET_COUNT)
  message(FATAL_ERROR "Apple gallery sample catalog is inconsistent")
endif()

set(GPU_APPLE_GALLERY_NATIVE_ENTRIES "")
if(GPU_APPLE_GALLERY_SAMPLE_COUNT GREATER 0)
  math(EXPR GPU_APPLE_GALLERY_LAST_SAMPLE
       "${GPU_APPLE_GALLERY_SAMPLE_COUNT} - 1")
  foreach(sampleIndex RANGE ${GPU_APPLE_GALLERY_LAST_SAMPLE})
    list(GET GPU_APPLE_GALLERY_SAMPLE_IDS
         ${sampleIndex}
         sampleId)
    list(GET GPU_APPLE_GALLERY_SAMPLE_TARGETS
         ${sampleIndex}
         sampleTarget)
    string(APPEND GPU_APPLE_GALLERY_NATIVE_ENTRIES
      "    \"${sampleId}\": \"$<TARGET_FILE:${sampleTarget}>\",\n")
  endforeach()
endif()

file(READ
     "${GPU_APPLE_SHELL_DIR}/NativeSamples.swift.in"
     GPU_APPLE_GALLERY_NATIVE_SAMPLES_CONTENT)
string(REPLACE
       "@GPU_APPLE_GALLERY_NATIVE_ENTRIES@"
       "${GPU_APPLE_GALLERY_NATIVE_ENTRIES}"
       GPU_APPLE_GALLERY_NATIVE_SAMPLES_CONTENT
       "${GPU_APPLE_GALLERY_NATIVE_SAMPLES_CONTENT}")
file(GENERATE
     OUTPUT "${GPU_APPLE_GALLERY_NATIVE_SAMPLES}"
     CONTENT "${GPU_APPLE_GALLERY_NATIVE_SAMPLES_CONTENT}")

file(GLOB GPU_APPLE_GALLERY_PREVIEWS CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/samples/shell/web/previews/*.png")
set(GPU_APPLE_GALLERY_CATALOG
    "${PROJECT_SOURCE_DIR}/samples/catalog.json")

add_executable(gpu-gallery-apple MACOSX_BUNDLE
  "${GPU_APPLE_SHELL_DIR}/GalleryApp.swift"
  "${GPU_APPLE_GALLERY_NATIVE_SAMPLES}"
)
add_custom_target(gpu-gallery-apple-resources
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_CONTENT_DIR:gpu-gallery-apple>/Resources/previews"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GPU_APPLE_GALLERY_CATALOG}"
          "$<TARGET_BUNDLE_CONTENT_DIR:gpu-gallery-apple>/Resources/catalog.json"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${PROJECT_SOURCE_DIR}/samples/shell/web/previews"
          "$<TARGET_BUNDLE_CONTENT_DIR:gpu-gallery-apple>/Resources/previews"
  DEPENDS
    "${GPU_APPLE_GALLERY_CATALOG}"
    ${GPU_APPLE_GALLERY_PREVIEWS}
  VERBATIM
)
add_dependencies(gpu-gallery-apple
  gpu-gallery-apple-resources
  ${GPU_APPLE_GALLERY_SAMPLE_TARGETS}
)
set_target_properties(gpu-gallery-apple PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "gpu.samples"
  MACOSX_BUNDLE_BUNDLE_NAME "GPU + USL Samples"
  OUTPUT_NAME "GPU + USL Samples"
  RUNTIME_OUTPUT_DIRECTORY
    "${PROJECT_BINARY_DIR}/samples/gpu-gallery-apple"
  Swift_LANGUAGE_VERSION 5
)
