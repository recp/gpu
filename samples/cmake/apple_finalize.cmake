set(GPU_APPLE_GALLERY_DEFAULT OFF)
if(CMAKE_GENERATOR STREQUAL "Xcode" OR CMAKE_GENERATOR MATCHES "^Ninja")
  set(GPU_APPLE_GALLERY_DEFAULT ON)
endif()
option(GPU_BUILD_APPLE_GALLERY "Build the Swift Apple gallery shell"
       ${GPU_APPLE_GALLERY_DEFAULT})
if(NOT GPU_BUILD_APPLE_GALLERY)
  message(STATUS
    "Swift Apple gallery disabled; native samples remain enabled. "
    "Use Ninja or Xcode to build the gallery shell.")
  return()
endif()

enable_language(Swift)

set(GPU_APPLE_SHELL_DIR
    "${PROJECT_SOURCE_DIR}/samples/shell/apple")
set(GPU_APPLE_GALLERY_GENERATED_DIR
    "${PROJECT_BINARY_DIR}/generated/shell/apple/$<CONFIG>")
set(GPU_APPLE_GALLERY_NATIVE_PATHS
    "${GPU_APPLE_GALLERY_GENERATED_DIR}/native-samples.json")
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

set(GPU_APPLE_GALLERY_NATIVE_ENTRIES)
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
    list(APPEND GPU_APPLE_GALLERY_NATIVE_ENTRIES
      "  \"${sampleId}\": \"$<TARGET_FILE:${sampleTarget}>\"")
  endforeach()
endif()

list(JOIN GPU_APPLE_GALLERY_NATIVE_ENTRIES ",\n"
     GPU_APPLE_GALLERY_NATIVE_PATHS_CONTENT)
file(GENERATE
     OUTPUT "${GPU_APPLE_GALLERY_NATIVE_PATHS}"
     CONTENT "{\n${GPU_APPLE_GALLERY_NATIVE_PATHS_CONTENT}\n}\n")

file(GLOB GPU_APPLE_GALLERY_PREVIEWS CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/samples/shell/web/previews/*.png")
set(GPU_APPLE_GALLERY_CATALOG
    "${PROJECT_SOURCE_DIR}/samples/catalog.json")

add_executable(gpu-gallery-apple MACOSX_BUNDLE
  "${GPU_APPLE_SHELL_DIR}/GalleryApp.swift"
  "${GPU_APPLE_SHELL_DIR}/NativeSamples.swift"
)
add_custom_target(gpu-gallery-apple-resources
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_CONTENT_DIR:gpu-gallery-apple>/Resources/previews"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GPU_APPLE_GALLERY_CATALOG}"
          "$<TARGET_BUNDLE_CONTENT_DIR:gpu-gallery-apple>/Resources/catalog.json"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${GPU_APPLE_GALLERY_NATIVE_PATHS}"
          "$<TARGET_BUNDLE_CONTENT_DIR:gpu-gallery-apple>/Resources/native-samples.json"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${PROJECT_SOURCE_DIR}/samples/shell/web/previews"
          "$<TARGET_BUNDLE_CONTENT_DIR:gpu-gallery-apple>/Resources/previews"
  DEPENDS
    "${GPU_APPLE_GALLERY_CATALOG}"
    "${GPU_APPLE_GALLERY_NATIVE_PATHS}"
    ${GPU_APPLE_GALLERY_PREVIEWS}
  VERBATIM
)
add_dependencies(gpu-gallery-apple
  gpu-gallery-apple-resources
  ${GPU_APPLE_GALLERY_SAMPLE_TARGETS}
)
set_target_properties(gpu-gallery-apple PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "gpu.samples"
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "gpu.samples"
  MACOSX_BUNDLE_BUNDLE_NAME "GPU + USL Samples"
  OUTPUT_NAME "GPU + USL Samples"
  RUNTIME_OUTPUT_DIRECTORY
    "${PROJECT_BINARY_DIR}/samples/gpu-gallery-apple"
  XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
)
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
  set_target_properties(gpu-gallery-apple PROPERTIES Swift_LANGUAGE_VERSION 5)
endif()
