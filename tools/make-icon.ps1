<#
.SYNOPSIS
  Rebuilds native/app/tourelle.ico from docs/assets/icon.html (needs Microsoft Edge).
#>
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$repo = Split-Path $PSScriptRoot -Parent
$html = Join-Path $repo 'docs\assets\icon.html'
$png = Join-Path $env:TEMP 'tourelle-icon-256.png'
$ico = Join-Path $repo 'native\app\tourelle.ico'

$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
          "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
& $edge --headless=new --disable-gpu --hide-scrollbars --force-device-scale-factor=1 --default-background-color=00000000 `
    --window-size=256,256 "--screenshot=$png" "file:///$($html -replace '\\', '/')" 2>$null
Start-Sleep -Seconds 2

# ICO with PNG-compressed entries (supported since Windows Vista).
$source = [System.Drawing.Image]::FromFile($png)
$entries = foreach ($size in 16, 24, 32, 48, 64, 128, 256) {
    $bitmap = New-Object System.Drawing.Bitmap $size, $size
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = 'HighQualityBicubic'
    $graphics.SmoothingMode = 'HighQuality'
    $graphics.DrawImage($source, 0, 0, $size, $size)
    $graphics.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    [pscustomobject]@{ Size = $size; Bytes = $stream.ToArray() }
}
$source.Dispose()

$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$entries.Count)
$offset = 6 + 16 * $entries.Count
foreach ($e in $entries) {
    $dimension = if ($e.Size -ge 256) { 0 } else { $e.Size }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$e.Bytes.Length); $writer.Write([uint32]$offset)
    $offset += $e.Bytes.Length
}
foreach ($e in $entries) { $writer.Write($e.Bytes) }
[System.IO.File]::WriteAllBytes($ico, $out.ToArray())
"icon written: $ico"
