# Draws the program's icon: an original crystal in the blue of the game's
# panels (no artwork from the game is used), and writes
# resources\crystal_defenders.ico with PNG entries from 16 to 256 pixels.
# Usage: make_icon.ps1 [-Preview <png>]
param([string]$Preview = '')
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$root = Split-Path (Split-Path $PSScriptRoot)

function P([double]$x, [double]$y) { New-Object System.Drawing.PointF ([single]$x), ([single]$y) }
function Fill($g, [int[]]$rgb, $points) { $b = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($rgb[0], $rgb[1], $rgb[2])); $g.FillPolygon($b, [System.Drawing.PointF[]]$points); $b.Dispose() }

$size = 512
$art = New-Object System.Drawing.Bitmap $size, $size
$g = [System.Drawing.Graphics]::FromImage($art)
$g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'

# Rounded panel, dark blue with a lighter rim, like the game's dialogs.
$path = New-Object System.Drawing.Drawing2D.GraphicsPath
$r = 96; $m = 16; $w = $size - 2 * $m
$path.AddArc($m, $m, $r, $r, 180, 90); $path.AddArc($m + $w - $r, $m, $r, $r, 270, 90)
$path.AddArc($m + $w - $r, $m + $w - $r, $r, $r, 0, 90); $path.AddArc($m, $m + $w - $r, $r, $r, 90, 90); $path.CloseFigure()
$back = New-Object System.Drawing.Drawing2D.LinearGradientBrush (P 0 0), (P 0 $size), ([System.Drawing.Color]::FromArgb(52, 120, 160)), ([System.Drawing.Color]::FromArgb(16, 44, 70))
$g.FillPath($back, $path)
$rim = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(140, 208, 232)), 10
$g.DrawPath($rim, $path)
$edge = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(10, 30, 48)), 6
$g.DrawPath($edge, $path)

# Soft glow behind the crystal.
$glowPath = New-Object System.Drawing.Drawing2D.GraphicsPath
$glowPath.AddEllipse(86, 70, 340, 372)
$glow = New-Object System.Drawing.Drawing2D.PathGradientBrush $glowPath
$glow.CenterColor = [System.Drawing.Color]::FromArgb(150, 170, 240, 255)
$glow.SurroundColors = @([System.Drawing.Color]::FromArgb(0, 170, 240, 255))
$g.FillPath($glow, $glowPath)

# The crystal: a tall six-sided gem, lit from the upper left.
$top = P 256 62; $bottom = P 256 450
$ul = P 150 178; $ur = P 362 178; $ll = P 150 318; $lr = P 362 318
$cu = P 256 214; $cl = P 256 296
Fill $g @(214, 248, 255) @($top, $ul, $cu)          # upper left facet
Fill $g @(120, 208, 244) @($top, $cu, $ur)          # upper right facet
Fill $g @(150, 226, 250) @($ul, $ll, $cl, $cu)      # left side
Fill $g @(58, 150, 214)  @($cu, $cl, $lr, $ur)      # right side
Fill $g @(70, 168, 224)  @($ll, $bottom, $cl)       # lower left facet
Fill $g @(28, 96, 170)   @($cl, $bottom, $lr)       # lower right facet
$outline = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(14, 52, 96)), 9
$outline.LineJoin = 'Round'
$g.DrawPolygon($outline, [System.Drawing.PointF[]]@($top, $ur, $lr, $bottom, $ll, $ul))
$inner = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(120, 236, 252, 255)), 4
foreach ($pair in @($top, $cu), @($ul, $cu), @($ur, $cu), @($cu, $cl), @($ll, $cl), @($lr, $cl), @($cl, $bottom)) { $g.DrawLine($inner, $pair[0], $pair[1]) }

# A four-pointed sparkle on the upper left facet.
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(245, 255, 255, 255))
$g.FillPolygon($white, [System.Drawing.PointF[]]@((P 206 118), (P 214 148), (P 244 156), (P 214 164), (P 206 194), (P 198 164), (P 168 156), (P 198 148)))
$g.Dispose()

if ($Preview) { $art.Save($Preview) }

$images = foreach ($s in 16, 24, 32, 48, 64, 256) {
    $bitmap = New-Object System.Drawing.Bitmap $s, $s
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.InterpolationMode = 'HighQualityBicubic'; $graphics.PixelOffsetMode = 'HighQuality'
    $graphics.DrawImage($art, 0, 0, $s, $s)
    $graphics.Dispose()
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    , @($s, $stream.ToArray())
}
$art.Dispose()

$out = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $out
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
$offset = 6 + 16 * $images.Count
foreach ($image in $images) {
    $dimension = if ($image[0] -ge 256) { 0 } else { $image[0] }
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32)
    $writer.Write([uint32]$image[1].Length); $writer.Write([uint32]$offset)
    $offset += $image[1].Length
}
foreach ($image in $images) { $writer.Write($image[1]) }
$target = Join-Path $root 'recomp\resources'
New-Item -ItemType Directory -Force $target | Out-Null
[IO.File]::WriteAllBytes((Join-Path $target 'crystal_defenders.ico'), $out.ToArray())
'{0} bytes' -f $out.Length
