if(GPU_BUILD_ANDROID_SAMPLES)
  if(BUILD_SHARED_LIBS)
    message(FATAL_ERROR
      "GPU_BUILD_ANDROID_SAMPLES requires BUILD_SHARED_LIBS=OFF so each APK "
      "contains one self-contained native library")
  endif()

  set(GPU_USL_HOST_FIXTURE "" CACHE FILEPATH
      "Host gpu-usl-fixture executable used for Android samples")
  set(GPU_USL_HOST_PACKER "" CACHE FILEPATH
      "Host uslpack executable used for Android samples")
  set(GPU_ANDROID_SDK_ROOT "$ENV{ANDROID_SDK_ROOT}" CACHE PATH
      "Android SDK root used to package NativeActivity samples")
  set(GPU_ANDROID_DEBUG_KEYSTORE
      "$ENV{HOME}/.android/debug.keystore"
      CACHE FILEPATH "Android debug keystore used to sign sample APKs")

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
      "Android samples require a host gpu-usl-fixture. Set "
      "GPU_USL_HOST_FIXTURE.")
  endif()
  if(NOT EXISTS "${GPU_USL_HOST_PACKER}")
    message(FATAL_ERROR
      "Android samples require a host uslpack. Set GPU_USL_HOST_PACKER.")
  endif()
  if(NOT GPU_ANDROID_SDK_ROOT)
    set(GPU_ANDROID_SDK_ROOT "$ENV{ANDROID_HOME}")
  endif()
  if(NOT GPU_ANDROID_SDK_ROOT)
    foreach(_gpu_android_sdk_candidate
            "$ENV{HOME}/Library/Developer/Android/sdk"
            "$ENV{HOME}/Android/Sdk")
      if(EXISTS "${_gpu_android_sdk_candidate}")
        set(GPU_ANDROID_SDK_ROOT "${_gpu_android_sdk_candidate}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT EXISTS "${GPU_ANDROID_SDK_ROOT}")
    message(FATAL_ERROR
      "Android samples require ANDROID_SDK_ROOT or GPU_ANDROID_SDK_ROOT.")
  endif()
  if(NOT EXISTS "${GPU_ANDROID_DEBUG_KEYSTORE}")
    message(FATAL_ERROR
      "Android debug keystore not found. Set GPU_ANDROID_DEBUG_KEYSTORE.")
  endif()

  file(GLOB _gpu_android_build_tools LIST_DIRECTORIES true
       "${GPU_ANDROID_SDK_ROOT}/build-tools/*")
  if(NOT _gpu_android_build_tools)
    message(FATAL_ERROR
      "Android SDK build-tools are not installed under "
      "${GPU_ANDROID_SDK_ROOT}.")
  endif()
  list(SORT _gpu_android_build_tools COMPARE NATURAL ORDER DESCENDING)
  list(GET _gpu_android_build_tools 0 _gpu_android_build_tools_dir)
  file(GLOB _gpu_android_platform_jars
       "${GPU_ANDROID_SDK_ROOT}/platforms/android-*/android.jar")
  if(NOT _gpu_android_platform_jars)
    message(FATAL_ERROR
      "No Android SDK platform is installed under ${GPU_ANDROID_SDK_ROOT}.")
  endif()
  list(SORT _gpu_android_platform_jars COMPARE NATURAL ORDER DESCENDING)
  list(GET _gpu_android_platform_jars 0 GPU_ANDROID_PLATFORM_JAR)

  set(GPU_ANDROID_AAPT2
      "${_gpu_android_build_tools_dir}/aapt2")
  set(GPU_ANDROID_ZIPALIGN
      "${_gpu_android_build_tools_dir}/zipalign")
  set(GPU_ANDROID_APKSIGNER
      "${_gpu_android_build_tools_dir}/apksigner")
  set(GPU_ANDROID_D8
      "${_gpu_android_build_tools_dir}/d8")
  foreach(_gpu_android_tool
          GPU_ANDROID_AAPT2
          GPU_ANDROID_ZIPALIGN
          GPU_ANDROID_APKSIGNER
          GPU_ANDROID_D8
          GPU_ANDROID_PLATFORM_JAR)
    if(NOT EXISTS "${${_gpu_android_tool}}")
      message(FATAL_ERROR
        "Android packaging tool not found: ${_gpu_android_tool}")
    endif()
  endforeach()
  find_program(GPU_ANDROID_ZIP NAMES zip REQUIRED)
  find_program(GPU_ANDROID_JAVAC NAMES javac REQUIRED)
  find_program(GPU_ANDROID_JAR NAMES jar REQUIRED)

  set(GPU_ANDROID_GLUE_DIR
      "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")
  add_library(gpu-android-native-app-glue STATIC
    "${GPU_ANDROID_GLUE_DIR}/android_native_app_glue.c"
  )
  target_include_directories(gpu-android-native-app-glue PUBLIC
    "${GPU_ANDROID_GLUE_DIR}"
  )
  target_link_libraries(gpu-android-native-app-glue PUBLIC android log)

  find_path(GPU_CGLM_INCLUDE_DIR
    NAMES cglm/cglm.h
    HINTS
      "${PROJECT_SOURCE_DIR}/../glm/include"
      /opt/homebrew/include
    NO_CMAKE_FIND_ROOT_PATH
  )
  if(NOT GPU_CGLM_INCLUDE_DIR)
    message(FATAL_ERROR
      "Android samples require cglm headers. Set GPU_CGLM_INCLUDE_DIR.")
  endif()

  add_library(gpu-android-sample-common STATIC
    ${PROJECT_SOURCE_DIR}/samples/common/android.c
  )
  target_include_directories(gpu-android-sample-common PUBLIC
    "${PROJECT_SOURCE_DIR}/samples/common"
    "${GPU_ANDROID_GLUE_DIR}"
  )
  target_link_libraries(gpu-android-sample-common PUBLIC
    gpu
    gpu-android-native-app-glue
    android
    log
  )
  target_compile_options(gpu-android-sample-common PRIVATE
    -Wall
    -Wextra
    -Werror
  )
  set_target_properties(gpu-android-sample-common PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
  )

  function(gpu_add_android_usl_artifact target shader_source out_dir out_us)
    set(options USES_STDLIB)
    set(oneValueArgs CAPS FALLBACK_CAPS)
    cmake_parse_arguments(GPU_ANDROID_SHADER
      "${options}" "${oneValueArgs}" "" ${ARGN})

    get_filename_component(_gpu_android_shader_name
                           "${shader_source}" NAME_WE)
    get_filename_component(_gpu_android_shader_source
                           "${shader_source}" ABSOLUTE
                           BASE_DIR "${PROJECT_SOURCE_DIR}")

    set(_gpu_android_dir
        "${PROJECT_BINARY_DIR}/android/${target}")
    set(_gpu_android_usl
        "${_gpu_android_dir}/${_gpu_android_shader_name}.usl")
    set(_gpu_android_us
        "${_gpu_android_dir}/${_gpu_android_shader_name}.us")
    set(_gpu_android_environment
        USL_EMIT_BYTECODE=1)
    if(GPU_ANDROID_SHADER_USES_STDLIB)
      list(APPEND _gpu_android_environment
        "USL_STDLIB_PATH=${GPU_USL_ROOT}/stdlib")
    endif()
    if(GPU_ANDROID_SHADER_CAPS)
      list(APPEND _gpu_android_environment
        "USL_TARGET_CAPS=${GPU_ANDROID_SHADER_CAPS}")
    endif()
    set(_gpu_android_fallback_pack_command)
    if(GPU_ANDROID_SHADER_FALLBACK_CAPS)
      list(APPEND _gpu_android_fallback_pack_command
        COMMAND "${CMAKE_COMMAND}"
                "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
                "-DGPU_USL_SOURCE=${_gpu_android_usl}"
                -DGPU_USL_TARGET=spirv
                -DGPU_USL_TARGET_PROFILE=vulkan1.1
                "-DGPU_USL_CAPS=${GPU_ANDROID_SHADER_FALLBACK_CAPS}"
                -P "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
      )
    endif()

    add_custom_command(
      OUTPUT "${_gpu_android_us}"
      BYPRODUCTS "${_gpu_android_usl}.spvasm"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${_gpu_android_dir}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${_gpu_android_shader_source}"
              "${_gpu_android_usl}"
      COMMAND ${CMAKE_COMMAND} -E env
              ${_gpu_android_environment}
              "${GPU_USL_HOST_FIXTURE}"
              vulkan
              "${_gpu_android_usl}"
      COMMAND "${CMAKE_COMMAND}"
              "-DGPU_USL_PACKER=${GPU_USL_HOST_PACKER}"
              "-DGPU_USL_SOURCE=${_gpu_android_usl}"
              -DGPU_USL_TARGET=spirv
              -DGPU_USL_TARGET_PROFILE=vulkan1.1
              "-DGPU_USL_CAPS=${GPU_ANDROID_SHADER_CAPS}"
              -P "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
      ${_gpu_android_fallback_pack_command}
      DEPENDS
        "${GPU_USL_HOST_FIXTURE}"
        "${GPU_USL_HOST_PACKER}"
        "${PROJECT_SOURCE_DIR}/cmake/PackUSLArtifact.cmake"
        "${_gpu_android_shader_source}"
      VERBATIM
    )
    add_custom_target(${target}-artifact DEPENDS "${_gpu_android_us}")
    set(${out_dir} "${_gpu_android_dir}" PARENT_SCOPE)
    set(${out_us} "${_gpu_android_us}" PARENT_SCOPE)
  endfunction()

endif()
