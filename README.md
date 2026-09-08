# Winchisel

Winchisel is a native Windows 11 maintenance and optimization app built with WinUI 3 and C++/WinRT. It provides a Windows-native interface for inspecting your system and applying clearly presented maintenance, privacy, performance, and troubleshooting actions.

> [!WARNING]
> Many actions change Windows settings, services, or registry values. Review every option before applying it, and create a restore point before making broad system changes.

## Features

- Hardware and system dashboard with live CPU, memory, storage, display, and uptime information.
- Debloater for Windows apps, capabilities, and optional features.
- Performance and privacy controls with current-state detection.
- Package discovery and installation through winget.
- Process inspection, CPU priority, I/O priority, and processor-affinity tools.
- USB latency and topology analysis.
- Extra maintenance controls, including the Winchisel power plan.
- Windows 11-style light, dark, and system theme modes.
- Signed update manifests for portable and installer releases.

## Requirements

- Windows 11, version 24H2 or later (build 26100+)
- x64 CPU
- Administrator permissions for actions that modify system-wide settings

For development, install Visual Studio with the **Desktop development with C++** workload, a Windows 11 SDK, and NuGet. Inno Setup 6 is also required to create installer releases.

## Build from source

From the repository root, restore packages and build the Debug x64 target:

```powershell
nuget restore Winchisel.sln -PackagesDirectory packages
msbuild Winchisel.sln /p:Configuration=Debug /p:Platform=x64 /m
```

The unpackaged executable is created at `out\x64\Debug\Winchisel.exe`. The project is configured as a self-contained Windows App SDK application, so the output includes the runtime files required to run it.

## Tests

Run the repository checks from a Developer Command Prompt or a shell where the Visual Studio build tools are available:

```cmd
tools\run_tests.cmd
```

## Releases and updates

Release artifacts are built with `tools\release.cmd`. The complete local and GitHub release procedure, including signed update manifests, is documented in [docs/releasing.md](docs/releasing.md). The application validates the signed manifest, artifact size, and SHA-256 hash before installing an update.

## Project documentation

- [Architecture](docs/architecture.md)
- [Release process](docs/releasing.md)
- [Roadmap](todo.md)

## Support

- Report a bug: [GitHub Issues](https://github.com/lejyfps/winchisel/issues)
- Support development: [Donate via Pally](https://pally.gg/p/lejy)

## License

The repository currently does not include a license file. Do not assume that reuse or redistribution is permitted until a license is added.
