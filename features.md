# Features – Geplante Tweaks (Stand September 2026)

Quelle: Web-Recherche (Winhance, WinUtil, 2026 Gaming-Guides) gefiltert nach
**nur belegbar wirksamen** Tweaks. Kein Placebo (kein `LargeSystemCache`,
kein `SystemResponsiveness=0`, kein `GPU Priority`, kein `useplatformclock true`,
kein SysMain-Off auf SSD, kein Search-Dienst-Off).

Umgesetzt: 1, 3, 4 (PCIe ASPM, siehe Hinweis), 5, 6, 7.
Bewusst nicht umgesetzt: MSI-Mode (50/50, siehe unten), Clean Taskbar/Start
(One-Shot-Aktionen ohne ehrliche State Detection), Home/Gallery-Namespace
(Keys löschen ohne sauberen Revert), AHCI/NVMe-Idle (hardwareabhängig, siehe 4).

## Bewusst NICHT aufgenommen

### MSI-Mode + Interrupt-Priority (GPU/USB/NIC)
50/50-Sache: kann helfen, kann verschlechtern. Moderne Hardware/Firmware setzt
MSI/MSI-X heute automatisch und korrekt. Manuelles Umschalten per Registry
(`MSISupported` + `Interrupt Priority`) ist riskant und oft ein No-Op.
Winchisel misst MSI-Status bereits auf der Latency-Seite – das reicht als
Diagnose, kein Toggle.

---

## 1. Customization-Kategorie (vollständig, Winhance/WinUtil-Parität) ✅ umgesetzt

Neue Performance-Gruppen `Taskbar`, `Start Menu`, `Explorer` (Katalog-IDs
`taskbar-*`, `start-*`, `explorer-*`). Alles HKCU mit Live-State-Detection,
Recommended/Default-Buttons und State-Badges wie der Rest des Katalogs.

Nicht umgesetzt: Clean Taskbar / Clean Start (One-Shot-Entpinnen ohne
State-Detection passt nicht ins Toggle-Modell) und Home/Gallery ausblenden
(Namespace-Keys löschen ohne sauberen Revert).

Neue Kategorie `Customize` mit den Untergruppen Taskbar, Start, Explorer,
Context Menu. Alles HKCU, sofort wirksam (ggf. Explorer-Restart), 100% realer
Verhaltenseffekt + RAM/CPU-Ersparnis (Widgets/WebExperience).

### 1.1 Taskbar
- Alignment Left/Center: `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced` → `TaskbarAl` DWORD 0/1 (Default 1)
- Widgets-Button aus: `TaskbarDa` 0/1 (Default 1)
- Search-Box/Highlights: `SearchBoxTaskbarMode` 0/1/2 + `SearchHighlights` 0/1
- TaskView-Button: `ShowTaskViewButton` 0/1
- Copilot-Icon: `ShowCopilotButton` 0/1 (nur wo vorhanden)
- End Task per Rechtsklick: `...Advanced\TaskbarDeveloperSettings` → `TaskbarEndTask` 0/1
- Clean Taskbar: Unpin alle Defaults + wendet Recommended-Taskbar-Settings an (wie Winhance)

### 1.2 Startmenü
- Bing-Websuche aus (nur lokal): `HKCU\Software\Policies\Microsoft\Windows\Explorer` → `DisableSearchBoxSuggestions` 0/1
- Recommended-Files/Recently added aus: `Start_TrackDocs` 0/1, `Start_TrackProgs` 0/1, `ShowRecentJumplists` 0/1
- Layout: `Start_Layout` 0/1/2 (Default/More pins/More recommendations)
- Clean Start Menu: alle Default-Pins + Bloat-Ghost-Icons entpinnen

### 1.3 Explorer
- Classic Context Menu (Win10-Stil): `HKCU\Software\Classes\CLSID\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\InprocServer32` = "" (an) / Key löschen (aus) + Explorer-Restart
- Dateiendungen zeigen: `HideFileExt` 0/1 (invertiert)
- Versteckte Dateien: `Hidden` 1/2
- LaunchTo This PC statt Home: `LaunchTo` 1/2
- Home/Gallery ausblenden (WinUtil-Parität)
- Menüleiste / Typing-Behavior / Sync-Provider-Hinweis wo sinnvoll

UI: wie Performance-Katalog (Suche, Toggle/Combo, Recommended/Default-Buttons,
State-Badges, Risk Safe). Fast alles HKCU → Risk Safe.

---

## 3. NIC Energiespar-Tweaks (Adapter Power Saving) ✅ umgesetzt

Als Special-Toggle `network-nic-power-saving`: es werden nur Werte angefasst,
die bereits existieren (nichts vendorspezifisches erzeugt), mit Snapshot/
Rollback wie jeder Katalog-Batch. Verfügbarkeits-Gate: ohne passenden
physischen Adapter ist der Toggle deaktiviert und Bulk-Profile überspringen
ihn.

Aktuell nur Nagle/Throttling/DNS vorhanden. Neu: pro-Adapter Special-Toggles
(dynamische Subkeys wie DNS, kein statischer Katalog):

