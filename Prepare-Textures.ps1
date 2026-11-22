# Scales the CC0 source textures in textures_src\ to the exact pixel sizes the panel uses
# and writes them as 24-bit BMPs into assets\ (they are compiled into HaulETA.dll by Build.ps1).
# Only needed when the source textures or the panel layout change.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $root 'textures_src'
$out = Join-Path $root 'assets'
New-Item -ItemType Directory -Path $out -Force | Out-Null

function Save-Tiled([string]$source, [int]$tile, [int]$width, [int]$height, [string]$target) {
    $image = [System.Drawing.Image]::FromFile($source)
    try {
        # Downscale one seamless tile (width $tile, height by the source aspect ratio) with
        # high-quality bicubic filtering first ...
        $tileHeight = [int][Math]::Round($tile * $image.Height / $image.Width)
        $tileBitmap = New-Object System.Drawing.Bitmap $tile, $tileHeight
        $g = [System.Drawing.Graphics]::FromImage($tileBitmap)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $attributes = New-Object System.Drawing.Imaging.ImageAttributes
        $attributes.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::Tile)
        $g.DrawImage($image, (New-Object System.Drawing.Rectangle 0, 0, $tile, $tileHeight),
            0, 0, $image.Width, $image.Height, [System.Drawing.GraphicsUnit]::Pixel, $attributes)
        $g.Dispose()
        # ... then repeat it 1:1 (no further resampling) over the exact target size.
        $result = New-Object System.Drawing.Bitmap $width, $height, ([System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $g = [System.Drawing.Graphics]::FromImage($result)
        for ($y = 0; $y -lt $height; $y += $tileHeight) {
            for ($x = 0; $x -lt $width; $x += $tile) { $g.DrawImageUnscaled($tileBitmap, $x, $y) }
        }
        $g.Dispose()
        $result.Save($target, [System.Drawing.Imaging.ImageFormat]::Bmp)
        $result.Dispose()
        $tileBitmap.Dispose()
        Write-Host ("{0}: {1} x {2} px (tile {3} px)" -f (Split-Path -Leaf $target), $width, $height, $tile)
    } finally { $image.Dispose() }
}

# Panel background: carbon weave, 788 x 104 px (panel size), weave cell about 10 px.
Save-Tiled (Join-Path $src 'Fabric004_1K-JPG_Color.jpg') 128 788 104 (Join-Path $out 'carbon_788x104.bmp')
# Fuel gauge bezel: brushed metal, 76 x 64 px (gauge frame size).
Save-Tiled (Join-Path $src 'Metal009_1K-JPG_Color.jpg') 256 76 64 (Join-Path $out 'metal_76x64.bmp')
# Clock bezel: brushed metal, 120 x 42 px (clock frame size).
Save-Tiled (Join-Path $src 'Metal009_1K-JPG_Color.jpg') 256 120 42 (Join-Path $out 'metal_120x42.bmp')
# Blinker back-light diffuser: frosted plastic (roughness map), 816 x 132 px
# (the panel plus the 14 px glow margin on every side).
Save-Tiled (Join-Path $src 'Plastic004_1K-JPG_Roughness.jpg') 512 816 132 (Join-Path $out 'diffuser_816x132.bmp')
# G-meter bezel: brushed metal, 62 x 62 px (round bezel of the G-meter, radius 31 px).
Save-Tiled (Join-Path $src 'Metal009_1K-JPG_Color.jpg') 256 62 62 (Join-Path $out 'metal_62x62.bmp')
# Light theme background: white paper grain, 788 x 104 px (panel size; the route card uses its left 400 px).
Save-Tiled (Join-Path $src 'Paper001_1K-JPG_Color.jpg') 512 788 104 (Join-Path $out 'paper_788x104.bmp')
