<#
.SYNOPSIS
  Writes the camera source DLL embedded in Tourelle.exe (resource RCDATA 2) to a file.
  The release CI uses it to check that Tourelle.exe embeds the signed DLL.
#>
param(
    [Parameter(Mandatory)] [string]$Exe,
    [Parameter(Mandatory)] [string]$Out
)

Add-Type @"
using System; using System.Runtime.InteropServices;
public static class EmbeddedResource {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll")] static extern IntPtr FindResourceW(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr module, IntPtr info);
    [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr module, IntPtr info);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr data);
    public static byte[] Read(string path, int id) {
        // LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE: no code runs.
        IntPtr module = LoadLibraryExW(path, IntPtr.Zero, 0x22);
        if (module == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
        try {
            IntPtr info = FindResourceW(module, (IntPtr)id, (IntPtr)10);  // RT_RCDATA
            if (info == IntPtr.Zero) throw new InvalidOperationException("no RCDATA " + id);
            uint size = SizeofResource(module, info);
            IntPtr data = LockResource(LoadResource(module, info));
            if (data == IntPtr.Zero || size == 0) throw new InvalidOperationException("empty RCDATA " + id);
            byte[] bytes = new byte[size];
            Marshal.Copy(data, bytes, 0, (int)size);
            return bytes;
        } finally { FreeLibrary(module); }
    }
}
"@
[System.IO.File]::WriteAllBytes([System.IO.Path]::GetFullPath($Out), [EmbeddedResource]::Read((Resolve-Path $Exe).Path, 2))
