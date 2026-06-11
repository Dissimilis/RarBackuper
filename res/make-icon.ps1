# Generates res\app.ico (all-PNG multi-size icon).
# Design: violet gradient rounded square, white "archive into tray" glyph.
Add-Type -AssemblyName System.Drawing

function Draw-IconPng([int]$S) {
    $bmp = New-Object System.Drawing.Bitmap($S, $S)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    # rounded-square background with diagonal gradient
    $r = [float]($S * 0.22)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $rect = New-Object System.Drawing.RectangleF(0, 0, $S, $S)
    $d = $r * 2
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($S - $d, 0, $d, $d, 270, 90)
    $path.AddArc($S - $d, $S - $d, $d, $d, 0, 90)
    $path.AddArc(0, $S - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $c1 = [System.Drawing.Color]::FromArgb(255, 124, 58, 237)   # violet
    $c2 = [System.Drawing.Color]::FromArgb(255, 49, 16, 130)    # deep indigo
    $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $c1, $c2, 45.0)
    $g.FillPath($brush, $path)

    # subtle top highlight
    $hl = New-Object System.Drawing.Drawing2D.GraphicsPath
    $hl.AddEllipse([float](-0.25 * $S), [float](-0.55 * $S), [float](1.5 * $S), [float](0.9 * $S))
    $hlBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(38, 255, 255, 255))
    $g.SetClip($path)
    $g.FillPath($hlBrush, $hl)
    $g.ResetClip()

    $white = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)

    # open tray (U shape)
    $penW = [float]($S * 0.085)
    if ($S -le 16) { $penW = 2.0 }
    $pen = New-Object System.Drawing.Pen($white, $penW)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    [System.Drawing.PointF[]]$pts = @(
        (New-Object System.Drawing.PointF([float](0.22 * $S), [float](0.56 * $S))),
        (New-Object System.Drawing.PointF([float](0.22 * $S), [float](0.78 * $S))),
        (New-Object System.Drawing.PointF([float](0.78 * $S), [float](0.78 * $S))),
        (New-Object System.Drawing.PointF([float](0.78 * $S), [float](0.56 * $S)))
    )
    $g.DrawLines($pen, $pts)

    # down arrow (shaft + head as one polygon)
    [System.Drawing.PointF[]]$arrow = @(
        (New-Object System.Drawing.PointF([float](0.43 * $S), [float](0.15 * $S))),
        (New-Object System.Drawing.PointF([float](0.57 * $S), [float](0.15 * $S))),
        (New-Object System.Drawing.PointF([float](0.57 * $S), [float](0.45 * $S))),
        (New-Object System.Drawing.PointF([float](0.68 * $S), [float](0.45 * $S))),
        (New-Object System.Drawing.PointF([float](0.50 * $S), [float](0.66 * $S))),
        (New-Object System.Drawing.PointF([float](0.32 * $S), [float](0.45 * $S))),
        (New-Object System.Drawing.PointF([float](0.43 * $S), [float](0.45 * $S)))
    )
    $wBrush = New-Object System.Drawing.SolidBrush($white)
    $g.FillPolygon($wBrush, $arrow)

    $g.Dispose()
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    return ,$ms.ToArray() # comma keeps the byte[] from unrolling in the pipeline
}

$sizes = 16, 24, 32, 48, 64, 128, 256
$pngs = @{}
foreach ($s in $sizes) { $pngs[$s] = Draw-IconPng $s }

# pack into .ico (PNG-compressed entries are valid on Windows Vista+)
$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter($out)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
foreach ($s in $sizes) {
    [byte[]]$data = $pngs[$s]
    $dim = if ($s -ge 256) { 0 } else { $s }
    $w.Write([byte]$dim); $w.Write([byte]$dim)
    $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$data.Length); $w.Write([uint32]$offset)
    $offset += $data.Length
}
foreach ($s in $sizes) { $w.Write([byte[]]$pngs[$s]) }
$w.Flush()
$bytes = $out.ToArray()
[System.IO.File]::WriteAllBytes("$PSScriptRoot\app.ico", $bytes)
$w.Dispose()
Write-Output "wrote $PSScriptRoot\app.ico ($($bytes.Length) bytes)"
