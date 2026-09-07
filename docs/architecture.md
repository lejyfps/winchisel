# Winchisel – Zielarchitektur (WinUI 3 / C++/WinRT)

Stand: Rewrite, funktional identisch zum Rust-Ist-Stand (0.1.8).

## Compiler und Sprache

| Entscheidung | Wert |
|---|---|
| Sprache | C++23 |
| Compiler | MSVC (VS 2026, Toolset v145 / 14.44+) |
| Flags | `/std:c++latest`, `/permissive-`, `/W4`, `/EHsc`, `/utf-8` |
| WinRT | C++/WinRT (`Microsoft.Windows.CppWinRT`) |
| UI | WinUI 3 über Windows App SDK **1.8** (`Microsoft.WindowsAppSDK`) |
| Architektur | x64 zuerst; ARM64 später, gleiche Quellen |

## Build-System

CMake 3.28+ ist das **Quell-of-Truth** für Core/Application/Platform (statische Libs, Tests).
Die WinUI-Schicht ist ein **VCXPROJ** (MSBuild), weil der XAML/MIDL-Compiler und NuGet-Targets des Windows App SDK dort zuverlässig sind.

- Generator: Visual Studio 18 2026 / MSBuild
- Presets: `windows-x64-debug`, `windows-x64-release`
- NuGet: PackageReference, Restore beim Build
- Deploy: **unpackaged** (`WindowsPackageType=None`), Bootstrap des Framework-Pakets zur Laufzeit

Unpackaged, nicht MSIX: die App muss elevated laufen. MSIX und `runas` vertragen sich schlecht; das entspricht dem heutigen EXE/MSI-Modell.

## Schichten

Abhängigkeiten nur nach unten. Keine umgekehrten Includes.

```
┌─────────────────────────────────────┐
│  UI  (Winchisel.App)                │  WinUI 3, XAML, Pages, Dialoge, Toasts
│  kennt: Application + Core-Typen    │
└─────────────────┬───────────────────┘
                  │
┌─────────────────▼───────────────────┐
│  Application                        │  Use-Cases, Navigation, Worker-Orchestrierung,
│  kennt: Core + Platform-Interfaces  │  Settings-Debounce, Update-Flow
└────────────┬────────────┬───────────┘
             │            │
┌────────────▼──────┐  ┌──▼──────────────────────────┐
│  Core             │  │  Platform                   │
│  keine WinUI,     │  │  Registry, Prozesse, netsh, │
│  keine HWND       │  │  winget, DISM, WMI, Datei-I/O│
│  Kataloge, Models,│  │  implementiert Ports        │
│  i18n-Keys, JSON  │  └─────────────────────────────┘
└───────────────────┘
```

- **Core** kompiliert ohne Windows-UI-Header. Darf `windows.h` nicht brauchen (reine Daten + Algorithmen). JSON-Schema von `settings.json` lebt hier.
- **Platform** kapselt alle Win32/COM/PowerShell-Aufrufe hinter schmalen Interfaces, die Application nutzt.
- **Application** kennt keine XAML-Typen (`Microsoft.UI.Xaml.*` verboten).
- **UI** enthält kein Registry-/Prozess-I/O.

## Fehlerbehandlung und Logging

- Core/Application: `std::expected<T, Error>` mit `Error { code, message_key, detail }`.
- Platform: Win32/`HRESULT` → `Error`. Keine Exceptions über Schichtgrenzen, außer C++/WinRT an der UI-Grenze (`winrt::hresult_error` fangen und in Toast/Dialog übersetzen).
- Logging: `OutputDebugStringW` + rotierende Datei unter `%APPDATA%\Winchisel\logs\` (eine Datei, max. klein). Kein Framework.
- Ungültige `settings.json` → Defaults, kein Toast (Ist-Verhalten).

## Datenmodell und Zustand

| Zustand | Ort | Lebensdauer |
|---|---|---|
| `settings.json` | Core-Modell, Platform I/O, Application Debounce 600 ms | Disk |
| Tweak-Kataloge | Core, Compile-Time (wie Rust) | Binary |
| Tweak-Ist-Werte | Windows-Registry via Platform | OS |
| UI-Ephemeral (Suche, Expand, Selection) | jeweilige Page | Sitzung |
| Download-Scan-Cache | Application, 600 s | Sitzung |
| Autostart | HKCU Run, abgeglichen beim Start | OS |

Kein eigenes DB-Format. Kein stilles Umschreiben von Registry-Tweaks beim Start.

## Threading und Async

- UI-Thread nur UI. DispatcherQueue für Marshal zurück.
- I/O und Scans: `winrt::resume_background` / Threadpool, Completion per Event/Callback — **kein 50-ms-Polling**.
- Lange Jobs (Repair, Latency, Restore, Downloads, Debloat-Scan): ein Job zur Zeit pro Feature; Button disable wie im Ist.
- Settings-Save: 600 ms Debounce auf dem UI-Thread, Write im Background.

## Performance-Ziele (erste Baseline, messen nach Minimalfenster)

| Metrik | Ziel (x64 Release, 24H2) |
|---|---|
| Zeit bis erstes Fenster (nach UAC) | ≤ 1,5 s auf Referenz-Desktop |
| Idle CPU nach Home-Load | ≈ 0 % (kein Polling) |
| Home-Refresh | 5 s, Arbeit off-UI |
| Arbeitsspeicher idle | unter dem Rust-Ist, nicht darüber |

Hot Paths: keine unnötigen Kopien, `std::span`/`string_view`, Move, UI-Updates gebündelt.
