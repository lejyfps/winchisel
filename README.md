# Winchisel

<img src="assets/logo.png" width="96" alt="Winchisel logo" />

**Winchisel** is a native Windows 11 maintenance and optimization app built with WinUI 3 and C++/WinRT. From debloating to system tuning and troubleshooting, it gives you a clean, Windows-native interface for inspecting your system and applying clearly presented actions — with live state detection on every tweak.

[![GitHub release](https://img.shields.io/github/v/release/lejyfps/winchisel?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/lejyfps/winchisel/releases)
[![License: AGPL-3.0](https://img.shields.io/badge/License-AGPL--3.0-1793D1?style=for-the-badge)](LICENSE)

> [!WARNING]
> Many actions change Windows settings, services, or registry values. Review every option before applying it, and create a restore point before making broad system changes.

> [!NOTE]
> Winchisel is an independent, open-source project and is **not affiliated with, endorsed by, or associated with Microsoft** in any way. "Windows" is a registered trademark of Microsoft Corporation.

## Requirements 💻

- Windows 11, version 22H2 or later (build 22621+)
- x64 CPU
- Administrator permissions for actions that modify system-wide settings

## Installation 📥

### Download from GitHub Releases

[![Latest stable](https://img.shields.io/github/v/release/lejyfps/winchisel?style=for-the-badge&logo=windows&logoColor=white)](https://github.com/lejyfps/winchisel/releases)
[![Download stable](https://img.shields.io/badge/Download-latest%20stable-1793D1?style=for-the-badge&logo=github&logoColor=white)](https://github.com/lejyfps/winchisel/releases)

Grab the installer (machine-wide, requires admin) or the portable build from the [latest stable release](https://github.com/lejyfps/winchisel/releases). The portable build updates itself through signed update manifests.

### Nightly builds 🌙

[![Latest nightly](https://img.shields.io/github/v/release/lejyfps/winchisel?include_prereleases&style=for-the-badge&logo=windows&logoColor=white)](https://github.com/lejyfps/winchisel/releases)
[![Download nightly](https://img.shields.io/badge/Download-latest%20nightly-7A3E1F?style=for-the-badge&logo=github&logoColor=white)](https://github.com/lejyfps/winchisel/releases)

Feeling adventurous? Pre-release nightly builds (`1.0.9-nightly.20260910.1` style) ship the latest commits before they land in a stable release — pick the newest `Pre-release` from the list above. Enable **Settings → Nightly updates** and the built-in updater will offer them to you automatically. Nightlies are less tested than stable releases — turn the toggle off anytime to be offered the latest stable release again, even as a downgrade.

### Winget and Microsoft Store 🚧

Winget and Microsoft Store releases are in preparation and will be announced here once available.

## Current Features 🛠️

### Home 🏠

- Hardware and system dashboard with live CPU, memory, storage, display, and uptime information

### Debloater 🗑️

- 72-entry catalog across Windows apps, capabilities, and optional features
- Searchable interface with install, update, and remove actions

### Performance 🚀

- 171 tweaks across 16 groups: Gaming, Processor, Graphics, Network, Security, Xbox, System Services, Scheduled Tasks, Visual Effects, Accessibility, Power, Windows Update, Notifications, Taskbar, Start Menu, and Explorer
- Live state detection, per-tweak Recommended/Default quick actions, and Recommended/Default state badges
- Windows Update policy (automatic, security-only, paused, disabled), Delivery Optimization, and System Protection controls

### Privacy & Security 🔒

- 88 tweaks across 13 groups: Security, Ads, Lock Screen, General, Speech, Inking, Diagnostics, Activity History, Search, App Permissions, Windows AI, Edge AI, and Office AI
- Per-tweak Recommended/Default quick actions with state badges
- UAC level, Smart App Control, PowerShell policy, and bulk Recommended/Defaults profiles with preview

### Downloads 📦

- 176-entry package catalog with discovery and installation through winget

### Processes ⚙️

- Process inspection with CPU priority, I/O priority, and processor-affinity tools

### Latency 📶

- USB latency and topology analysis with a built-in latency probe

### Startup Manager & Scheduled Tasks ⏱️

- Autostart entries and boot/logon tasks with status, toggle, search, and filters

### Cleanup 🧹

- Disk cleanup across 6 safe categories (user/system temp, recycle bin, thumbnails, delivery optimization, shader caches) with per-category sizes
- 3 gated system categories (update cleanup via DISM, previous installations, prefetch): hidden in Store builds, require elevation otherwise
- Store builds additionally hide the user-temp, thumbnail, and shader-cache categories: packaged file virtualization would redirect those deletes into the private per-app location instead of freeing the real files
- Confirm dialog with total, live progress with cancel, per-file error report, automatic rescan

### Extras 🧰

- Extra maintenance controls, including the Winchisel and Ultimate Performance power plans, Long Paths, Developer Mode, Verbose Boot, HPET, and Dynamic Tick
- Per-tweak Recommended/Default quick actions

### Settings ⚙️

- 20 UI languages, light/dark/system themes, autostart, update checks (stable or nightly channel)
- Toggles for risk badges and state badges (NEW badges show automatically until the next release)
- One-click system restore point creation

## Safety 🛡️

- **Restore points:** create a Windows restore point from Settings before making changes
- **Atomic applies:** registry batches apply atomically with snapshot rollback on failure; every error reports its full Windows error code
- **Risk badges:** each tweak is labeled Safe, Moderate, or Risky; new tweaks carry a NEW badge until the next release
- **Nothing automatic:** every change happens only when you click it — no background modifications
- **Signed updates:** release manifests are ECDSA-signed and artifacts are SHA-256 verified before installing

## Why Winchisel? ❓

- **Truly native:** pure C++/WinRT with direct Windows API calls — no .NET runtime, no PowerShell scripts running your tweaks
- **Honest state:** every tweak reads back the real system state, so the UI shows what is actually set — not what was clicked last
- **Reversible by design:** per-tweak Windows-default buttons, bulk profiles, and snapshot rollbacks get you back to stock
- **No placebo catalog:** every entry maps to a documented setting, policy, service, or task with a real effect

## FAQ 💬

**Do I need administrator rights?**
Yes for anything system-wide (services, machine policies, device settings). Per-user settings work without elevation.

**Installer or portable?**
The installer puts Winchisel in Program Files for all users. The portable build runs from any folder and updates itself in place.

**How do I try nightly builds?**
Enable **Settings → Nightly updates**. Pre-release builds are then offered through the auto-updater; declining one ("Later") won't nag you on every restart. Turn the toggle off to switch back to the latest stable release.

**Does Winchisel collect data?**
No usage telemetry, no accounts, no phone-home. The only network access is the release check on startup plus automatic background update checks (both toggleable in Settings) plus downloads you explicitly start (winget packages, updates).

**How do I undo a change?**
Flip the toggle back, use the per-tweak Default button, apply a Defaults profile, or restore the system restore point you created first.

**Does it work on Windows 10?**
No — Winchisel targets Windows 11 22H2+ (build 22621+) on x64 only.

**When are winget/Store versions coming?**
They are in preparation; watch the releases page for the announcement.

## Build from source

The repository contains no solution file (it is maintainer-local). Build the projects directly from the repository root — `Winchisel.App` pulls in `Core` and `Platform` via project references:

```powershell
nuget restore src\app\packages.config -PackagesDirectory packages
msbuild src\app\Winchisel.App.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=${PWD}\ /m
msbuild src\updater\Winchisel.Updater.vcxproj /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=${PWD}\ /m
```

(`SolutionDir` must be absolute — the projects resolve shared headers and the output directory from it.)

The unpackaged executable is created at `out\x64\Debug\Winchisel.exe`. The project is configured as a self-contained Windows App SDK application, so the output includes the runtime files required to run it.

For development, install Visual Studio with the **Desktop development with C++** workload, a Windows 11 SDK, and NuGet.

## Tests

Test sources live in `tools/` (`core_tests.cpp`, `validate_catalogs.cpp`, `release_smoke.cpp`, `process_tests.cpp`, `update_tests.cpp`). The test runner script is maintainer-local and not part of the repository; there is no build CI on GitHub.

## Releases and updates

Release artifacts (installer, portable executable, Store package) are built locally by the maintainer and published on the releases page. The application validates the signed manifest, artifact size, and SHA-256 hash before installing an update. Besides stable releases (`1.2.3`, hotfixes like `1.2.3.1`), nightly pre-releases (`1.2.3-nightly.YYYYMMDD.N`) are published for testers who opt in via Settings.

## Project documentation

- [Architecture](docs/architecture.md)

## Support

- Report a bug: [GitHub Issues](https://github.com/lejyfps/winchisel/issues)
- Support development: [Donate via Pally](https://pally.gg/p/lejy)

## License

### Software License

Winchisel is licensed under the [AGPL-3.0](LICENSE).

In plain language:

- ✅ Free to use for everyone — individuals, businesses, IT professionals
- ✅ Use it to service and consult for clients
- ✅ Modify for your own internal use
- ✅ Fork and modify under AGPL-3.0 terms (must remain open source)
- ❌ Remove or circumvent license/copyright notices
- ❌ Rebrand redistributed versions as "Winchisel"

### Community guidelines

We respectfully ask contributors and forkers:

1. **Consider upstream contributions** — Pull requests help everyone
2. **Use distinct branding** for public forks — "Winchisel" is reserved
3. **Credit original work** — Link back to this repository

These are social expectations, not legal requirements. The AGPL-3.0 governs all rights and obligations.

### Trademark

“Winchisel” and the Winchisel logo are trademarks of lyrx2k. Derivative works must use a different name and branding.
