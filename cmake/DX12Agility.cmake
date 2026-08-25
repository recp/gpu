include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/WindowsTarget.cmake)

set(GPU_DX12_AGILITY_CHANNEL "retail" CACHE STRING
    "DirectX 12 Agility SDK channel: retail or preview")
set_property(CACHE GPU_DX12_AGILITY_CHANNEL PROPERTY STRINGS retail preview)

if(GPU_DX12_AGILITY_CHANNEL STREQUAL "retail")
  set(gpuDx12AgilityVersion "1.619.5")
  set(gpuDx12AgilitySha256
      "0e9bcf32aac9a79343ede9b21e4864950ee54577e3d8e19bfcdf002bb4e9bfd6")
  set(gpuDx12AgilityPreview 0)
elseif(GPU_DX12_AGILITY_CHANNEL STREQUAL "preview")
  set(gpuDx12AgilityVersion "1.721.3-preview")
  set(gpuDx12AgilitySha256
      "0131bce1e4bace3fc08c03018c29a09ede2570b263721c6449b0ec75762ab22d")
  set(gpuDx12AgilityPreview 1)
else()
  message(FATAL_ERROR
    "GPU_DX12_AGILITY_CHANNEL must be retail or preview")
endif()

set(GPU_DX12_AGILITY_SDK_ROOT "" CACHE PATH
    "Extracted Microsoft.Direct3D.D3D12 NuGet package")

function(gpu_enable_dx12_agility target)
  if(NOT WIN32)
    return()
  endif()

  if(GPU_DX12_AGILITY_SDK_ROOT)
    set(agilityRoot "${GPU_DX12_AGILITY_SDK_ROOT}")
  else()
    string(TOLOWER "${gpuDx12AgilityVersion}" agilityVersion)
    set(agilityPackage
        "microsoft.direct3d.d3d12.${agilityVersion}.nupkg")
    set(agilityUrl
        "https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/${agilityVersion}/${agilityPackage}")
    FetchContent_Declare(gpu_dx12_agility
      URL "${agilityUrl}"
      URL_HASH "SHA256=${gpuDx12AgilitySha256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(gpu_dx12_agility)
    set(agilityRoot "${gpu_dx12_agility_SOURCE_DIR}")
  endif()

  set(agilityInclude "${agilityRoot}/build/native/include")
  if(NOT EXISTS "${agilityInclude}/d3d12.h")
    message(FATAL_ERROR
      "Invalid Agility SDK package root: ${agilityRoot}")
  endif()

  gpu_windows_target_arch(agilityPlatform)
  if(agilityPlatform STREQUAL "x86")
    set(agilityPlatform win32)
  endif()

  set(agilityRuntime "${agilityRoot}/build/native/bin/${agilityPlatform}")
  set(agilityCore "${agilityRuntime}/D3D12Core.dll")
  set(agilityLayers "${agilityRuntime}/d3d12SDKLayers.dll")
  if(NOT EXISTS "${agilityCore}")
    message(FATAL_ERROR
      "Agility SDK does not contain ${agilityPlatform}/D3D12Core.dll")
  endif()
  if(GPU_BUILD_WITH_VALIDATION AND NOT EXISTS "${agilityLayers}")
    message(FATAL_ERROR
      "Agility SDK does not contain ${agilityPlatform}/d3d12SDKLayers.dll")
  endif()

  target_include_directories(${target} BEFORE PRIVATE "${agilityInclude}")
  target_compile_definitions(${target} PRIVATE
    GPU_DX12_AGILITY_SDK_PREVIEW=${gpuDx12AgilityPreview}
  )
  if(GPU_DX12_AGILITY_CHANNEL STREQUAL "preview")
    message(STATUS
      "DirectX 12 Agility SDK ${gpuDx12AgilityVersion} requires Windows Developer Mode")
  endif()

  set(GPU_DX12_AGILITY_CORE "${agilityCore}" CACHE INTERNAL "" FORCE)
  set(GPU_DX12_AGILITY_LAYERS "${agilityLayers}" CACHE INTERNAL "" FORCE)
  set(GPU_DX12_AGILITY_LICENSE "${agilityRoot}/LICENSE.txt"
      CACHE INTERNAL "" FORCE)

  gpu_stage_dx12_agility(${target})
endfunction()

function(gpu_dx12_agility_stage_commands outVar directory)
  if(NOT WIN32 OR NOT GPU_DX12_AGILITY_CORE)
    set(${outVar} "" PARENT_SCOPE)
    return()
  endif()

  set(commands
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${directory}/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_AGILITY_CORE}"
            "${directory}/D3D12/D3D12Core.dll"
  )
  if(GPU_BUILD_WITH_VALIDATION)
    list(APPEND commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${GPU_DX12_AGILITY_LAYERS}"
              "${directory}/D3D12/d3d12SDKLayers.dll"
    )
  endif()
  set(${outVar} ${commands} PARENT_SCOPE)
endfunction()

function(gpu_stage_dx12_agility_to_directory target directory)
  gpu_dx12_agility_stage_commands(commands "${directory}")
  if(NOT commands)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    ${commands}
    VERBATIM
  )
endfunction()

function(gpu_stage_dx12_agility target)
  gpu_stage_dx12_agility_to_directory(
    ${target}
    "$<TARGET_FILE_DIR:${target}>"
  )
endfunction()