- Energy Efficient Ethernet OFF
- Power Saving Mode / Green Ethernet OFF
- Interrupt Moderation OFF bzw. Low-Latency-Profil
- Wake-on-LAN / Power Management (Device kann Computer aufwecken) OFF für Gaming
- RSS (Receive Side Scaling) ON wo vorhanden

Pfad: `HKLM\SYSTEM\CurrentControlSet\Control\Class\{4D36E972-...}\<NNNN>` bzw.
`Set-NetAdapterAdvancedProperty` / `powercfg`. Realer Effekt: weniger
Latenz-Spikes/Jitter, v.a. Laptop/WLAN. State-Detection pro Adapter,
Bulk-Profile skippen wenn kein passender Adapter.

---

## 4. NVMe / PCIe Power Management gegen Stutter ✅ teilweise umgesetzt

Umgesetzt als Special-Toggle `power-pcie-link-state` (PCIe ASPM aus im
Netzbetrieb, nativ über Power-APIs, Akku-Verhalten unangetastet).
AHCI HIPM/DIPM und NVMe-Idle-Timeouts bewusst nicht: hardware-/treiberabhängig,
auf vielen Systemen gar nicht vorhanden – `powercfg`-Writes auf nicht
existierende Settings wären No-Ops ohne ehrliche State Detection.

- PCIe Link State Power Management OFF (nur Netzbetrieb): `powercfg /setacvalueindex <scheme> SUB_PCIEXPRESS <setting> 0`
- NVMe Idle Timeout / HIPM-DIPM OFF, AHCI Link Power Management OFF
- Als Special-Toggle auf aktivem Scheme + Winchisel-Scheme, mit Reboot-Hinweis wo nötig

Realer Effekt: weniger SSD-Microstutter beim Asset-Streaming (DirectStorage-Titel).
Beliebt in 2026 Storage-Guides, fehlt aktuell komplett.

---

## 5. NVIDIA Systemsteuerungs-Werte ✅ umgesetzt

Als Special-Toggles über die offizielle Driver Settings API (NVAPI DRS,
selbes Verfahren wie nvidiaProfileInspector, IDs aus NVIDIAs MIT-lizensierten
Headern): `graphics-nvidia-shader-cache` (10GB; Recommended bleibt Aus, da
aktuelle Treiber bereits 16GB als Default haben), `graphics-nvidia-power-max`
(Prefer Maximum Performance) und `graphics-nvidia-low-latency`
(pre-rendered frames = 1, entspricht Control Panel On). Vendor-Gating wie die
bestehenden GPU-Toggles; ohne NVIDIA-Treiber kommt eine klare Fehlermeldung
statt Stille. Reflex/Anti-Lag bleibt In-Game-Sache (kein Windows-Tweak).

Aktuell nur Legacy-Sharpening vorhanden. Neu (High Impact laut perfgamer 2026):

- Shader Cache Size = 10GB (verhindert Recompile-Stutter wenn Cache voll läuft)
- Power Management Mode = Prefer Maximum Performance
- Low Latency Mode = On (explizit NICHT Ultra – Ultra kann Input-Lag-Spikes geben)
- Hinweis-Kachel Reflex / Anti-Lag 2 ingame aktivieren (kein Registry-Tweak, nur Guidance)

Implementierung via NVAPI bzw. dokumentiertem Registry-Pfad (`HKCU/HKLM\Software\NVIDIA Corporation\Global\FTS`), Vendor-Gating wie bestehende GPU-Toggles (nur bei NVIDIA-GPU klickbar). State-Readback + Recommended/Default wie Katalog.

---

## 6. Ultimate Performance Plan Import ✅ umgesetzt

Zusätzlich zum eigenen Winchisel-Plan (Extras-Seite, zweite Power-Plan-Karte):

- Button `Ultimate Performance importieren`: `powercfg.exe /duplicatescheme e9a42b02-d5df-448d-aa00-03f14749eb61` → GUID aus Output parsen → `/setactive <GUID>`
- State: `powercfg /getactivescheme` gegen gespeicherte GUID (Muster wie `apply_winchisel_power_plan` in `src/platform/src/system.cpp`)
- Effekt: Core Parking aus, alle Kerne wach, weniger Power-State-Transitions → +3-5% AVG, deutlich bessere 1% Lows (Top-3 in allen 2026 Guides). User wollen explizit den MS-Stock-Plan.

---

## 7. Disable Dynamic Tick (`disabledynamictick`) ✅ umgesetzt

Als Extras-Toggle im HPET-Muster (Tri-State-Read, Write-Verifikation,
Reboot-Hinweis + Laptop-Hinweis in der Beschreibung).

- Toggle: `bcdedit.exe /set disabledynamictick yes` (an) / `bcdedit.exe /deletevalue disabledynamictick` (aus), Reboot nötig
- Tri-State-Read wie HPET (`bcdedit /enum {current}`), Verifikation nach Write
- Nur mit Mess-Hinweis + Undo anbieten (Latency-Seite / DPC-Check): stoppt Tick-Suspend im Idle, messbar weniger DPC-Spikes auf manchen Systemen, kostet Laptop-Akku

Kein `useplatformtick yes` (contested, kann Mouse-Stutter geben), kein `useplatformclock true` erzwingen (Placebo/schädlich – nur bestehendes `false`/Delete wie HPET-Toggle).
