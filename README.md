# Winchisel (WinUI 3 / C++ rewrite)

Native Windows-Tweaking-App, Rewrite der Rust-Version. Funktional identisch; siehe `todo.md` und `docs/architecture.md`.

## Build

Voraussetzungen: Visual Studio 2026 (C++), Windows 11 SDK 26100+.

```powershell
.\nuget.exe restore Winchisel.sln -PackagesDirectory packages
msbuild Winchisel.sln /p:Configuration=Debug /p:Platform=x64
```

`nuget.exe` liegt nicht im Repo; einmalig von https://dist.nuget.org/win-x86-commandline/latest/nuget.exe holen.

Unpackaged: Windows App Runtime 1.8 muss auf dem Rechner liegen, oder der Build ist self-contained (`WindowsAppSDKSelfContained`).
