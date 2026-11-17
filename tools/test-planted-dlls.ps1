<#
.SYNOPSIS
  Planted-DLL test (run by the CI): a garbage file named like a system DLL next to
  Tourelle.exe or tourelle-cli.exe must never be loaded at startup.
#>
param([Parameter(Mandatory)] [string]$BuildDir)
# Loader error popups are suppressed (the child processes inherit the error mode).
Add-Type @"
using System.Runtime.InteropServices;
public static class ErrMode { [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint m); }
"@
[ErrMode]::SetErrorMode(0x8003) | Out-Null  # SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX

$names = 'bcrypt','mfcore','devrtl','wofutil','mfplat','mf','mfreadwrite','mfsensorgroup','newdev','dwmapi','comctl32',
         'devobj','cfgmgr32','winusb','propsys','windows.storage','uxtheme','version','profapi','cryptbase','dbghelp',
         'wintrust','crypt32','msasn1','sspicli','userenv','powrprof','umpdc','kernel.appcore','textinputframework',
         'coremessaging','iertutil','srvcli','netutils','wtsapi32','winsta','dxgi','d3d11','xinput1_4','rtworkq',
         'ksuser','msvcp_win','ucrtbase','win32u','gdi32full','dwrite','windowscodecs','mfperfhelper','avrt','winmm'
$work = Join-Path $env:TEMP "tourelle-plant-$PID"
$results = foreach ($exe in 'Tourelle.exe', 'tourelle-cli.exe') {
    $expected = if ($exe -eq 'Tourelle.exe') { 0 } else { 2 }  # --self-test / usage
    $arguments = if ($exe -eq 'Tourelle.exe') { '--self-test' } else { '' }
    foreach ($name in @('(none)') + $names) {
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory $work | Out-Null
        Copy-Item (Join-Path $BuildDir $exe) $work
        if ($name -ne '(none)') { Set-Content (Join-Path $work "$name.dll") 'not a dll' }
        $p = if ($arguments) { Start-Process (Join-Path $work $exe) -ArgumentList $arguments -PassThru -Wait -WindowStyle Hidden }
             else { Start-Process (Join-Path $work $exe) -PassThru -Wait -WindowStyle Hidden }
        if ($p.ExitCode -ne $expected) { [pscustomobject]@{ Exe = $exe; Planted = "$name.dll"; Exit = ('0x{0:X8}' -f $p.ExitCode) } }
    }
}
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
if ($results) { "PLANTED DLL LOADED:"; $results | Format-Table -AutoSize | Out-String; exit 1 }
"no planted DLL loaded ($($names.Count) names x 2 executables)"

