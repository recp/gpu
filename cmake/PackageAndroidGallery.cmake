foreach(required
        GPU_ANDROID_AAPT2
        GPU_ANDROID_ZIP
        GPU_ANDROID_ZIPALIGN
        GPU_ANDROID_APKSIGNER
        GPU_ANDROID_PLATFORM_JAR
        GPU_ANDROID_MANIFEST
        GPU_ANDROID_LIBRARY
        GPU_ANDROID_LIBRARY_NAME
        GPU_ANDROID_ABI
        GPU_ANDROID_DEX
        GPU_ANDROID_GENERATED_ASSET_DIR
        GPU_ANDROID_SAMPLE_SOURCE_DIR
        GPU_ANDROID_PREVIEW_DIR
        GPU_ANDROID_KEYSTORE
        GPU_ANDROID_OUTPUT
        GPU_ANDROID_STAGE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Missing Android gallery input: ${required}")
  endif()
endforeach()

file(REMOVE_RECURSE "${GPU_ANDROID_STAGE}")
file(MAKE_DIRECTORY
  "${GPU_ANDROID_STAGE}/lib/${GPU_ANDROID_ABI}"
  "${GPU_ANDROID_STAGE}/assets/previews"
  "${GPU_ANDROID_STAGE}/assets/sources"
)
file(COPY_FILE
  "${GPU_ANDROID_LIBRARY}"
  "${GPU_ANDROID_STAGE}/lib/${GPU_ANDROID_ABI}/lib${GPU_ANDROID_LIBRARY_NAME}.so"
)
file(COPY_FILE
  "${GPU_ANDROID_DEX}"
  "${GPU_ANDROID_STAGE}/classes.dex"
)
file(COPY
  "${GPU_ANDROID_GENERATED_ASSET_DIR}/"
  DESTINATION "${GPU_ANDROID_STAGE}/assets"
)
file(COPY
  "${GPU_ANDROID_PREVIEW_DIR}/"
  DESTINATION "${GPU_ANDROID_STAGE}/assets/previews"
  FILES_MATCHING PATTERN "*.png"
)
file(COPY
  "${GPU_ANDROID_SAMPLE_SOURCE_DIR}/"
  DESTINATION "${GPU_ANDROID_STAGE}/assets/sources"
  FILES_MATCHING
    PATTERN "*.c"
    PATTERN "*.h"
    PATTERN "*.usl"
)

file(GLOB_RECURSE runtimeAssets
  RELATIVE "${GPU_ANDROID_SAMPLE_SOURCE_DIR}"
  "${GPU_ANDROID_SAMPLE_SOURCE_DIR}/*"
)
foreach(asset IN LISTS runtimeAssets)
  if(NOT asset MATCHES "\\.(png|rgba|rgba16f|astc|bc1|etc2)$" OR
     IS_DIRECTORY "${GPU_ANDROID_SAMPLE_SOURCE_DIR}/${asset}")
    continue()
  endif()
  get_filename_component(assetName "${asset}" NAME)
  file(COPY_FILE
    "${GPU_ANDROID_SAMPLE_SOURCE_DIR}/${asset}"
    "${GPU_ANDROID_STAGE}/assets/${assetName}"
  )
endforeach()

set(unsigned "${GPU_ANDROID_STAGE}/unsigned.apk")
set(aligned  "${GPU_ANDROID_STAGE}/aligned.apk")
execute_process(
  COMMAND "${GPU_ANDROID_AAPT2}" link
          -o "${unsigned}"
          --manifest "${GPU_ANDROID_MANIFEST}"
          -I "${GPU_ANDROID_PLATFORM_JAR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "aapt2 failed:\n${output}${error}")
endif()

execute_process(
  COMMAND "${GPU_ANDROID_ZIP}" -q -r "${unsigned}" classes.dex lib assets
  WORKING_DIRECTORY "${GPU_ANDROID_STAGE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "zip failed:\n${output}${error}")
endif()

execute_process(
  COMMAND "${GPU_ANDROID_ZIPALIGN}" -f 4 "${unsigned}" "${aligned}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "zipalign failed:\n${output}${error}")
endif()

execute_process(
  COMMAND "${GPU_ANDROID_APKSIGNER}" sign
          --ks "${GPU_ANDROID_KEYSTORE}"
          --ks-pass pass:android
          --key-pass pass:android
          --out "${GPU_ANDROID_OUTPUT}"
          "${aligned}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "apksigner failed:\n${output}${error}")
endif()
