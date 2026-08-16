param(
  [string]$BuildDir = "",
  [string]$Target = "gpu-api-test",
  [string]$TestRegex = "^(api-validation|dx12-)",
  [ValidateSet("auto", "low", "high")]
  [string]$Adapter = "auto",
  [switch]$Test
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) {
  $BuildDir = Join-Path $root "out\build\windows"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} `
  "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
  throw "Visual Studio Installer's vswhere.exe was not found"
}

$vsRoot = & $vswhere -latest -prerelease -products * `
  -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
  -property installationPath
if (-not $vsRoot) {
  throw "A Visual Studio installation with the C++ toolchain was not found"
}

$vsDevCmd = Join-Path $vsRoot "Common7\Tools\VsDevCmd.bat"
$cmake = Join-Path $vsRoot `
  "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ctest = Join-Path $vsRoot `
  "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
if (-not (Test-Path $vsDevCmd) -or -not (Test-Path $cmake)) {
  throw "Visual Studio CMake or developer environment is incomplete"
}

$envScript = Join-Path $env:TEMP "gpu-vs-env-$PID.cmd"
@"
@echo off
call "$vsDevCmd" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%
set
"@ | Set-Content -Path $envScript -Encoding Ascii

try {
  $environment = & cmd.exe /d /c $envScript
  if ($LASTEXITCODE -ne 0) {
    throw "VsDevCmd.bat failed with exit code $LASTEXITCODE"
  }
  foreach ($line in $environment) {
    $separator = $line.IndexOf("=")
    if ($separator -gt 0) {
      $name = $line.Substring(0, $separator)
      $value = $line.Substring($separator + 1)
      Set-Item -Path "Env:$name" -Value $value
    }
  }
} finally {
  Remove-Item $envScript -ErrorAction SilentlyContinue
}

if (-not (Test-Path (Join-Path $BuildDir "CMakeCache.txt"))) {
  & $cmake -S $root -B $BuildDir -G Ninja `
    -DGPU_BUILD_DX12=ON `
    -DGPU_BUILD_TESTS=ON `
    -DGPU_BUILD_SAMPLES=OFF `
    -DGPU_BUILD_BENCHMARKS=OFF
  if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
  }
}

& $cmake --build $BuildDir --target $Target --parallel
if ($LASTEXITCODE -ne 0 -or -not $Test) {
  exit $LASTEXITCODE
}

$env:GPU_TEST_ADAPTER = $Adapter
& $ctest --test-dir $BuildDir -R $TestRegex --output-on-failure
exit $LASTEXITCODE
