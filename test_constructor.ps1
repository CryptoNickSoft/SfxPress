$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $here 'build\SfxPress.exe'
$out = Join-Path $here 'build\demo.exe'
$payload = Join-Path $here 'demo_payload'
$inst = Join-Path $env:TEMP 'app_demo'
$marker = Join-Path $env:TEMP 'sfxctor_demo_marker.txt'

$script:pass = 0
$script:fail = 0
function Check($name, $cond, $detail) {
    if ($cond) { $script:pass++; Write-Output ("  PASS  " + $name + $(if ($detail) { " — $detail" })) }
    else { $script:fail++; Write-Output ("  FAIL  " + $name + $(if ($detail) { " — $detail" })) }
}

Get-Process SfxPress -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400
Remove-Item -LiteralPath $out, $marker -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $inst) { Remove-Item -LiteralPath $inst -Recurse -Force }

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class SfxTest {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr Find(uint target) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, l) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == target) {
        var c = new StringBuilder(128); GetClassName(h, c, 128);
        if (c.ToString() == "SfxPressMain") { found = h; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public static string Title(IntPtr h) {
    var sb = new StringBuilder(512); GetWindowText(h, sb, 512); return sb.ToString();
  }
}
"@

Write-Output "=== 1. сборка без интерфейса (--build)"
$args = @(
    '--build',
    "--payload=$payload",
    "--out=$out",
    "--install=%TEMP%\app_demo",
    "--run=run.cmd"
)
$proc = Start-Process -FilePath $exe -ArgumentList $args -PassThru -Wait
Check 'код возврата 0' ($proc.ExitCode -eq 0) ("код=" + $proc.ExitCode)
$made = Test-Path -LiteralPath $out
Check 'инсталлятор создан' $made
if ($made) {
    $size = (Get-Item -LiteralPath $out).Length
    Check 'размер правдоподобен' ($size -gt 250000) ("{0} байт" -f $size)
}

Write-Output "=== 2. проверка сигнатуры архива"
if ($made) {
    $bytes = [System.IO.File]::ReadAllBytes($out)
    $sig = @(0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C)
    $limit = [Math]::Min($bytes.Length, 1048576)
    $found = -1
    for ($i = 0; $i -lt $limit - 6; $i++) {
        if ($bytes[$i] -eq $sig[0] -and $bytes[$i + 1] -eq $sig[1] -and $bytes[$i + 2] -eq $sig[2] -and
            $bytes[$i + 3] -eq $sig[3] -and $bytes[$i + 4] -eq $sig[4] -and $bytes[$i + 5] -eq $sig[5]) { $found = $i; break }
    }
    Check 'сигнатура 7z в первых 1 МБ' ($found -ge 0) ("смещение " + $found)
}

Write-Output "=== 3. запуск инсталлятора"
if ($made) {
    $p2 = Start-Process -FilePath $out -PassThru
    $done = $p2.WaitForExit(60000)
    Check 'инсталлятор завершился' $done ("код=" + $p2.ExitCode)
    Start-Sleep -Milliseconds 800
    Check 'маркер создан' (Test-Path -LiteralPath $marker)
    if (Test-Path -LiteralPath $marker) {
        $text = (Get-Content -LiteralPath $marker -Raw).Trim()
        Write-Output ("    маркер: " + $text)
        Check 'папка распаковки удалена (Delete=%T)' (-not (Test-Path -LiteralPath $inst))
    }
}

Write-Output "=== 4. запуск интерфейса"
$gui = Start-Process -FilePath $exe -PassThru
$h = [IntPtr]::Zero
for ($i = 0; $i -lt 40 -and $h -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 200
    $h = [SfxTest]::Find([uint32]$gui.Id)
}
Check 'окно конструктора найдено' ($h -ne [IntPtr]::Zero)
if ($h -ne [IntPtr]::Zero) {
    $title = [SfxTest]::Title($h)
    Check 'заголовок окна' ($title -like '*SfxPress*') $title
    [void][SfxTest]::PostMessage($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
}
if (-not $gui.HasExited) { $gui.Kill() }

Write-Output ""
Write-Output ("ИТОГО: PASS={0}  FAIL={1}" -f $script:pass, $script:fail)
if ($script:fail -gt 0) { exit 1 }
