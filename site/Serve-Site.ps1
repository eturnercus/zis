#Requires -Version 5.1
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$zis = Split-Path -Parent $root
$cloud = Join-Path $zis 'cloud'
$listener = New-Object System.Net.HttpListener
$listener.Prefixes.Add('http://127.0.0.1:8743/')
$listener.Start()
function Mime($p) {
  switch ([IO.Path]::GetExtension($p).ToLowerInvariant()) {
    '.html' { 'text/html; charset=utf-8' }
    '.css' { 'text/css; charset=utf-8' }
    '.js' { 'application/javascript; charset=utf-8' }
    '.svg' { 'image/svg+xml' }
    '.png' { 'image/png' }
    '.gif' { 'image/gif' }
    '.jpg' { 'image/jpeg' }
    '.jpeg' { 'image/jpeg' }
    '.webp' { 'image/webp' }
    '.mp4' { 'video/mp4' }
    '.webm' { 'video/webm' }
    '.ico' { 'image/x-icon' }
    '.json' { 'application/json; charset=utf-8' }
    '.md' { 'text/plain; charset=utf-8' }
    '.txt' { 'text/plain; charset=utf-8' }
    '.sha256' { 'text/plain; charset=utf-8' }
    '.exe' { 'application/vnd.microsoft.portable-executable' }
    '.dll' { 'application/octet-stream' }
    '.zip' { 'application/zip' }
    '.jar' { 'application/java-archive' }
    default { 'application/octet-stream' }
  }
}
function Test-HiddenName([string]$name) {
  $n = $name.ToLowerInvariant()
  if ($n -eq 'news' -or $n -eq 'old' -or $n -eq 'manifest.json' -or $n -eq 'build-index.ps1') { return $true }
  if ($n -eq 'serve-site.ps1' -or $n -eq 'nginx-download.conf' -or $n -eq 'zis-admin.service') { return $true }
  if ($n.StartsWith('.')) { return $true }
  if ($n -eq 'admin-api.php') { return $true }
  if ($n -match '\.(ps1|py|service)$') { return $true }
  if ($n -match 'linux') { return $true }
  return $false
}
function Resolve-SiteFile([string]$p) {
  if ($p -eq '/') { $p = '/index.html' }
  if ($p -eq '/admin' -or $p -eq '/admin/') { $p = '/admin.html' }
  $rel = $p.TrimStart('/').Replace('/','\')
  if ($rel.StartsWith('.')) { return $null }
  if ($rel -match '(^|[\\/])\.\.([\\/]|$)') { return $null }
  if ($rel -match '\.(ps1|py|service|php)$') { return $null }
  if ($rel -like 'dl\*') {
    $fromSite = Join-Path $root $rel
    if (Test-Path -LiteralPath $fromSite -PathType Leaf) { return $fromSite }
    $fromLauncher = Join-Path (Join-Path $zis 'Launcher') (Split-Path $rel -Leaf)
    if (Test-Path -LiteralPath $fromLauncher -PathType Leaf) { return $fromLauncher }
  }
  if ($rel -match '^download\\DynastyLauncher\.exe(\.sha256)?$') {
    $fromSite = Join-Path $root $rel
    if (Test-Path -LiteralPath $fromSite -PathType Leaf) { return $fromSite }
    $fromLauncher = Join-Path (Join-Path $zis 'Launcher') (Split-Path $rel -Leaf)
    if (Test-Path -LiteralPath $fromLauncher -PathType Leaf) { return $fromLauncher }
  }
  $f = Join-Path $root $rel
  $full = [IO.Path]::GetFullPath($f)
  $rootFull = [IO.Path]::GetFullPath($root)
  if (-not $full.StartsWith($rootFull, [StringComparison]::OrdinalIgnoreCase)) { return $null }
  return $full
}
function Resolve-Cloud([string]$path) {
  if ($path -notmatch '^/download/cloud(/.*)?$') { return $null }
  $rel = ($path -replace '^/download/cloud/?','').Replace('/','\')
  if ($rel -match '(^|[\\/])\.\.([\\/]|$)') { return $null }
  $cloudFull = [IO.Path]::GetFullPath($cloud)
  if ([string]::IsNullOrEmpty($rel)) {
    return @{ Kind = 'dir'; Path = $cloudFull; Url = '/download/cloud/' }
  }
  $full = [IO.Path]::GetFullPath((Join-Path $cloud $rel))
  if (-not $full.StartsWith($cloudFull, [StringComparison]::OrdinalIgnoreCase)) { return $null }
  $leaf = Split-Path $full -Leaf
  if (Test-HiddenName $leaf) { return $null }
  if (Test-Path -LiteralPath $full -PathType Container) {
    $url = $path
    if (-not $url.EndsWith('/')) { $url += '/' }
    return @{ Kind = 'dir'; Path = $full; Url = $url }
  }
  if (Test-Path -LiteralPath $full -PathType Leaf) {
    return @{ Kind = 'file'; Path = $full }
  }
  return $null
}
function HtmlEnc([string]$s) {
  if ($null -eq $s) { return '' }
  ($s -replace '&','&amp;' -replace '<','&lt;' -replace '>','&gt;' -replace '"','&quot;')
}
function Send-Bytes($ctx, [byte[]]$bytes, [string]$ctype, [int]$code = 200) {
  $ctx.Response.StatusCode = $code
  $ctx.Response.ContentType = $ctype
  $ctx.Response.ContentLength64 = $bytes.Length
  $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
}
function Send-DirIndex($ctx, [string]$dir, [string]$urlPath, [object[]]$extraDirs) {
  if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
  }
  $items = @(Get-ChildItem -LiteralPath $dir | Where-Object { -not $_.Name.StartsWith('.') -and -not (Test-HiddenName $_.Name) } | Sort-Object { -not $_.PSIsContainer }, Name)
  $lines = New-Object System.Collections.Generic.List[string]
  [void]$lines.Add('<html>')
  [void]$lines.Add('<head><meta charset="utf-8"><title>Index of ' + (HtmlEnc $urlPath) + '</title></head>')
  [void]$lines.Add('<body bgcolor="white">')
  [void]$lines.Add('<h1>Index of ' + (HtmlEnc $urlPath) + '</h1><hr><pre><a href="../">../</a>')
  foreach ($ed in @($extraDirs)) {
    $disp = [string]$ed
    $pad = [Math]::Max(1, 51 - $disp.Length)
    [void]$lines.Add(('<a href="{0}">{1}</a>{2}{3} {4}' -f (HtmlEnc $disp), (HtmlEnc $disp), (' ' * $pad), '                  -', '-'.PadLeft(20)))
  }
  foreach ($it in $items) {
    $disp = if ($it.PSIsContainer) { $it.Name + '/' } else { $it.Name }
    $href = $disp
    $when = $it.LastWriteTime.ToString('dd-MMM-yyyy HH:mm', [Globalization.CultureInfo]::InvariantCulture)
    $size = if ($it.PSIsContainer) { '-'.PadLeft(20) } else { $it.Length.ToString().PadLeft(20) }
    $pad = [Math]::Max(1, 51 - $disp.Length)
    [void]$lines.Add(('<a href="{0}">{1}</a>{2}{3} {4}' -f (HtmlEnc $href), (HtmlEnc $disp), (' ' * $pad), $when, $size))
  }
  [void]$lines.Add('</pre><hr></body>')
  [void]$lines.Add('</html>')
  $html = ($lines -join "`n") + "`n"
  $bytes = [Text.Encoding]::UTF8.GetBytes($html)
  Send-Bytes $ctx $bytes 'text/html; charset=utf-8'
}
function Send-FileStream($ctx, [string]$file) {
  $fs = [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
  try {
    $len = $fs.Length
    $ext = [IO.Path]::GetExtension($file).ToLowerInvariant()
    $start = [int64]0
    $end = [int64]($len - 1)
    $code = 200
    $range = [string]$ctx.Request.Headers['Range']
    if ($range -and $range.StartsWith('bytes=') -and $len -gt 0) {
      $spec = $range.Substring(6)
      $dash = $spec.IndexOf('-')
      if ($dash -ge 0) {
        $a = $spec.Substring(0, $dash)
        $b = $spec.Substring($dash + 1)
        if ($a) { $start = [int64]$a }
        if ($b) { $end = [int64]$b }
        if ($start -lt 0) { $start = 0 }
        if ($end -ge $len) { $end = $len - 1 }
        if ($start -le $end) { $code = 206 }
        else { $start = 0; $end = $len - 1 }
      }
    }
    $ctx.Response.StatusCode = $code
    $ctx.Response.ContentType = Mime $file
    $ctx.Response.Headers['Accept-Ranges'] = 'bytes'
    if ($ext -eq '.json') {
      $ctx.Response.Headers['Cache-Control'] = 'no-store'
    }
    if ($code -eq 206) {
      $ctx.Response.Headers['Content-Range'] = ('bytes {0}-{1}/{2}' -f $start, $end, $len)
    }
    if ($ext -eq '.exe' -or $ext -eq '.zip' -or $ext -eq '.jar') {
      $name = [IO.Path]::GetFileName($file)
      $ctx.Response.Headers.Add('Content-Disposition', "attachment; filename=`"$name`"")
    }
    $remain = [int64]($end - $start + 1)
    $ctx.Response.ContentLength64 = $remain
    [void]$fs.Seek($start, [IO.SeekOrigin]::Begin)
    $buf = New-Object byte[] 65536
    while ($remain -gt 0) {
      $take = [int][Math]::Min($buf.Length, $remain)
      $n = $fs.Read($buf, 0, $take)
      if ($n -le 0) { break }
      $ctx.Response.OutputStream.Write($buf, 0, $n)
      $remain -= $n
    }
  } finally {
    $fs.Close()
  }
}
. (Join-Path $zis 'private\Admin-Api.ps1')
Write-Host 'site http://127.0.0.1:8743/'
while ($listener.IsListening) {
  $c = $listener.GetContext()
  $path = $c.Request.Url.AbsolutePath
  $isAdminApi = $path.StartsWith('/admin/api') -or $path.StartsWith('/admin-api.php')
  try {
    if ($isAdminApi) {
      Invoke-AdminNative $c
    } else {
    $cloudHit = Resolve-Cloud $path
    if ($null -ne $cloudHit) {
      if ($cloudHit.Kind -eq 'dir') {
        Send-DirIndex $c $cloudHit.Path $cloudHit.Url @()
      } else {
        Send-FileStream $c $cloudHit.Path
      }
    } elseif ($path -eq '/download' -or $path -eq '/download/') {
      $extra = @()
      if (Test-Path -LiteralPath $cloud -PathType Container) { $extra = @('cloud/') }
      Send-DirIndex $c (Join-Path $root 'download') '/download/' $extra
    } else {
      $f = Resolve-SiteFile $path
      if (-not $f -or -not (Test-Path -LiteralPath $f -PathType Leaf)) { throw 'missing' }
      Send-FileStream $c $f
    }
    }
  } catch {
    if ($isAdminApi) {
      try { Send-AdminJson $c 500 @{ ok = $false; error = 'write_failed' } $null $false } catch {}
    } else {
      $c.Response.StatusCode = 404
    }
  }
  $c.Response.Close()
}
