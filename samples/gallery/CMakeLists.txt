set(GPU_ASSETKIT_ROOT
    "${PROJECT_SOURCE_DIR}/../assetio"
    CACHE PATH "AssetKit checkout used by gallery asset samples")

if(GPU_BUILD_ANDROID_SAMPLES)
  include(cmake/android.cmake)
endif()

function(gpu_gallery_sample sampleDir)
  set(options ANDROID_ASSETKIT USES_STDLIB)
  set(multiValueArgs CAPS)
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
    if(GPU_SAMPLE_ANDROID_ASSETKIT)
      set_property(GLOBAL PROPERTY GPU_ANDROID_GALLERY_USES_ASSETKIT TRUE)
    endif()

    foreach(shaderSource IN LISTS shaderSources)
      get_filename_component(shaderStem "${shaderSource}" NAME_WE)
      set(shaderCaps)
      foreach(capability IN LISTS GPU_SAMPLE_CAPS)
        string(REPLACE "=" ";" capabilityPair "${capability}")
        list(GET capabilityPair 0 capabilityStem)
        if(capabilityStem STREQUAL shaderStem)
          list(GET capabilityPair 1 shaderCaps)
        endif()
      endforeach()

      set(shaderArgs)
      if(GPU_SAMPLE_USES_STDLIB)
        list(APPEND shaderArgs USES_STDLIB)
      endif()
      if(shaderCaps)
        list(APPEND shaderArgs CAPS "${shaderCaps}")
      endif()

      gpu_android_gallery_shader(
        "${sampleId}-${shaderStem}"
        "${shaderSource}"
        ${shaderArgs}
      )
    endforeach()
  endif()
endfunction()

set(galleryOptions_assetkit_damaged_helmet
  ANDROID_ASSETKIT
  USES_STDLIB
)
set(galleryOptions_descriptor_array
  CAPS descriptor_array=descriptor_indexing
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

file(GLOB galleryEntries
  LIST_DIRECTORIES true
  CONFIGURE_DEPENDS
  RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/*"
)
list(SORT galleryEntries)

foreach(entry IN LISTS galleryEntries)
  set(sampleDir "${CMAKE_CURRENT_SOURCE_DIR}/${entry}")
  if(IS_DIRECTORY "${sampleDir}" AND EXISTS "${sampleDir}/main.c")
    string(REPLACE "-" "_" optionKey "${entry}")
    set(optionVariable "galleryOptions_${optionKey}")
    gpu_gallery_sample("${sampleDir}" ${${optionVariable}})
  endif()
endforeach()

if(GPU_BUILD_ANDROID_SAMPLES)
  include(cmake/android_finalize.cmake)
endif()

if(GPU_BUILD_SAMPLES AND GPU_BUILD_WEBGPU AND EMSCRIPTEN)
  include(cmake/webgpu.cmake)
endif()
