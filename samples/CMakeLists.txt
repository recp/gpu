set(GPU_ASSETKIT_ROOT
    "${PROJECT_SOURCE_DIR}/../assetio"
    CACHE PATH "AssetKit checkout used by gallery asset samples")

if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL AND APPLE AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  include(cmake/apple.cmake)
endif()

if(GPU_BUILD_ANDROID_SAMPLES)
  include(cmake/android.cmake)
endif()

function(gpu_gallery_sample sampleDir)
  set(options ASSETKIT USES_STDLIB)
  set(multiValueArgs CAPS FALLBACK_CAPS)
  cmake_parse_arguments(GPU_SAMPLE
    "${options}" "" "${multiValueArgs}" ${ARGN})

  get_filename_component(sampleId "${sampleDir}" NAME)
  file(GLOB sampleSources CONFIGURE_DEPENDS
    "${sampleDir}/*.c"
    "${sampleDir}/*.h"
  )
  file(GLOB shaderSources CONFIGURE_DEPENDS
    "${sampleDir}/*.usl"
  )

  if(GPU_BUILD_ANDROID_SAMPLES)
    set(mainSource "${sampleDir}/main.c")
    if(NOT EXISTS "${mainSource}")
      message(FATAL_ERROR "${sampleId} has no gallery main.c")
    endif()

    set(extraSources ${sampleSources})
    list(REMOVE_ITEM extraSources "${mainSource}")
    list(FILTER extraSources INCLUDE REGEX "\\.c$")
    gpu_android_gallery_source(
      "${sampleId}"
      "${mainSource}"
      ${extraSources}
    )
    if(GPU_SAMPLE_ASSETKIT)
      set_property(GLOBAL PROPERTY GPU_ANDROID_GALLERY_USES_ASSETKIT TRUE)
    endif()

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

      set(shaderArgs)
      if(GPU_SAMPLE_USES_STDLIB)
        list(APPEND shaderArgs USES_STDLIB)
      endif()
      if(shaderCaps)
        list(APPEND shaderArgs CAPS "${shaderCaps}")
      endif()
      if(shaderFallbackCaps)
        list(APPEND shaderArgs FALLBACK_CAPS "${shaderFallbackCaps}")
      endif()

      gpu_android_gallery_shader(
        "${sampleId}-${shaderStem}"
        "${shaderSource}"
        ${shaderArgs}
      )
    endforeach()
  endif()

  if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL AND APPLE AND
     NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(appleOptions)
    if(GPU_SAMPLE_ASSETKIT)
      list(APPEND appleOptions ASSETKIT)
    endif()
    if(GPU_SAMPLE_USES_STDLIB)
      list(APPEND appleOptions USES_STDLIB)
    endif()
    if(GPU_SAMPLE_CAPS)
      list(APPEND appleOptions CAPS ${GPU_SAMPLE_CAPS})
    endif()
    gpu_apple_gallery_sample("${sampleDir}" ${appleOptions})
  endif()
endfunction()

set(galleryOptions_assetkit_damaged_helmet
  ASSETKIT
  USES_STDLIB
)
set(galleryOptions_descriptor_array
  CAPS descriptor_array=descriptor_indexing
  FALLBACK_CAPS descriptor_array=bounded_descriptor_indexing
)
set(galleryOptions_pbr_material
  USES_STDLIB
)
set(galleryOptions_shader_f16
  CAPS shader_f16=shader_f16
)
set(galleryOptions_subgroup
  CAPS subgroup=subgroup
)

file(GLOB galleryMainSources
  CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/gallery/*/main.c"
)
list(SORT galleryMainSources)

foreach(mainSource IN LISTS galleryMainSources)
  get_filename_component(entry "${mainSource}" DIRECTORY)
  get_filename_component(sampleId "${entry}" NAME)
  set(sampleDir "${CMAKE_CURRENT_SOURCE_DIR}/${entry}")
  string(REPLACE "-" "_" optionKey "${sampleId}")
  set(optionVariable "galleryOptions_${optionKey}")
  gpu_gallery_sample("${sampleDir}" ${${optionVariable}})
endforeach()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_METAL AND APPLE AND
   NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  include(cmake/apple_finalize.cmake)
endif()

if(GPU_BUILD_ANDROID_SAMPLES)
  include(cmake/android_finalize.cmake)
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_WEBGPU AND EMSCRIPTEN)
  include(cmake/webgpu.cmake)
endif()
