@echo off
setlocal
cd /d "%~dp0"
where g++ >nul 2>nul && goto :gcc
if exist "C:\mingw64\bin\g++.exe" set PATH=C:\mingw64\bin;%PATH% & goto :gcc
for /d %%D in ("%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT*") do (
  if exist "%%D\mingw64\bin\g++.exe" set PATH=%%D\mingw64\bin;%PATH% & goto :gcc
)
echo g++ not found. Install WinLibs POSIX UCRT or put g++ on PATH.
exit /b 1
:gcc
where windres >nul 2>nul || (echo windres not found on PATH after locating g++. & exit /b 1)
windres -I. src\app.rc -O coff -o src\app.res
if errorlevel 1 exit /b 1
g++ -std=c++17 -O2 -municode -mwindows -finput-charset=utf-8 src\main.cpp src\engine.cpp src\app.res -static-libgcc -static-libstdc++ -static -lgdi32 -lgdiplus -ldwmapi -luser32 -lcomctl32 -lmsimg32 -lwinhttp -lole32 -lshell32 -lshlwapi -lbcrypt -lwinpthread -lwindowscodecs -luxtheme -o DynastyLauncher.exe
if errorlevel 1 exit /b 1
if not exist "..\site\download" mkdir "..\site\download"
if not exist "..\site\dl" mkdir "..\site\dl"
if not exist "..\dist\launcher" mkdir "..\dist\launcher"
if not exist "..\dist\launcher\config" mkdir "..\dist\launcher\config"
copy /Y DynastyLauncher.exe ..\site\download\DynastyLauncher.exe >nul
copy /Y DynastyLauncher.exe ..\site\dl\DynastyLauncher.exe >nul
copy /Y DynastyLauncher.exe ..\dist\launcher\DynastyLauncher.exe >nul
if exist "config\live.json" copy /Y config\live.json ..\dist\launcher\config\live.json >nul
if exist "assets\app.ico" copy /Y assets\app.ico ..\dist\launcher\app.ico >nul
if exist "assets\crest.png" (
  if not exist "..\dist\launcher\assets" mkdir "..\dist\launcher\assets"
  copy /Y assets\crest.png ..\dist\launcher\assets\crest.png >nul
)
powershell -NoProfile -Command "$h=(Get-FileHash -Algorithm SHA256 -LiteralPath 'DynastyLauncher.exe').Hash.ToLowerInvariant(); $b=[Text.Encoding]::ASCII.GetBytes($h); [IO.File]::WriteAllBytes((Join-Path (Resolve-Path '..\site\download') 'DynastyLauncher.exe.sha256'), $b); [IO.File]::WriteAllBytes((Join-Path (Resolve-Path '..\site\dl') 'DynastyLauncher.exe.sha256'), $b); [IO.File]::WriteAllBytes((Join-Path (Resolve-Path '..\dist\launcher') 'DynastyLauncher.exe.sha256'), $b)"
echo Built DynastyLauncher.exe
dir DynastyLauncher.exe ..\dist\launcher\DynastyLauncher.exe ..\site\download\DynastyLauncher.exe.sha256
