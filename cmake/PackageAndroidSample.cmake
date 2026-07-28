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
        GPU_ANDROID_ASSET
        GPU_ANDROID_ASSET_NAME
        GPU_ANDROID_KEYSTORE
        GPU_ANDROID_OUTPUT
        GPU_ANDROID_STAGE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "Missing Android package input: ${required}")
  endif()
endforeach()

file(REMOVE_RECURSE "${GPU_ANDROID_STAGE}")
file(MAKE_DIRECTORY
  "${GPU_ANDROID_STAGE}/lib/${GPU_ANDROID_ABI}"
  "${GPU_ANDROID_STAGE}/assets"
)
file(COPY_FILE
  "${GPU_ANDROID_LIBRARY}"
  "${GPU_ANDROID_STAGE}/lib/${GPU_ANDROID_ABI}/lib${GPU_ANDROID_LIBRARY_NAME}.so"
)
file(COPY_FILE
  "${GPU_ANDROID_ASSET}"
  "${GPU_ANDROID_STAGE}/assets/${GPU_ANDROID_ASSET_NAME}"
)

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
  COMMAND "${GPU_ANDROID_ZIP}" -q -r "${unsigned}" lib assets
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
