<#
.SYNOPSIS
  Planted-DLL test (run by the CI): a garbage file named like a system DLL next to
  Tourelle.exe or tourelle-cli.exe must never be loaded at startup.
.NOTES
  Throws on failure, so a calling step fails even when the call is not its last command.
  Children are started without the shell (UseShellExecute = false) so that they inherit
  the error mode set below: a loader failure exits with a status code instead of showing
  a blocking "Bad Image" dialog. Each run is killed after 15 seconds.
#>
param([Parameter(Mandatory)] [string]$BuildDir)

Add-Type @"
using System.Runtime.InteropServices;
public static class ErrMode { [DllImport("kernel32.dll")] public static extern uint SetErrorMode(uint m); }
"@
[ErrMode]::SetErrorMode(0x8003) | Out-Null  # SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX

function Invoke-Isolated([string]$Exe, [string]$Arguments) {
    $info = [System.Diagnostics.ProcessStartInfo]::new($Exe, $Arguments)
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.WorkingDirectory = Split-Path $Exe
    $process = [System.Diagnostics.Process]::Start($info)
    $output = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit(15000)) {
        $process.Kill()
        return 'timeout'
    }
    [void]$output.Result
    [void]$errors.Result
    return $process.ExitCode
}

$names = 'bcrypt','mfcore','devrtl','wofutil','mfplat','mf','mfreadwrite','mfsensorgroup','newdev','dwmapi','comctl32',
         'devobj','cfgmgr32','winusb','propsys','windows.storage','uxtheme','version','profapi','cryptbase','dbghelp',
         'wintrust','crypt32','msasn1','sspicli','userenv','powrprof','umpdc','kernel.appcore','textinputframework',
         'coremessaging','iertutil','srvcli','netutils','wtsapi32','winsta','dxgi','d3d11','xinput1_4','rtworkq',
         'ksuser','msvcp_win','ucrtbase','win32u','gdi32full','dwrite','windowscodecs','mfperfhelper','avrt','winmm'
$work = Join-Path ([System.IO.Path]::GetTempPath()) "tourelle-plant-$PID"
$failures = foreach ($exe in 'Tourelle.exe', 'tourelle-cli.exe') {
    $expected = if ($exe -eq 'Tourelle.exe') { 0 } else { 2 }        # --self-test / usage
    $arguments = if ($exe -eq 'Tourelle.exe') { '--self-test' } else { '' }
    foreach ($name in @('(none)') + $names) {
        Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory $work | Out-Null
        Copy-Item (Join-Path $BuildDir $exe) $work
        if ($name -ne '(none)') { Set-Content (Join-Path $work "$name.dll") 'not a dll' }
        $exit = Invoke-Isolated (Join-Path $work $exe) $arguments
        if ($exit -ne $expected) {
            $shown = if ($exit -is [int]) { '0x{0:X8}' -f $exit } else { $exit }
            [pscustomobject]@{ Exe = $exe; Planted = "$name.dll"; Exit = $shown }
        }
    }
}
Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
if ($failures) { throw "Planted DLL loaded:`n$($failures | Format-Table -AutoSize | Out-String)" }
"no planted DLL loaded ($($names.Count) names x 2 executables)"
