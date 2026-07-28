get_property(gallerySources GLOBAL PROPERTY GPU_ANDROID_GALLERY_SOURCES)
get_property(galleryAssetTargets
             GLOBAL PROPERTY GPU_ANDROID_GALLERY_ASSET_TARGETS)
get_property(galleryUsesAssetKit
             GLOBAL PROPERTY GPU_ANDROID_GALLERY_USES_ASSETKIT)

add_custom_target(gpu-android-gallery-assets
  DEPENDS ${galleryAssetTargets}
)

add_custom_command(
  OUTPUT "${GPU_ANDROID_GALLERY_DEX}"
  BYPRODUCTS "${GPU_ANDROID_GALLERY_JAR}"
  COMMAND ${CMAKE_COMMAND} -E rm -rf
          "${GPU_ANDROID_GALLERY_CLASSES}"
          "${GPU_ANDROID_GALLERY_DEX_DIR}"
          "${GPU_ANDROID_GALLERY_JAR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "${GPU_ANDROID_GALLERY_CLASSES}"
          "${GPU_ANDROID_GALLERY_DEX_DIR}"
  COMMAND "${GPU_ANDROID_JAVAC}"
          -source 8
          -target 8
          -bootclasspath "${GPU_ANDROID_PLATFORM_JAR}"
          -d "${GPU_ANDROID_GALLERY_CLASSES}"
          ${GPU_ANDROID_GALLERY_JAVA}
  COMMAND "${GPU_ANDROID_JAR}"
          --create
          --file "${GPU_ANDROID_GALLERY_JAR}"
          -C "${GPU_ANDROID_GALLERY_CLASSES}" .
  COMMAND "${GPU_ANDROID_D8}"
          --min-api 26
          --lib "${GPU_ANDROID_PLATFORM_JAR}"
          --output "${GPU_ANDROID_GALLERY_DEX_DIR}"
          "${GPU_ANDROID_GALLERY_JAR}"
  DEPENDS ${GPU_ANDROID_GALLERY_JAVA}
  VERBATIM
)
add_custom_target(gpu-android-gallery-dex
  DEPENDS "${GPU_ANDROID_GALLERY_DEX}"
)

add_library(gpu-android-gallery SHARED
  "${PROJECT_SOURCE_DIR}/samples/android-gallery/main.c"
  "${PROJECT_SOURCE_DIR}/samples/android-gallery/web_samples.c"
  "${PROJECT_SOURCE_DIR}/samples/common/android_webgpu.c"
  ${gallerySources}
)
target_include_directories(gpu-android-gallery PRIVATE
  "${GPU_ANDROID_GLUE_DIR}"
  "${GPU_CGLM_INCLUDE_DIR}"
)
target_link_libraries(gpu-android-gallery PRIVATE
  gpu-android-sample-common
  jnigraphics
)
if(galleryUsesAssetKit)
  if(NOT TARGET gpu-assetkit-android)
    message(FATAL_ERROR
      "Android AssetKit sample requires GPU_ASSETKIT_ROOT")
  endif()
  target_compile_definitions(gpu-android-gallery PRIVATE AK_STATIC)
  target_include_directories(gpu-android-gallery PRIVATE
    "${GPU_ASSETKIT_ROOT}/include"
  )
  target_link_libraries(gpu-android-gallery PRIVATE
    "${GPU_ASSETKIT_ANDROID_LIBRARY}"
    "${GPU_ASSETKIT_ANDROID_DS_LIBRARY}"
    "${GPU_ASSETKIT_ANDROID_DEFLATE_LIBRARY}"
    dl
    m
  )
  add_dependencies(gpu-android-gallery gpu-assetkit-android)
endif()
target_link_options(gpu-android-gallery PRIVATE
  "LINKER:-u,ANativeActivity_onCreate"
)
target_compile_options(gpu-android-gallery PRIVATE
  -Wall
  -Wextra
  -Werror
)
set_target_properties(gpu-android-gallery PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED YES
  C_EXTENSIONS NO
  OUTPUT_NAME "gpu_samples"
  LIBRARY_OUTPUT_DIRECTORY "${GPU_ANDROID_GALLERY_DIR}"
)

file(GLOB_RECURSE gallerySourceFiles
     CONFIGURE_DEPENDS
     "${PROJECT_SOURCE_DIR}/samples/gallery/*")
set(galleryApk
    "${GPU_ANDROID_GALLERY_DIR}/gpu-android-gallery.apk")
add_custom_command(
  OUTPUT "${galleryApk}"
  COMMAND "${CMAKE_COMMAND}"
          "-DGPU_ANDROID_AAPT2=${GPU_ANDROID_AAPT2}"
          "-DGPU_ANDROID_ZIP=${GPU_ANDROID_ZIP}"
          "-DGPU_ANDROID_ZIPALIGN=${GPU_ANDROID_ZIPALIGN}"
          "-DGPU_ANDROID_APKSIGNER=${GPU_ANDROID_APKSIGNER}"
          "-DGPU_ANDROID_PLATFORM_JAR=${GPU_ANDROID_PLATFORM_JAR}"
          "-DGPU_ANDROID_MANIFEST=${PROJECT_SOURCE_DIR}/samples/android-gallery/AndroidManifest.xml"
          "-DGPU_ANDROID_LIBRARY=$<TARGET_FILE:gpu-android-gallery>"
          -DGPU_ANDROID_LIBRARY_NAME=gpu_samples
          "-DGPU_ANDROID_ABI=${ANDROID_ABI}"
          "-DGPU_ANDROID_DEX=${GPU_ANDROID_GALLERY_DEX}"
          "-DGPU_ANDROID_GENERATED_ASSET_DIR=${GPU_ANDROID_GALLERY_ASSET_DIR}"
          "-DGPU_ANDROID_SAMPLE_SOURCE_DIR=${PROJECT_SOURCE_DIR}/samples/gallery"
          "-DGPU_ANDROID_PREVIEW_DIR=${PROJECT_SOURCE_DIR}/samples/webgpu-gallery/previews"
          "-DGPU_ANDROID_KEYSTORE=${GPU_ANDROID_DEBUG_KEYSTORE}"
          "-DGPU_ANDROID_OUTPUT=${galleryApk}"
          "-DGPU_ANDROID_STAGE=${GPU_ANDROID_GALLERY_DIR}/apk"
          -P "${PROJECT_SOURCE_DIR}/cmake/PackageAndroidGallery.cmake"
  DEPENDS
    gpu-android-gallery
    gpu-android-gallery-assets
    gpu-android-gallery-dex
    ${gallerySourceFiles}
    "${PROJECT_SOURCE_DIR}/cmake/PackageAndroidGallery.cmake"
    "${PROJECT_SOURCE_DIR}/samples/android-gallery/AndroidManifest.xml"
    "${PROJECT_SOURCE_DIR}/samples/webgpu-gallery/previews"
  VERBATIM
)
add_custom_target(gpu-android-gallery-apk ALL
  DEPENDS "${galleryApk}"
)
