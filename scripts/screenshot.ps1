# Screenshot a top-level window by process name. Usage:
#   powershell -ExecutionPolicy Bypass -File screenshot.ps1 -ProcessName ViPER4Host -OutFile ui.png
param(
  [string]$ProcessName = "ViPER4Host",
  [string]$OutFile = "ui.png",
  [string]$Resize = "",  # e.g. "1400x1900" physical pixels, applied before capture
  [int]$ScrollDown = 0,  # mouse-wheel clicks to send over the window before capture
  [string]$Click = ""    # "x,y" or "x1,y1;x2,y2" window-relative physical px, clicked before capture
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int ht, bool repaint);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, int data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, UIntPtr wp, IntPtr lp);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X; public int Y; }
}
"@
[Win]::SetProcessDPIAware() | Out-Null

$deadline = (Get-Date).AddSeconds(30)
$h = [IntPtr]::Zero
while ((Get-Date) -lt $deadline) {
  $p = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($p) { $h = $p.MainWindowHandle; break }
  Start-Sleep -Milliseconds 300
}
if ($h -eq [IntPtr]::Zero) { Write-Error "window not found for process '$ProcessName'"; exit 1 }

[Win]::SetForegroundWindow($h) | Out-Null
if ($Resize -match '^(\d+)x(\d+)$') {
  [Win]::MoveWindow($h, 0, 0, [int]$Matches[1], [int]$Matches[2], $true) | Out-Null
  Start-Sleep -Milliseconds 800
}
Start-Sleep -Milliseconds 600

$r = New-Object Win+RECT
[Win]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top

if ($ScrollDown -gt 0) {
  # Post wheel messages straight to the window (synthetic global wheel events
  # are ignored for background windows).
  $cx = [int](($r.Left + $r.Right) / 2)
  $cy = [int](($r.Top + $r.Bottom) / 2)
  $lp = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))
  $wp = [UIntPtr][uint64]4286578688  # HIWORD = wheel delta -120
  for ($i = 0; $i -lt $ScrollDown; $i++) {
    [Win]::PostMessage($h, 0x020A, $wp, $lp) | Out-Null
    Start-Sleep -Milliseconds 60
  }
  Start-Sleep -Milliseconds 800
}
if ($Click -ne "") {
  # Post clicks straight to the window (safe for a background/occluded window —
  # real input injection would hit whatever is on top). Caveat: JUCE ignores
  # synthetic clicks while the window is not foreground; bring it to front or
  # click manually if shots don't reflect the clicks.
  # Coords are window-relative physical px, i.e. the same space as the capture.
  $origin = New-Object Win+POINT
  [Win]::ClientToScreen($h, [ref]$origin) | Out-Null
  $offX = $r.Left - $origin.X
  $offY = $r.Top - $origin.Y
  foreach ($pt in $Click.Split(';')) {
    if ($pt -notmatch '^(\d+),(\d+)$') { Write-Error "bad -Click point '$pt'"; exit 1 }
    $cx = [int]$Matches[1] + $offX
    $cy = [int]$Matches[2] + $offY
    $lp = [IntPtr](($cy -shl 16) -bor ($cx -band 0xFFFF))
    [Win]::PostMessage($h, 0x0200, [UIntPtr]::Zero, $lp) | Out-Null  # move
    Start-Sleep -Milliseconds 150
    [Win]::PostMessage($h, 0x0201, [UIntPtr][uint64]1, $lp) | Out-Null  # down
    Start-Sleep -Milliseconds 100
    [Win]::PostMessage($h, 0x0202, [UIntPtr]::Zero, $lp) | Out-Null  # up
    Start-Sleep -Milliseconds 500
  }
  Start-Sleep -Milliseconds 500
}
if ($w -le 0 -or $ht -le 0) { Write-Error "bad window rect"; exit 1 }

$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
# PW_RENDERFULLCONTENT: captures the window itself even when occluded.
$hdc = $g.GetHdc()
$ok = [Win]::PrintWindow($h, $hdc, 2)
$g.ReleaseHdc($hdc)
if (-not $ok) { $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size) }
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "saved $OutFile ($w x $ht)"
