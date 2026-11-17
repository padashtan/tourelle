<#
.SYNOPSIS
  Switches the QuickCam Orbit/Sphere AF video function between Windows' own
  driver (usbvideo.sys) and WinUSB, which Tourelle needs for live pan/tilt.

.EXAMPLE
  .\tourelle-driver.ps1 -Bind     # video function -> WinUSB (Tourelle takes over)
  .\tourelle-driver.ps1 -Unbind   # back to the original Windows drivers
  .\tourelle-driver.ps1           # show the current state
#>
param([switch]$Bind, [switch]$Unbind, [string]$Log)

$HardwareId = 'USB\VID_046D&PID_0994&MI_00'
# Must match tourelle::kWinUsbInterfaceGuid in native/uvc/UvcDevice.h.
$InterfaceGuid = '{293D3D63-AF2C-42DA-84B9-B9DDC76A7AB7}'

function Get-VideoFunction {
    Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like "$HardwareId\*" } | Select-Object -First 1
}

$device = Get-VideoFunction
if (-not $device) { Write-Error "Camera not connected ($HardwareId)."; exit 1 }
$service = (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName DEVPKEY_Device_Service).Data
if (-not ($Bind -or $Unbind)) {
    "$($device.FriendlyName) [$($device.InstanceId)] driver: $service"
    exit 0
}

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    $log = Join-Path $env:TEMP 'tourelle-driver.log'
    $mode = if ($Bind) { '-Bind' } else { '-Unbind' }
    Start-Process powershell.exe -Verb RunAs -Wait -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" $mode -Log `"$log`""
    Get-Content $log
    exit 0
}

if ($Log) { Start-Transcript -Path $Log -Force | Out-Null }
try {
    $id = $device.InstanceId
    if ($Bind) {
        if ($service -eq 'WINUSB') { 'Already bound to WinUSB.' }
        else {
            & (Join-Path $PSScriptRoot 'bind-winusb.ps1') -InstanceId $id -Log (Join-Path $env:TEMP 'tourelle-bind.log')
            Get-Content (Join-Path $env:TEMP 'tourelle-bind.log') | Select-String 'INSTALL|ERROR|class' | ForEach-Object { $_.Line }
        }
        New-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Enum\$id\Device Parameters" -Name DeviceInterfaceGUIDs `
            -PropertyType MultiString -Value @($InterfaceGuid) -Force | Out-Null
        pnputil /restart-device "$id" | Out-Null
    }
    else {
        # Removing the device node drops the forced driver; the rescan reinstalls usbvideo.sys.
        pnputil /remove-device "$id" | Out-Null
        pnputil /scan-devices | Out-Null
        Start-Sleep -Seconds 3
    }
    $device = Get-VideoFunction
    "driver now: " + (Get-PnpDeviceProperty -InstanceId $device.InstanceId -KeyName DEVPKEY_Device_Service).Data
}
finally {
    if ($Log) { Stop-Transcript | Out-Null }
}
