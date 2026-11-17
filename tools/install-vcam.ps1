<#
.SYNOPSIS
  Installs (or removes) the Tourelle virtual camera media source.

  The DLL is copied to Program Files because the Windows Camera Frame Server
  service loads it and cannot read user profile folders. Its COM class is
  registered machine-wide for the same reason.

.EXAMPLE
  .\install-vcam.ps1 -Dll ..\native\build\TourelleCamera.dll
  .\install-vcam.ps1 -Uninstall
#>
param([string]$Dll, [switch]$Uninstall, [string]$Log)

# Release layout: tools\install-vcam.ps1 next to bin\TourelleCamera.dll.
if (-not $Dll -and -not $Uninstall) { $Dll = Join-Path $PSScriptRoot '..\bin\TourelleCamera.dll' }

$Clsid = '{7268DC8D-D8F5-47C9-B981-EC9E9199EAEE}'  # CLSID_TourelleCamera
$TargetDir = Join-Path $env:ProgramFiles 'Tourelle'
$Target = Join-Path $TargetDir 'TourelleCamera.dll'
$ClassKey = "HKLM:\SOFTWARE\Classes\CLSID\$Clsid"

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $logFile = Join-Path $env:TEMP 'tourelle-install-vcam.log'
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Log `"$logFile`""
    if ($Uninstall) { $arguments += ' -Uninstall' } else { $arguments += " -Dll `"$((Resolve-Path $Dll).Path)`"" }
    Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList $arguments
    Get-Content $logFile
    exit 0
}

if ($Log) { Start-Transcript -Path $Log -Force | Out-Null }
try {
    if ($Uninstall) {
        Remove-Item -Path $ClassKey -Recurse -Force -ErrorAction SilentlyContinue
        # The Frame Server may still hold the DLL; it goes away at the next reboot at worst.
        Remove-Item -Path $TargetDir -Recurse -Force -ErrorAction SilentlyContinue
        'virtual camera source removed'
    }
    else {
        New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
        try { Copy-Item -Path $Dll -Destination $Target -Force -ErrorAction Stop }
        catch {
            # In use by the Frame Server: restart it to release the old DLL, then retry.
            Restart-Service FrameServer -Force -ErrorAction SilentlyContinue
            Copy-Item -Path $Dll -Destination $Target -Force
        }
        New-Item -Path "$ClassKey\InprocServer32" -Force | Out-Null
        Set-ItemProperty -Path $ClassKey -Name '(default)' -Value 'Tourelle Camera Source'
        Set-ItemProperty -Path "$ClassKey\InprocServer32" -Name '(default)' -Value $Target
        Set-ItemProperty -Path "$ClassKey\InprocServer32" -Name 'ThreadingModel' -Value 'Both'
        "virtual camera source installed: $Target"
    }
}
finally {
    if ($Log) { Stop-Transcript | Out-Null }
}
