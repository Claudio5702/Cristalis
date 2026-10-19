# Draws the program's icon: the initial C of Cristalis in the gold of the setup screen's
# title, with an original crystal, on the blue of the game's panels (no artwork from
# the game is used), and writes
# resources\crystal_defenders.ico with PNG entries from 16 to 256 pixels, and a grey
# crystal_defenders_uninstall.ico for the uninstaller.
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

# The initial "C" of Cristalis, lettered like the setup screen's title (Constantia capitals, in the gold
# of its logo), as Kahna's icon carries its "K".
$family = New-Object System.Drawing.FontFamily 'Constantia'
$fmt = [System.Drawing.StringFormat]::GenericTypographic
$letter = New-Object System.Drawing.Drawing2D.GraphicsPath
$letter.AddString('C', $family, [int][System.Drawing.FontStyle]::Bold, 400, (P 0 0), $fmt)
$lb = $letter.GetBounds()
$scale = 372 / $lb.Height
$fit = New-Object System.Drawing.Drawing2D.Matrix
$fit.Translate(250 - ($lb.X + $lb.Width / 2) * $scale, 256 - ($lb.Y + $lb.Height / 2) * $scale)
$fit.Scale($scale, $scale)
$letter.Transform($fit)
$shadow = $letter.Clone(); $sm = New-Object System.Drawing.Drawing2D.Matrix; $sm.Translate(6, 9); $shadow.Transform($sm)
$shade = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(110, 4, 16, 30))
$g.FillPath($shade, $shadow)
$letterEdge = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(40, 30, 10)), 12
$letterEdge.LineJoin = 'Round'
$g.DrawPath($letterEdge, $letter)
$lb = $letter.GetBounds()
$gold = New-Object System.Drawing.Drawing2D.LinearGradientBrush (P 0 $lb.Top), (P 0 $lb.Bottom), ([System.Drawing.Color]::FromArgb(250, 238, 186)), ([System.Drawing.Color]::FromArgb(196, 160, 82))
$g.FillPath($gold, $letter)

# The crystal, smaller, in the opening of the C, with its glow.
$cx = 318; $cy = 256; $k = 0.42
function Q([double]$x, [double]$y) { P ($cx + ($x - 256) * $k) ($cy + ($y - 256) * $k) }
$glowPath = New-Object System.Drawing.Drawing2D.GraphicsPath
$glowPath.AddEllipse($cx - 92, $cy - 100, 184, 200)
$glow = New-Object System.Drawing.Drawing2D.PathGradientBrush $glowPath
$glow.CenterColor = [System.Drawing.Color]::FromArgb(150, 170, 240, 255)
$glow.SurroundColors = @([System.Drawing.Color]::FromArgb(0, 170, 240, 255))
$g.FillPath($glow, $glowPath)
$top = Q 256 62; $bottom = Q 256 450
$ul = Q 150 178; $ur = Q 362 178; $ll = Q 150 318; $lr = Q 362 318
$cu = Q 256 214; $cl = Q 256 296
Fill $g @(214, 248, 255) @($top, $ul, $cu)
Fill $g @(120, 208, 244) @($top, $cu, $ur)
Fill $g @(150, 226, 250) @($ul, $ll, $cl, $cu)
Fill $g @(58, 150, 214)  @($cu, $cl, $lr, $ur)
Fill $g @(70, 168, 224)  @($ll, $bottom, $cl)
Fill $g @(28, 96, 170)   @($cl, $bottom, $lr)
$outline = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(14, 52, 96)), 6
$outline.LineJoin = 'Round'
$g.DrawPolygon($outline, [System.Drawing.PointF[]]@($top, $ur, $lr, $bottom, $ll, $ul))
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(245, 255, 255, 255))
$g.FillPolygon($white, [System.Drawing.PointF[]]@((Q 206 118), (Q 214 148), (Q 244 156), (Q 214 164), (Q 206 194), (Q 198 164), (Q 168 156), (Q 198 148)))
$g.Dispose()

if ($Preview) { $art.Save($Preview) }

# An .ico of PNG entries from 16 to 256 pixels, from a 512 x 512 drawing.
function Write-Ico($art, [string]$name) {
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
    [IO.File]::WriteAllBytes((Join-Path $target $name), $out.ToArray())
    '{0}: {1} bytes' -f $name, $out.Length
}

Write-Ico $art 'crystal_defenders.ico'

# The uninstaller's icon: the same drawing in shades of grey (relief and shadows kept), so that
# Uninstall.exe is not mistaken for the game next to it.
$grey = New-Object System.Drawing.Bitmap $size, $size
$gg = [System.Drawing.Graphics]::FromImage($grey)
$m = New-Object System.Drawing.Imaging.ColorMatrix
$m.Matrix00 = 0.30; $m.Matrix01 = 0.30; $m.Matrix02 = 0.30
$m.Matrix10 = 0.59; $m.Matrix11 = 0.59; $m.Matrix12 = 0.59
$m.Matrix20 = 0.11; $m.Matrix21 = 0.11; $m.Matrix22 = 0.11
$m.Matrix33 = 1; $m.Matrix44 = 1
$attributes = New-Object System.Drawing.Imaging.ImageAttributes
$attributes.SetColorMatrix($m)
$gg.DrawImage($art, (New-Object System.Drawing.Rectangle 0, 0, $size, $size), 0, 0, $size, $size, 'Pixel', $attributes)
$gg.Dispose()
if ($Preview) { $grey.Save([IO.Path]::ChangeExtension($Preview, '.uninstall.png')) }
Write-Ico $grey 'crystal_defenders_uninstall.ico'
$grey.Dispose()
$art.Dispose()
