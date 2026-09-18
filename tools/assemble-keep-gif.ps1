# Assemble a looping GIF from keep stills (no Pillow/ffmpeg required).
Add-Type -AssemblyName System.Drawing

$src = @(
  'C:\Users\123\.cursor\projects\c-zis\assets\keep-b.png',
  'C:\Users\123\.cursor\projects\c-zis\assets\keep-c.png'
)
$outDir = 'C:\zis\site\img'
$launchDir = 'C:\zis\Launcher\assets'
New-Item -ItemType Directory -Force -Path $outDir, $launchDir | Out-Null

function Resize-Png([string]$path, [int]$w, [int]$h) {
  $srcBmp = [System.Drawing.Bitmap]::FromFile($path)
  $dst = New-Object System.Drawing.Bitmap $w, $h
  $g = [System.Drawing.Graphics]::FromImage($dst)
  $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
  $g.DrawImage($srcBmp, 0, 0, $w, $h)
  $g.Dispose()
  $srcBmp.Dispose()
  return $dst
}

function Get-GifBytes([System.Drawing.Bitmap]$bmp) {
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Gif)
  $bytes = $ms.ToArray()
  $ms.Dispose()
  return $bytes
}

function Get-GctSize([byte[]]$gif) {
  $packed = $gif[10]
  if (($packed -band 0x80) -eq 0) { return 0 }
  $n = ($packed -band 7) + 1
  return [int][Math]::Pow(2, $n) * 3
}

function Get-ImageBlock([byte[]]$gif) {
  $gct = Get-GctSize $gif
  $i = 13 + $gct
  $start = $i
  while ($i -lt $gif.Length) {
    $b = $gif[$i]
    if ($b -eq 0x3B) { break }
    if ($b -eq 0x2C) {
      $start = $i
      break
    }
    if ($b -eq 0x21) {
      $i += 2
      while ($i -lt $gif.Length -and $gif[$i] -ne 0) { $i += 1 + $gif[$i] }
      $i += 1
      continue
    }
    $i++
  }
  $end = $gif.Length
  if ($gif[$gif.Length - 1] -eq 0x3B) { $end = $gif.Length - 1 }
  $len = $end - $start
  $block = New-Object byte[] $len
  [Array]::Copy($gif, $start, $block, 0, $len)
  return $block
}

$w = 480; $h = 640
$frames = New-Object System.Collections.Generic.List[byte[]]
$headerGif = $null
foreach ($p in $src) {
  if (-not (Test-Path -LiteralPath $p)) { throw "missing $p" }
  $bmp = Resize-Png $p $w $h
  $gif = Get-GifBytes $bmp
  $bmp.Dispose()
  $frames.Add((Get-ImageBlock $gif)) | Out-Null
  $headerGif = $gif
}

$gct = Get-GctSize $headerGif
$headerLen = 13 + $gct
$head = New-Object byte[] $headerLen
[Array]::Copy($headerGif, 0, $head, 0, $headerLen)
$head[4] = 0x39

$loop = New-Object byte[] 19
[byte[]]$loopSrc = 0x21, 0xFF, 0x0B, 0x4E, 0x45, 0x54, 0x53, 0x43, 0x41, 0x50, 0x45, 0x32, 0x2E, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00
[Array]::Copy($loopSrc, $loop, 19)
$delay = 55
$gce = New-Object byte[] 8
[byte[]]$gceSrc = 0x21, 0xF9, 0x04, 0x00, ($delay -band 0xFF), (($delay -shr 8) -band 0xFF), 0x00, 0x00
[Array]::Copy($gceSrc, $gce, 8)

$out = New-Object System.Collections.Generic.List[byte]
$out.AddRange($head)
$out.AddRange($loop)
foreach ($block in $frames) {
  $out.AddRange($gce)
  $out.AddRange($block)
}
$out.Add([byte]0x3B) | Out-Null

$gifPath = Join-Path $outDir 'keep.gif'
[IO.File]::WriteAllBytes($gifPath, $out.ToArray())
Copy-Item -Force $gifPath (Join-Path $launchDir 'keep.gif')
Copy-Item -Force $src[0] (Join-Path $outDir 'keep.png')
Copy-Item -Force $src[0] (Join-Path $launchDir 'keep.png')
Copy-Item -Force $src[1] (Join-Path $outDir 'keep-alt.png')
Copy-Item -Force 'C:\Users\123\.cursor\projects\c-zis\assets\crest.png' (Join-Path $outDir 'crest-src.png')
$crestSrc = [System.Drawing.Bitmap]::FromFile((Join-Path $outDir 'crest-src.png'))
$crest = New-Object System.Drawing.Bitmap 128, 128
$cg = [System.Drawing.Graphics]::FromImage($crest)
$cg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$cg.DrawImage($crestSrc, 0, 0, 128, 128)
$cg.Dispose(); $crestSrc.Dispose()
$crest.Save((Join-Path $outDir 'crest.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$crest.Save((Join-Path $launchDir 'crest.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$crest.Dispose()
Remove-Item -Force (Join-Path $outDir 'crest-src.png') -ErrorAction SilentlyContinue

$gi = Get-Item $gifPath
Write-Output "GIF $($gi.Length) bytes frames=$($frames.Count) header=$([Text.Encoding]::ASCII.GetString($headerGif,0,6))"
Get-ChildItem $outDir, $launchDir | Select-Object Name, Length | Format-Table -AutoSize
