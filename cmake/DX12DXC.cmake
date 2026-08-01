include(FetchContent)

set(GPU_DX12_DXC_VERSION "1.10.2605.24-preview" CACHE STRING
    "DirectX Shader Compiler NuGet package version")
set(GPU_DX12_DXC_SHA256
    "64d453416bb4771e36a217e37049d1b809354316337d5eddb04056ff205bdd85"
    CACHE STRING "SHA-256 of the selected DXC NuGet package")
set(GPU_DX12_DXC_ROOT "" CACHE PATH
    "Extracted Microsoft.Direct3D.DXC NuGet package")

function(gpu_enable_dx12_dxc target)
  if(NOT WIN32)
    return()
  endif()

  if(GPU_DX12_DXC_ROOT)
    set(dxcRoot "${GPU_DX12_DXC_ROOT}")
  else()
    string(TOLOWER "${GPU_DX12_DXC_VERSION}" dxcVersion)
    set(dxcPackage "microsoft.direct3d.dxc.${dxcVersion}.nupkg")
    set(dxcUrl
        "https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.dxc/${dxcVersion}/${dxcPackage}")
    FetchContent_Declare(gpu_dx12_dxc
      URL "${dxcUrl}"
      URL_HASH "SHA256=${GPU_DX12_DXC_SHA256}"
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(gpu_dx12_dxc)
    set(dxcRoot "${gpu_dx12_dxc_SOURCE_DIR}")
  endif()

  string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" dxcPlatform)
  if(NOT dxcPlatform)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" dxcPlatform)
  endif()
  if(dxcPlatform MATCHES "arm64|aarch64")
    set(dxcPlatform arm64)
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(dxcPlatform x86)
  else()
    set(dxcPlatform x64)
  endif()

  set(dxcInclude "${dxcRoot}/build/native/include")
  set(dxcRuntime "${dxcRoot}/build/native/bin/${dxcPlatform}")
  set(dxcCompiler "${dxcRuntime}/dxcompiler.dll")
  set(dxcValidator "${dxcRuntime}/dxil.dll")
  set(dxcHlslInclude "${dxcInclude}/hlsl")
  if(NOT EXISTS "${dxcCompiler}" OR
     NOT EXISTS "${dxcValidator}" OR
     NOT EXISTS "${dxcHlslInclude}/dx/linalg.h")
    message(FATAL_ERROR
      "Invalid DirectX Shader Compiler package root: ${dxcRoot}")
  endif()

  target_include_directories(${target} BEFORE PRIVATE "${dxcInclude}")
  set(GPU_DX12_DXC_COMPILER "${dxcCompiler}" CACHE INTERNAL "" FORCE)
  set(GPU_DX12_DXC_VALIDATOR "${dxcValidator}" CACHE INTERNAL "" FORCE)
  set(GPU_DX12_DXC_HLSL_INCLUDE "${dxcHlslInclude}"
      CACHE INTERNAL "" FORCE)
  set(GPU_DX12_DXC_LICENSES
      "${dxcRoot}/LICENSE-MS.txt;${dxcRoot}/LICENSE-LLVM.txt;${dxcRoot}/LICENCE-MIT.txt"
      CACHE INTERNAL "" FORCE)

  gpu_stage_dx12_dxc(${target})
endfunction()

function(gpu_dx12_dxc_stage_commands outVar directory)
  if(NOT WIN32 OR NOT GPU_DX12_DXC_COMPILER)
    set(${outVar} "" PARENT_SCOPE)
    return()
  endif()

  set(commands
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_DXC_COMPILER}"
            "${directory}/dxcompiler.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${GPU_DX12_DXC_VALIDATOR}"
            "${directory}/dxil.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${directory}/include/hlsl"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${GPU_DX12_DXC_HLSL_INCLUDE}"
            "${directory}/include/hlsl"
  )
  set(${outVar} ${commands} PARENT_SCOPE)
endfunction()

function(gpu_stage_dx12_dxc_to_directory target directory)
  gpu_dx12_dxc_stage_commands(commands "${directory}")
  if(NOT commands)
    return()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    ${commands}
    VERBATIM
  )
endfunction()

function(gpu_stage_dx12_dxc target)
  gpu_stage_dx12_dxc_to_directory(
    ${target}
    "$<TARGET_FILE_DIR:${target}>"
  )
endfunction()
