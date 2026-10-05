param(
  [int]$SoakCycles = 3,
  [switch]$SkipBuild,
  [switch]$SkipGpu
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$bin = Join-Path $root "build\bin"
$log = Join-Path $bin "overlay.log"
$script:results = @()
$script:failed = 0

function Write-Result([string]$name, [bool]$pass, [string]$detail = "") {
  $script:results += [pscustomobject]@{ Name = $name; Pass = $pass; Detail = $detail }
  if (-not $pass) { $script:failed++ }
  $status = if ($pass) { "PASS" } else { "FAIL" }
  $color = if ($pass) { "Green" } else { "Red" }
  Write-Host ("  [{0}] {1}{2}" -f $status, $name, $(if ($detail) { " - $detail" } else { "" })) -ForegroundColor $color
}

function Invoke-Step([string]$name, [scriptblock]$body) {
  Write-Host "== $name" -ForegroundColor Cyan
  try {
    $detail = & $body
    Write-Result $name ($LASTEXITCODE -eq 0 -or $null -eq $LASTEXITCODE) $detail
  } catch {
    Write-Result $name $false $_.Exception.Message
  }
}

function Stop-Targets {
  Get-Process test_target, sdk_consumer, external_host -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 400
}

function Reset-Log {
  Set-Content $log ""
}

function Wait-Log([string]$pattern, [int]$seconds, [string]$path = $log) {
  for ($i = 0; $i -lt ($seconds * 10); ++$i) {
    if (Test-Path $path) {
      if (Select-String -Path $path -Pattern $pattern -Quiet) { return $true }
    }
    Start-Sleep -Milliseconds 100
  }
  return $false
}

function Test-InjectedBackend([string]$name, [string[]]$arguments, [int]$waitSeconds, [string]$extraPattern = "") {
  Stop-Targets
  Reset-Log
  $out = & (Join-Path $bin "injector.exe") --no-wait --launch (Join-Path $bin "test_target.exe") @arguments 2>&1
  if ($LASTEXITCODE -ne 0) { throw "injector failed: $($out | Select-Object -Last 1)" }
  if (-not (Wait-Log "render ready" $waitSeconds)) { throw "render never became ready" }
  if ($extraPattern -and -not (Wait-Log $extraPattern $waitSeconds)) { throw "missing log: $extraPattern" }
  Start-Sleep -Seconds 1
  $alive = [bool](Get-Process test_target -ErrorAction SilentlyContinue)
  Stop-Targets
  if (-not $alive) { throw "target died" }
  return "ready and alive"
}

Push-Location $root
try {
  Write-Host "overlay ship check" -ForegroundColor White

  if (-not $SkipBuild) {
    Invoke-Step "build" {
      & cmake --build build 2>&1 | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
      return "cmake --build"
    }
  }

  Invoke-Step "unit tests" {
    $out = & ctest --test-dir build 2>&1
    $summary = ($out | Select-String "tests passed").Line
    if ($LASTEXITCODE -ne 0) { throw $summary }
    return $summary
  }

  Invoke-Step "fault containment" {
    Stop-Targets
    Reset-Log
    Start-Process -FilePath (Join-Path $bin "sdk_consumer.exe") -ArgumentList "--crash-painter" | Out-Null
    if (-not (Wait-Log "salvage: contained exception" 8)) { throw "no containment log" }
    if (-not (Wait-Log "render abandoned after fault" 4)) { throw "renderer not abandoned" }
    $alive = [bool](Get-Process sdk_consumer -ErrorAction SilentlyContinue)
    Stop-Targets
    if (-not $alive) { throw "process died" }
    return "access violation contained, process alive"
  }

  Invoke-Step "package" {
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\package.ps1") 2>&1
    if ($LASTEXITCODE -ne 0) { throw "package failed" }
    $zips = Get-ChildItem (Join-Path $root "build\dist") -Filter "*.zip"
    if ($zips.Count -lt 2) { throw "expected 2 zip artifacts" }
    return "$($zips.Count) artifacts"
  }

  if (-not $SkipGpu) {
    Invoke-Step "injected DX11" { return (Test-InjectedBackend "dx11" @() 10) }
    Invoke-Step "injected DX12" { return (Test-InjectedBackend "dx12" @("--dx12") 12) }
    Invoke-Step "injected Vulkan" { return (Test-InjectedBackend "vulkan" @("--vulkan") 12) }
    Invoke-Step "Vulkan device replacement" {
      Stop-Targets
      Reset-Log
      $out = & (Join-Path $bin "injector.exe") --no-wait --launch (Join-Path $bin "test_target.exe") --vulkan --vk-recreate 2>&1
      if ($LASTEXITCODE -ne 0) { throw "injector failed" }
      if (-not (Wait-Log "render ready" 12)) { throw "render never became ready" }
      Start-Sleep -Seconds 9
      $alive = [bool](Get-Process test_target -ErrorAction SilentlyContinue)
      $rebinds = (Select-String -Path $log -Pattern "device destroyed, renderer will rebind").Count
      Stop-Targets
      if (-not $alive) { throw "target died across recreation" }
      if ($rebinds -lt 2) { throw "expected 2 rebinds, saw $rebinds" }
      return "$rebinds rebinds survived"
    }
    Invoke-Step "menu releases the cursor" {
      Stop-Targets
      Reset-Log
      & (Join-Path $bin "injector.exe") --no-wait --launch (Join-Path $bin "test_target.exe") --cursor-lock 2>&1 | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "injector failed" }
      if (-not (Wait-Log "render ready" 12)) { throw "render never became ready" }
      Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class CursorProbe {
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern bool GetClipCursor(out RECT r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, UIntPtr e);
 [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@
      $proc = Get-Process test_target
      $h = $proc.MainWindowHandle
      $wr = New-Object CursorProbe+RECT
      [CursorProbe]::GetWindowRect($h, [ref]$wr) | Out-Null
      $center_x = ($wr.L + $wr.R) / 2
      $p = New-Object CursorProbe+POINT
      [CursorProbe]::SetCursorPos(200, 200) | Out-Null
      Start-Sleep -Milliseconds 400
      [CursorProbe]::GetCursorPos([ref]$p) | Out-Null
      if ([math]::Abs($p.X - $center_x) -gt 120) { throw "target is not locking the cursor" }
      [CursorProbe]::SetForegroundWindow($h) | Out-Null
      Start-Sleep -Milliseconds 300
      [CursorProbe]::keybd_event(0x2E, 0, 0, [UIntPtr]::Zero)
      Start-Sleep -Milliseconds 100
      [CursorProbe]::keybd_event(0x2E, 0, 2, [UIntPtr]::Zero)
      Start-Sleep -Milliseconds 500
      [CursorProbe]::SetCursorPos(200, 200) | Out-Null
      Start-Sleep -Milliseconds 500
      [CursorProbe]::GetCursorPos([ref]$p) | Out-Null
      $clip = New-Object CursorProbe+RECT
      [CursorProbe]::GetClipCursor([ref]$clip) | Out-Null
      Stop-Targets
      if ($p.X -ne 200 -or $p.Y -ne 200) { throw "cursor not released with the menu open ($($p.X),$($p.Y))" }
      if ($clip.L -ne 0 -or $clip.T -ne 0) { throw "cursor clip not lifted" }
      return "clip lifted, recentring blocked"
    }
    Invoke-Step "sdk consumer from zip" {
      $zip = Get-ChildItem (Join-Path $root "build\dist") -Filter "overlay-sdk-*.zip" | Select-Object -First 1
      if (-not $zip) { throw "sdk zip missing" }
      $extract = Join-Path $env:TEMP "overlay-sdk-check"
      Remove-Item $extract -Recurse -Force -ErrorAction SilentlyContinue
      Expand-Archive -Path $zip.FullName -DestinationPath $extract
      $consumerBuild = Join-Path $env:TEMP "overlay-consumer-build"
      Remove-Item $consumerBuild -Recurse -Force -ErrorAction SilentlyContinue
      & cmake -S (Join-Path $root "examples\sdk_consumer") -B $consumerBuild -G Ninja "-DOVERLAY_SDK_DIR=$extract" "-DCMAKE_BUILD_TYPE=Release" 2>&1 | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "configure against the sdk failed" }
      & cmake --build $consumerBuild 2>&1 | Out-Null
      if ($LASTEXITCODE -ne 0) { throw "consumer build failed" }
      Copy-Item (Join-Path $extract "bin\overlay.dll") $consumerBuild -Force
      Copy-Item (Join-Path $extract "bin\assets") $consumerBuild -Recurse -Force
      $consumerLog = Join-Path $consumerBuild "overlay.log"
      Set-Content $consumerLog ""
      Start-Process -FilePath (Join-Path $consumerBuild "sdk_consumer.exe") | Out-Null
      $ready = Wait-Log "render ready" 10 $consumerLog
      $alive = [bool](Get-Process sdk_consumer -ErrorAction SilentlyContinue)
      Stop-Targets
      if (-not $ready) { throw "consumer never rendered" }
      if (-not $alive) { throw "consumer died" }
      return "packaged sdk built and rendered"
    }
  } else {
    Write-Host "== GPU stages skipped" -ForegroundColor Yellow
  }

  Invoke-Step "soak" {
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "tools\soak\soak.ps1") -Cycles $SoakCycles 2>&1
    $summary = ($out | Select-String "soak:").Line
    if ($LASTEXITCODE -ne 0) { throw $summary }
    return $summary
  }

  Invoke-Step "C# binding" {
    if (-not (Get-Command dotnet -ErrorAction SilentlyContinue)) { return "dotnet not installed, skipped" }
    $out = & dotnet run --project (Join-Path $root "bindings\csharp\Example\Example.csproj") -c Release 2>&1
    $pass = $out | Select-String "PASS" -Quiet
    if ($LASTEXITCODE -ne 0 -or -not $pass) { throw "smoke test failed" }
    return "smoke test passed"
  }
} finally {
  Stop-Targets
  Pop-Location
}

Write-Host ""
Write-Host "summary" -ForegroundColor White
foreach ($r in $script:results) {
  $status = if ($r.Pass) { "PASS" } else { "FAIL" }
  Write-Host ("  {0}  {1}" -f $status, $r.Name)
}
if ($script:failed -eq 0) {
  Write-Host "ship check: all stages passed" -ForegroundColor Green
  exit 0
}
Write-Host "ship check: $script:failed stage(s) failed" -ForegroundColor Red
exit 1
