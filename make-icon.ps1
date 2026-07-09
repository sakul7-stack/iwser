# Generates assets/iwser.ico (multi-size) for the app.
# If assets/logo.png exists it is used as the source; otherwise a placeholder
# feather is drawn. Run automatically by build.bat.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$logo = Join-Path $root 'assets\logo.png'
$out  = Join-Path $root 'assets\iwser.ico'
New-Item -ItemType Directory -Force -Path (Join-Path $root 'assets') | Out-Null

# ---- Build a 256x256 source bitmap -----------------------------------------
$S = 256
$src = New-Object System.Drawing.Bitmap $S, $S
$g = [System.Drawing.Graphics]::FromImage($src)
$g.SmoothingMode = 'AntiAlias'
$g.InterpolationMode = 'HighQualityBicubic'
$g.PixelOffsetMode = 'HighQuality'

if (Test-Path $logo) {
    # Use the user-provided logo on a white rounded tile.
    $g.Clear([System.Drawing.Color]::White)
    $img = [System.Drawing.Image]::FromFile($logo)
    try {
        # Contain the image within the canvas, centered.
        $scale = [Math]::Min($S / $img.Width, $S / $img.Height)
        $w = [int]($img.Width * $scale); $h = [int]($img.Height * $scale)
        $x = [int](($S - $w) / 2); $y = [int](($S - $h) / 2)
        $g.DrawImage($img, $x, $y, $w, $h)
    } finally { $img.Dispose() }
} else {
    # Placeholder: white tile + a simple black feather (replaced once logo.png exists).
    $g.Clear([System.Drawing.Color]::White)
    $black = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(20,20,20))
    $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(20,20,20)), 10
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    # feather leaf
    $leaf = New-Object System.Drawing.Drawing2D.GraphicsPath
    $leaf.AddBezier(60,196, 70,110, 150,60, 200,58)
    $leaf.AddBezier(200,58, 170,120, 120,170, 60,196)
    $g.FillPath($black, $leaf)
    # shaft + a couple barbs
    $g.DrawLine($pen, 66,196, 190,64)
    $g.DrawLine($pen, 44,214, 78,182)
    $pen.Dispose(); $black.Dispose()
}
$g.Dispose()

# ---- Write a multi-size PNG-framed ICO -------------------------------------
$sizes = 16,24,32,48,64,128,256
$pngs = @()
foreach ($sz in $sizes) {
    $bmp = New-Object System.Drawing.Bitmap $sz, $sz
    $gg = [System.Drawing.Graphics]::FromImage($bmp)
    $gg.SmoothingMode = 'AntiAlias'
    $gg.InterpolationMode = 'HighQualityBicubic'
    $gg.PixelOffsetMode = 'HighQuality'
    $gg.DrawImage($src, 0, 0, $sz, $sz)
    $gg.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngs += ,($ms.ToArray())
    $bmp.Dispose(); $ms.Dispose()
}
$src.Dispose()

$fs = [System.IO.File]::Create($out)
$bw = New-Object System.IO.BinaryWriter $fs
$bw.Write([UInt16]0); $bw.Write([UInt16]1); $bw.Write([UInt16]$pngs.Count)  # header
$offset = 6 + 16 * $pngs.Count
for ($i = 0; $i -lt $pngs.Count; $i++) {
    $sz = $sizes[$i]; $b = $pngs[$i]
    $dim = if ($sz -ge 256) { 0 } else { $sz }
    $bw.Write([Byte]$dim); $bw.Write([Byte]$dim)          # width, height
    $bw.Write([Byte]0); $bw.Write([Byte]0)                # colors, reserved
    $bw.Write([UInt16]1); $bw.Write([UInt16]32)           # planes, bpp
    $bw.Write([UInt32]$b.Length); $bw.Write([UInt32]$offset)
    $offset += $b.Length
}
foreach ($b in $pngs) { $bw.Write($b) }
$bw.Flush(); $fs.Close()

if (Test-Path $logo) { Write-Host "iwser.ico built from assets/logo.png" }
else { Write-Host "iwser.ico built with PLACEHOLDER feather (add assets/logo.png for your logo)" }
