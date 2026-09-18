# Dynasty of Rot — C++ launcher

Нативное Win32-приложение (`DynastyLauncher.exe`). Синк cloud в `%USERPROFILE%\.zisLauncher`. Запуск как у Bunlauncher: DLL из natives-windows в `game\natives`, затем `java` + BootstrapLauncher по `versions/*.json`.

## Сборка

Нужен MinGW g++ (WinLibs POSIX UCRT):

```
build.bat
```

## Запуск

`DynastyLauncher.exe` из этой папки. Конфиг: `config\live.json`.
