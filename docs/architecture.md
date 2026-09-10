# Winchisel architecture

Winchisel is an unpackaged, native Windows 11 application. Its UI uses WinUI 3 with C++/WinRT; the application deliberately relies on Windows APIs and native tools rather than a cross-platform runtime.

## Technology

| Area | Choice |
| --- | --- |
| Language | Modern C++ |
| UI | WinUI 3 and C++/WinRT |
| Build | MSBuild, Visual Studio, and NuGet |
| Target | Windows 11 build 26100+ on x64 |
| Packaging | Inno Setup installer and a portable executable |
| Update validation | ECDSA P-256 signed manifest plus SHA-256 artifact checks |

## Layers

Dependencies point downward only:

```text
Winchisel.App          WinUI 3 pages, dialogs, window chrome, and presentation state
        |
Winchisel.Application  Feature workflows, navigation, settings coordination, and jobs
        |
Winchisel.Core         Data models, catalogs, settings schema, and pure logic
        |
Winchisel.Platform     Win32, registry, WMI, processes, command execution, and file I/O
```

`Winchisel.Core` must remain independent of WinUI types. `Winchisel.Platform` owns operating-system access. UI pages initiate work and render results; they must not directly perform registry, process, or command-line I/O.

## State and background work

- Persistent user settings are stored in `%APPDATA%\Winchisel\settings.json`.
- System tweak state remains in Windows itself, primarily the registry, services, scheduled tasks, and command-line tools.
- Page-only state such as search text, selection, expansion, and loading is kept in the respective page for the current session.
- Long-running scans and system operations run away from the UI thread and marshal completed results back through the DispatcherQueue.
- A feature prevents duplicate concurrent jobs while it is already loading or applying a change.

This keeps the interface responsive without continuously polling on the UI thread.

## Error handling

Platform code translates Win32 and process errors into clear feature-level results. The UI reports actionable failures in an InfoBar, toast, or dialog instead of hiding command output. Expected operating-system differences, such as an unavailable optional Windows component, are represented as state rather than treated as an application crash.

## Updates

The update service reads the latest GitHub release only as a discovery source (the release list when the user opted into nightly updates in Settings). Before an update is accepted, Winchisel verifies all of the following:

1. `release.json` has a valid ECDSA P-256 signature for the public key embedded in the application.
2. The offered version is newer than the running version.
3. The selected artifact matches the manifest's exact byte size and SHA-256 hash.

Installer builds delegate installation to the installer. Portable builds use the updater helper, which waits for the app to close, verifies the replacement, and retains a backup for recovery.

## Project layout

```text
src/app/          WinUI 3 application, pages, resources, and window chrome
src/application/  Application workflows and feature coordination
src/core/         Models, catalogs, localization keys, and pure logic
src/platform/     Windows API and system-integration implementations
src/updater/      Portable update helper
assets/           Application and installer visual assets
installer/        Inno Setup installer definition
tools/            Tests, release packaging, signing, and validation tools
docs/             Public project documentation
```

## Contribution guidelines

Keep new system access in `Platform`, retain existing cancellation and loading states, and make failures visible to the user. Changes to catalogs or update formats should include a matching validation or test update. Avoid introducing new dependencies when the Windows API or the C++ standard library already provides the needed capability.
