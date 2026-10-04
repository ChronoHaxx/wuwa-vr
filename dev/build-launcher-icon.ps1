# Compile the existing site/media/mark.svg geometry into a multi-size Windows icon.
# Uses Windows WPF only; no downloaded artwork or image-generation dependency.
param([string]$Output = (Join-Path $PSScriptRoot '../launcher/native/assets/wuwa-vr.ico'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore,WindowsBase
$images = @()
foreach ($size in @(16,24,32,48,64,256)) {
    $visual = [Windows.Media.DrawingVisual]::new()
    $dc = $visual.RenderOpen()
    $dc.PushTransform([Windows.Media.ScaleTransform]::new($size / 40.0, $size / 40.0))
    $convert = [Windows.Media.BrushConverter]::new()
    $edge = [Windows.Media.Pen]::new($convert.ConvertFromString('#698b7d'), 1)
    $stroke = [Windows.Media.Pen]::new($convert.ConvertFromString('#b2e4cd'), 2)
    $stroke.StartLineCap = $stroke.EndLineCap = [Windows.Media.PenLineCap]::Round
    $stroke.LineJoin = [Windows.Media.PenLineJoin]::Round
    $dc.DrawRoundedRectangle($convert.ConvertFromString('#10181b'), $edge, [Windows.Rect]::new(1,1,38,38),9,9)
    $dc.DrawGeometry($null, $stroke, [Windows.Media.Geometry]::Parse('M7,23 L13,13 L20,27 L27,13 L33,23 M7,29 L33,29'))
    $dc.Pop(); $dc.Close()
    $bitmap = [Windows.Media.Imaging.RenderTargetBitmap]::new($size,$size,96,96,[Windows.Media.PixelFormats]::Pbgra32)
    $bitmap.Render($visual)
    $encoder = [Windows.Media.Imaging.PngBitmapEncoder]::new()
    $encoder.Frames.Add([Windows.Media.Imaging.BitmapFrame]::Create($bitmap))
    $stream = [IO.MemoryStream]::new()
    $encoder.Save($stream)
    $images += ,@($size,$stream.ToArray())
    $stream.Dispose()
}
$full = [IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($full)) | Out-Null
$writer = [IO.BinaryWriter]::new([IO.File]::Create($full))
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($entry in $images) {
        $dimension = if ($entry[0] -eq 256) { 0 } else { $entry[0] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$entry[1].Length); $writer.Write([uint32]$offset)
        $offset += $entry[1].Length
    }
    foreach ($entry in $images) { $writer.Write([byte[]]$entry[1]) }
} finally { $writer.Dispose() }
Get-FileHash -LiteralPath $full -Algorithm SHA256
