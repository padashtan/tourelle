param([string]$InstanceId, [string]$Log)
$ErrorActionPreference = 'Stop'
Start-Transcript -Path $Log -Force | Out-Null
try {
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.ComponentModel;

public static class ForceWinUsb {
  [StructLayout(LayoutKind.Sequential)]
  public struct SP_DEVINFO_DATA { public uint cbSize; public Guid ClassGuid; public uint DevInst; public IntPtr Reserved; }

  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct SP_DEVINSTALL_PARAMS {
    public uint cbSize; public uint Flags; public uint FlagsEx; public IntPtr hwndParent;
    public IntPtr InstallMsgHandler; public IntPtr InstallMsgHandlerContext; public IntPtr FileQueue;
    public UIntPtr ClassInstallReserved; public uint Reserved;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string DriverPath;
  }

  [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
  public struct SP_DRVINFO_DATA {
    public uint cbSize; public uint DriverType; public UIntPtr Reserved;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string Description;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string MfgName;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string ProviderName;
    public long DriverDate; public ulong DriverVersion;
  }

  [DllImport("setupapi.dll", SetLastError = true)] static extern IntPtr SetupDiCreateDeviceInfoList(IntPtr classGuid, IntPtr hwnd);
  [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetupDiOpenDeviceInfo(IntPtr set, string id, IntPtr hwnd, uint flags, ref SP_DEVINFO_DATA data);
  [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetupDiSetDeviceRegistryProperty(IntPtr set, ref SP_DEVINFO_DATA data, uint prop, byte[] buf, uint size);
  [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetupDiGetDeviceInstallParams(IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DEVINSTALL_PARAMS p);
  [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetupDiSetDeviceInstallParams(IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DEVINSTALL_PARAMS p);
  [DllImport("setupapi.dll", SetLastError = true)] static extern bool SetupDiBuildDriverInfoList(IntPtr set, ref SP_DEVINFO_DATA data, uint type);
  [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetupDiEnumDriverInfo(IntPtr set, ref SP_DEVINFO_DATA data, uint type, uint index, ref SP_DRVINFO_DATA drv);
  [DllImport("setupapi.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool SetupDiSetSelectedDriver(IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DRVINFO_DATA drv);
  [DllImport("setupapi.dll", SetLastError = true)] static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);
  [DllImport("newdev.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern bool DiInstallDevice(IntPtr hwnd, IntPtr set, ref SP_DEVINFO_DATA data, ref SP_DRVINFO_DATA drv, uint flags, out bool reboot);

  const uint SPDRP_CLASSGUID = 8;
  const uint DI_ENUMSINGLEINF = 0x00010000;
  const uint DI_FLAGSEX_ALLOWEXCLUDEDDRVS = 0x00000800;
  const uint SPDIT_CLASSDRIVER = 1;
  public const string UsbDeviceClass = "{88BAE032-5A81-49F0-BC3D-A4FF138216D6}";

  static void Check(bool ok, string step) { if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error(), step + " failed"); }

  public static void SetClass(string instanceId, string classGuid) {
    IntPtr set = SetupDiCreateDeviceInfoList(IntPtr.Zero, IntPtr.Zero);
    try {
      var d = new SP_DEVINFO_DATA(); d.cbSize = (uint)Marshal.SizeOf(d);
      Check(SetupDiOpenDeviceInfo(set, instanceId, IntPtr.Zero, 0, ref d), "OpenDeviceInfo");
      var buf = System.Text.Encoding.Unicode.GetBytes(classGuid + "\0");
      Check(SetupDiSetDeviceRegistryProperty(set, ref d, SPDRP_CLASSGUID, buf, (uint)buf.Length), "SetClassGuid");
    } finally { SetupDiDestroyDeviceInfoList(set); }
  }

  public static string Install(string instanceId, string inf) {
    IntPtr set = SetupDiCreateDeviceInfoList(IntPtr.Zero, IntPtr.Zero);
    try {
      var d = new SP_DEVINFO_DATA(); d.cbSize = (uint)Marshal.SizeOf(d);
      Check(SetupDiOpenDeviceInfo(set, instanceId, IntPtr.Zero, 0, ref d), "OpenDeviceInfo");
      var p = new SP_DEVINSTALL_PARAMS(); p.cbSize = (uint)Marshal.SizeOf(p);
      Check(SetupDiGetDeviceInstallParams(set, ref d, ref p), "GetInstallParams");
      p.Flags |= DI_ENUMSINGLEINF; p.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS; p.DriverPath = inf;
      Check(SetupDiSetDeviceInstallParams(set, ref d, ref p), "SetInstallParams");
      Check(SetupDiBuildDriverInfoList(set, ref d, SPDIT_CLASSDRIVER), "BuildDriverInfoList");
      var drv = new SP_DRVINFO_DATA(); drv.cbSize = (uint)Marshal.SizeOf(drv);
      string found = null; var list = "";
      for (uint i = 0; SetupDiEnumDriverInfo(set, ref d, SPDIT_CLASSDRIVER, i, ref drv); i++) {
        list += " [" + drv.Description + " / " + drv.MfgName + "]";
        if (found == null && drv.Description.IndexOf("WinUsb", StringComparison.OrdinalIgnoreCase) >= 0) found = drv.Description;
        if (found != null) break;
      }
      if (found == null) throw new Exception("WinUsb driver not in list:" + list);
      Check(SetupDiSetSelectedDriver(set, ref d, ref drv), "SetSelectedDriver");
      bool reboot;
      Check(DiInstallDevice(IntPtr.Zero, set, ref d, ref drv, 0, out reboot), "DiInstallDevice");
      return found + (reboot ? " (reboot needed)" : "");
    } finally { SetupDiDestroyDeviceInfoList(set); }
  }
}
"@
  $original = (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName DEVPKEY_Device_ClassGuid).Data
  "original class: $original"
  [ForceWinUsb]::SetClass($InstanceId, [ForceWinUsb]::UsbDeviceClass)
  "class set to USBDevice"
  try {
    $r = [ForceWinUsb]::Install($InstanceId, "$env:windir\INF\winusb.inf")
    "INSTALLED: $r"
  } catch {
    "INSTALL FAILED: $($_.Exception.Message)"
    [ForceWinUsb]::SetClass($InstanceId, $original)
    "class restored to $original"
  }
} catch {
  "ERROR: $($_.Exception.Message)"
} finally {
  Stop-Transcript | Out-Null
}
