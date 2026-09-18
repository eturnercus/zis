@echo off
cd /d "%~dp0"
if exist DynastyLauncher.exe (
  start "" "%~dp0DynastyLauncher.exe"
  exit /b 0
)
echo Соберите лаунчер: build.bat
pause
