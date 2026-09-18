param(
  [Parameter(Mandatory = $true)][string]$Nick,
  [int]$Ram = 8192
)
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $Root 'DynastyLauncher.exe'
if (-not (Test-Path -LiteralPath $exe)) { $exe = Join-Path $Root 'DynastyLauncher.v5.exe' }
if (-not (Test-Path -LiteralPath $exe)) {
  Write-Host 'Соберите лаунчер: Launcher\build.bat'
  exit 1
}
Write-Host "Ник и RAM задаются в окне лаунчера. Запуск $exe (ник $Nick, ${Ram}M — подсказка)."
Start-Process -FilePath $exe -WorkingDirectory $Root
