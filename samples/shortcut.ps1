param(
  [string]$BuildDir = "$PSScriptRoot\..\out\build\samples-windows",
  [string]$Config = "Release",
  [string]$Desktop = [Environment]::GetFolderPath("Desktop")
)

$ErrorActionPreference = "Stop"

$runtimeDir = Join-Path $BuildDir "samples\windows\bin"
$gallery = Join-Path $runtimeDir "$Config\GPU + USL Samples.exe"
if (-not (Test-Path -LiteralPath $gallery)) {
  $gallery = Join-Path $runtimeDir "GPU + USL Samples.exe"
}
if (-not (Test-Path -LiteralPath $gallery)) {
  throw "samples: build windows first: $runtimeDir"
}

$shortcutPath = Join-Path $desktop "GPU + USL Samples.lnk"
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = $gallery
$shortcut.WorkingDirectory = Split-Path -Parent $gallery
$shortcut.Description = "GPU + USL Direct3D 12 samples"
$shortcut.Save()

Write-Host "samples: installed windows desktop shortcut"
