param([string]$HubInstance, [int]$Port)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class UsbDesc {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  static extern SafeFileHandle CreateFile(string n, uint a, uint s, IntPtr sa, uint c, uint f, IntPtr t);
  [DllImport("kernel32.dll", SetLastError=true)]
  static extern bool DeviceIoControl(SafeFileHandle h, uint code, byte[] inb, int inl, byte[] outb, int outl, out int ret, IntPtr ov);
  public static byte[] Config(string hubPath, int port, int len) {
    using (var h = CreateFile(hubPath, 0x40000000, 2, IntPtr.Zero, 3, 0, IntPtr.Zero)) {
      if (h.IsInvalid) throw new System.ComponentModel.Win32Exception();
      var buf = new byte[12 + len];
      BitConverter.GetBytes(port).CopyTo(buf, 0);
      buf[4] = 0x80; buf[5] = 6;                 // GET_DESCRIPTOR
      buf[6] = 0; buf[7] = 2;                    // wValue: CONFIGURATION, index 0
      BitConverter.GetBytes((ushort)len).CopyTo(buf, 10);
      int ret;
      if (!DeviceIoControl(h, 0x220410, buf, buf.Length, buf, buf.Length, out ret, IntPtr.Zero))
        throw new System.ComponentModel.Win32Exception();
      var outb = new byte[ret - 12]; Array.Copy(buf, 12, outb, 0, outb.Length); return outb;
    }
  }
}
"@
$hubPath = '\\?\' + ($HubInstance -replace '\\','#') + '#{f18a0e88-c30c-11d0-8815-00a0c906bed8}'
$head = [UsbDesc]::Config($hubPath, $Port, 9)
$total = [BitConverter]::ToUInt16($head, 2)
$d = [UsbDesc]::Config($hubPath, $Port, $total)
"total $total bytes"
$i = 0; $iface = -1; $sub = -1
while ($i -lt $d.Length) {
  $len = $d[$i]; $type = $d[$i+1]
  if ($len -eq 0) { break }
  if ($type -eq 0x41) {
    $g = New-Object Guid (,[byte[]]$d[($i+4)..($i+19)])
    "  HIDDEN(0x41) unit id $($d[$i+3]) guid $g raw $([BitConverter]::ToString($d, $i, $len))"
  }
  elseif ($type -eq 5 -and $iface -eq 1) {
    $mps = [BitConverter]::ToUInt16($d, $i+4)
    "    ENDPOINT 0x{0:X2} attr 0x{1:X2} maxPacket {2} x{3}" -f $d[$i+2], $d[$i+3], ($mps -band 0x7FF), ((($mps -shr 11) -band 3) + 1)
  }
  elseif ($type -eq 0x24 -and $iface -eq 1) {
    $st = $d[$i+2]
    switch ($st) {
      4 { $g = New-Object Guid (,[byte[]]$d[($i+5)..($i+20)]); "  FORMAT_UNCOMPRESSED index $($d[$i+3]) frames $($d[$i+4]) guid $g bpp $($d[$i+21])" }
      6 { "  FORMAT_MJPEG index $($d[$i+3]) frames $($d[$i+4])" }
      { $_ -eq 5 -or $_ -eq 7 } {
          $w = [BitConverter]::ToUInt16($d, $i+5); $h = [BitConverter]::ToUInt16($d, $i+7)
          $buf = [BitConverter]::ToUInt32($d, $i+17); $def = [BitConverter]::ToUInt32($d, $i+21); $n = $d[$i+25]
          $iv = @(); for ($k = 0; $k -lt [Math]::Max($n, 3) -and (26 + 4*$k + 4) -le $len; $k++) { $iv += [Math]::Round(1e7 / [BitConverter]::ToUInt32($d, $i+26+4*$k), 1) }
          "    FRAME index $($d[$i+3]) ${w}x${h} maxbuf $buf default {0} fps, fps [{1}]" -f [Math]::Round(1e7/$def,1), ($iv -join ',') }
      default { "  VS subtype $st len $len" }
    }
  }
  elseif ($type -eq 4) { $iface = $d[$i+2]; "INTERFACE $iface alt $($d[$i+3]) class $($d[$i+5]) subclass $($d[$i+6])"; $sub = $d[$i+6] }
  elseif ($type -eq 0x24 -and $iface -ge 0 -and $sub -eq 1 -and $d[$i+5-3] -ne $null) {
    $st = $d[$i+2]
    switch ($st) {
      2 { $tt = [BitConverter]::ToUInt16($d, $i+4); "  INPUT_TERMINAL id $($d[$i+3]) type 0x{0:X4} raw {1}" -f $tt, ([BitConverter]::ToString($d, $i, $len)) }
      5 { "  PROCESSING_UNIT id $($d[$i+3]) source $($d[$i+4]) raw $([BitConverter]::ToString($d, $i, $len))" }
      6 { $g = New-Object Guid (,[byte[]]$d[($i+4)..($i+19)]); $n = $d[$i+20]; $np = $d[$i+21]
          $src = $d[($i+22)..($i+21+$np)] -join ','; $cs = $d[$i+22+$np]
          $bm = [BitConverter]::ToString($d, $i+23+$np, $cs)
          "  EXTENSION_UNIT id $($d[$i+3]) guid $g controls $n sources [$src] bmControls $bm" }
      3 { "  OUTPUT_TERMINAL id $($d[$i+3]) source $($d[$i+7])" }
      default { "  VC subtype $st len $len" }
    }
  }
  $i += $len
}
