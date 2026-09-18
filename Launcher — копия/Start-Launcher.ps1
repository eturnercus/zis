# Dynasty of Rot — local host for the branded launcher UI (melody-style nick/play/settings).
$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Ui = Join-Path $Root 'ui'
$Runtime = Join-Path $Root 'runtime'
$ConfigPath = Join-Path $Root 'config\live.json'
$StatePath = Join-Path $Runtime 'state.json'
$LogPath = Join-Path $Runtime 'launch.log'
New-Item -ItemType Directory -Force -Path $Runtime | Out-Null

$live = Get-Content $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
$state = @{
  nick = ''
  ram = 8192
  gameDir = Join-Path $Root 'game'
  server = "$($live.settings.server_host):$($live.settings.server_port)"
  online = $false
}
if (Test-Path $StatePath) {
  $saved = Get-Content $StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($saved.nick) { $state.nick = $saved.nick }
  if ($saved.ram) { $state.ram = [int]$saved.ram }
  if ($saved.gameDir) { $state.gameDir = $saved.gameDir }
}

function Save-State {
  ($state | ConvertTo-Json) | Set-Content -Encoding UTF8 $StatePath
}

function Get-Mime($path) {
  switch ([IO.Path]::GetExtension($path).ToLower()) {
    '.html' { 'text/html; charset=utf-8' }
    '.css'  { 'text/css; charset=utf-8' }
    '.js'   { 'application/javascript; charset=utf-8' }
    '.json' { 'application/json; charset=utf-8' }
    '.svg'  { 'image/svg+xml' }
    '.png'  { 'image/png' }
    default { 'application/octet-stream' }
  }
}

function Read-Body($req) {
  $reader = New-Object IO.StreamReader($req.InputStream, $req.ContentEncoding)
  try { return $reader.ReadToEnd() } finally { $reader.Close() }
}

function Write-Json($res, $obj, $code = 200) {
  $bytes = [Text.Encoding]::UTF8.GetBytes(($obj | ConvertTo-Json -Compress -Depth 6))
  $res.StatusCode = $code
  $res.ContentType = 'application/json; charset=utf-8'
  $res.ContentLength64 = $bytes.Length
  $res.OutputStream.Write($bytes, 0, $bytes.Length)
}

$listener = New-Object System.Net.HttpListener
$prefix = 'http://127.0.0.1:8742/'
$listener.Prefixes.Add($prefix)
try {
  $listener.Start()
} catch {
  Write-Host "Port 8742 busy or URL ACL missing: $($_.Exception.Message)"
  exit 1
}

$edge = @(
  "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe",
  "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($edge) {
  Start-Process $edge -ArgumentList "--app=$prefix","--window-size=1080,720"
} else {
  Start-Process $prefix
}

Write-Host "Dynasty of Rot launcher: $prefix"
Write-Host "Close this window to stop the host."

while ($listener.IsListening) {
  $ctx = $listener.GetContext()
  $req = $ctx.Request
  $res = $ctx.Response
  try {
    $path = [Uri]::UnescapeDataString($req.Url.AbsolutePath)
    if ($path -eq '/') { $path = '/index.html' }

    if ($path -eq '/api/state' -and $req.HttpMethod -eq 'GET') {
      Write-Json $res $state
    }
    elseif ($path -eq '/api/log' -and $req.HttpMethod -eq 'GET') {
      $text = if (Test-Path $LogPath) { Get-Content $LogPath -Raw -Encoding UTF8 } else { 'Готов к работе.' }
      Write-Json $res @{ text = $text }
    }
    elseif ($path -eq '/api/play' -and $req.HttpMethod -eq 'POST') {
      $body = Read-Body $req | ConvertFrom-Json
      if ($body.nick) { $state.nick = [string]$body.nick }
      if ($body.ram) { $state.ram = [int]$body.ram }
      if ($body.gameDir) { $state.gameDir = [string]$body.gameDir }
      Save-State
      $launch = Join-Path $Root 'Launch-Game.ps1'
      $arg = "-NoProfile -ExecutionPolicy Bypass -File `"$launch`" -Nick `"$($state.nick)`" -Ram $($state.ram) -GameDir `"$($state.gameDir)`""
      Start-Process -FilePath 'powershell.exe' -ArgumentList $arg -WindowStyle Minimized
      Write-Json $res @{ ok = $true; message = 'Синхронизация и запуск пошли. Лог обновится сам.' }
    }
    else {
      $file = Join-Path $Ui ($path.TrimStart('/').Replace('/','\'))
      $full = [IO.Path]::GetFullPath($file)
      $uiFull = [IO.Path]::GetFullPath($Ui)
      if (-not $full.StartsWith($uiFull)) { throw 'bad path' }
      if (-not (Test-Path $full)) {
        $res.StatusCode = 404
        $bytes = [Text.Encoding]::UTF8.GetBytes('not found')
        $res.OutputStream.Write($bytes, 0, $bytes.Length)
      } else {
        $bytes = [IO.File]::ReadAllBytes($full)
        $res.ContentType = Get-Mime $full
        $res.ContentLength64 = $bytes.Length
        $res.OutputStream.Write($bytes, 0, $bytes.Length)
      }
    }
  } catch {
    $res.StatusCode = 500
    $bytes = [Text.Encoding]::UTF8.GetBytes($_.Exception.Message)
    $res.OutputStream.Write($bytes, 0, $bytes.Length)
  } finally {
    $res.OutputStream.Close()
  }
}
