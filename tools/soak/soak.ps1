param(
  [int]$Cycles = 3,
  [string]$Bin = (Join-Path $PSScriptRoot "..\..\build\bin")
)

$ErrorActionPreference = "Stop"
Add-Type -TypeDefinition @"
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class Soak {
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int ht, uint flags);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, UIntPtr e);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 public static Bitmap Capture(RECT r) { var b = new Bitmap(r.R-r.L, r.B-r.T); using (var g = Graphics.FromImage(b)) g.CopyFromScreen(r.L, r.T, 0, 0, b.Size); return b; }
 public static bool MenuOpen(RECT r) { using (var b = Capture(r)) { if (b.Width < 120 || b.Height < 120) return false; var p = b.GetPixel(100,100); return Math.Abs(p.R-19) < 6 && Math.Abs(p.G-19) < 6 && Math.Abs(p.B-21) < 6; } }
 public static void Tap(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(120); keybd_event(vk, 0, 2, UIntPtr.Zero); System.Threading.Thread.Sleep(150); }
 public static void Click(RECT r, int x, int y) { SetCursorPos(r.L + x, r.T + y); System.Threading.Thread.Sleep(200); mouse_event(2,0,0,0,UIntPtr.Zero); System.Threading.Thread.Sleep(60); mouse_event(4,0,0,0,UIntPtr.Zero); System.Threading.Thread.Sleep(300); }
 public static void Drag(RECT r, int x1, int y1, int x2, int y2) { SetCursorPos(r.L + x1, r.T + y1); System.Threading.Thread.Sleep(150); mouse_event(2,0,0,0,UIntPtr.Zero); for (int i = 1; i <= 10; i++) { SetCursorPos(r.L + x1 + (x2-x1)*i/10, r.T + y1 + (y2-y1)*i/10); System.Threading.Thread.Sleep(30); } mouse_event(4,0,0,0,UIntPtr.Zero); System.Threading.Thread.Sleep(250); }
}
"@ -ReferencedAssemblies System.Drawing

$target = Join-Path $Bin "test_target.exe"
$injector = Join-Path $Bin "injector.exe"
$dll = Join-Path $Bin "overlay.dll"
$logPath = Join-Path $Bin "overlay.log"

function Read-LogText {
  if (-not (Test-Path $logPath)) { return "" }
  $stream = [System.IO.File]::Open($logPath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
  try {
    $reader = New-Object System.IO.StreamReader($stream)
    return $reader.ReadToEnd()
  } finally {
    $stream.Dispose()
  }
}

function Read-NewLog([long]$offset) {
  $text = Read-LogText
  if ($text.Length -le $offset) { return "" }
  return $text.Substring([int]$offset)
}

function Wait-Log([long]$offset, [string]$pattern, [int]$timeoutMs) {
  $deadline = (Get-Date).AddMilliseconds($timeoutMs)
  while ((Get-Date) -lt $deadline) {
    if ((Read-NewLog $offset) -match $pattern) { return $true }
    Start-Sleep -Milliseconds 200
  }
  return $false
}

function Get-LogLength {
  return (Read-LogText).Length
}

$failed = 0
for ($cycle = 1; $cycle -le $Cycles; $cycle++) {
  Write-Host "--- cycle $cycle/$Cycles ---"
  $offset = Get-LogLength
  $proc = Start-Process -FilePath $target -PassThru
  $proc.WaitForInputIdle(4000) | Out-Null
  Start-Sleep -Seconds 2
  & $injector --no-wait "$($proc.Id)" $dll | Out-Null
  if (-not (Wait-Log $offset "render ready" 10000)) { Write-Host "FAIL: render never became ready"; $failed++; Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue; continue }

  $rect = New-Object Soak+RECT
  [Soak]::GetWindowRect($proc.MainWindowHandle, [ref]$rect) | Out-Null

  [Soak]::Tap(0x2E)   # Del opens menu
  Start-Sleep -Milliseconds 600
  if (-not [Soak]::MenuOpen($rect)) { Write-Host "FAIL: menu did not open"; $failed++; Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue; continue }

  [Soak]::Click($rect, 775, 184)      # toggle
  [Soak]::Click($rect, 1135, 178)     # open dropdown
  [Soak]::Click($rect, 1100, 225)     # pick item
  [Soak]::Drag($rect, 950, 90, 900, 140)  # drag the menu

  [Soak]::Tap(0x2E); Start-Sleep -Milliseconds 400
  [Soak]::Tap(0x2E); Start-Sleep -Milliseconds 400

  [Soak]::SetWindowPos($proc.MainWindowHandle, [IntPtr]::Zero, 0, 0, 1000, 620, 0x16) | Out-Null
  Start-Sleep -Milliseconds 800
  [Soak]::SetWindowPos($proc.MainWindowHandle, [IntPtr]::Zero, 0, 0, 1280, 760, 0x16) | Out-Null
  Start-Sleep -Milliseconds 800

  [Soak]::Tap(0x23)   # End ejects
  Start-Sleep -Seconds 2

  $alive = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
  $newLog = Read-NewLog $offset
  $drained = $newLog -match "eject: drained"
  $unloaded = $newLog -match "eject: unloading module"
  $deviceIssue = $newLog -match "device removed/reset"
  $faulted = $newLog -match "faulted"

  $ok = $alive -and $drained -and $unloaded -and (-not $faulted)
  if (-not $alive) { Write-Host "FAIL: target died" }
  if (-not $drained) { Write-Host "FAIL: eject never drained" }
  if (-not $unloaded) { Write-Host "FAIL: module never unloaded" }
  if ($deviceIssue) { Write-Host "WARN: device removed/reset seen" }
  if ($faulted) { Write-Host "FAIL: UI fault logged" }
  if ($ok) { Write-Host "cycle $cycle PASS" } else { $failed++ }

  Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
  Start-Sleep -Seconds 1
}

Write-Host ""
if ($failed -eq 0) { Write-Host "soak: $Cycles/$Cycles cycles passed"; exit 0 }
Write-Host "soak: $failed/$Cycles cycles failed"; exit 1
