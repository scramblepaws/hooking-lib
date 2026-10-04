param(
  [string]$BuildDir = "build",
  [string]$OutDir = "build/dist"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$out = Join-Path $root $OutDir

$header = Get-Content (Join-Path $root "include\overlay\overlay_c.h") -Raw
$major = [regex]::Match($header, "#define OVERLAY_VERSION_MAJOR (\d+)").Groups[1].Value
$minor = [regex]::Match($header, "#define OVERLAY_VERSION_MINOR (\d+)").Groups[1].Value
$patch = [regex]::Match($header, "#define OVERLAY_VERSION_PATCH (\d+)").Groups[1].Value
$version = "$major.$minor.$patch"

Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $out | Out-Null

$sdkStage = Join-Path $build "stage\sdk"
Remove-Item $sdkStage -Recurse -Force -ErrorAction SilentlyContinue
cmake --install $build --prefix $sdkStage --config Release
Copy-Item (Join-Path $root "NOTICE.md") $sdkStage
Compress-Archive -Path (Join-Path $sdkStage "*") -DestinationPath (Join-Path $out "overlay-sdk-$version-win64.zip")

$runnerStage = Join-Path $build "stage\runner"
Remove-Item $runnerStage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $runnerStage | Out-Null
Copy-Item (Join-Path $build "bin\overlay.dll") $runnerStage
Copy-Item (Join-Path $build "bin\injector.exe") $runnerStage
Copy-Item (Join-Path $build "bin\assets") $runnerStage -Recurse
Copy-Item (Join-Path $build "bin\scripts") $runnerStage -Recurse
Copy-Item (Join-Path $root "NOTICE.md") $runnerStage
Compress-Archive -Path (Join-Path $runnerStage "*") -DestinationPath (Join-Path $out "overlay-runner-$version-win64.zip")

Write-Host "packaged overlay ${version}:"
Get-ChildItem $out | ForEach-Object { Write-Host "  $($_.Name)  ($([math]::Round($_.Length/1KB)) KB)" }
