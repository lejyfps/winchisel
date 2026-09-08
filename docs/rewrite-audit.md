# Winchisel Rewrite-Audit: C++/WinUI 3 gegen Rust 0.1.8

Stand: 2026-09-08  
Geprüfte neue Codebasis: `D:\Zeugs\Codeing\Winchisel` (`master`)  
Primärreferenz: `D:\Zeugs\Codeing\winchisel-rust`, Revision `5ab8ee8`  
Ergänzende Bedienreferenz: <https://github.com/memstechtips/Winhance>

## Zweck und Einordnung

Dieses Dokument ist die Arbeitsliste für den schrittweisen Abgleich. Es unterscheidet zwischen:

- **PARITÄT**: Verhalten weicht vom Rust-Ist-Stand ab oder fehlt.
- **BUG**: Verhalten ist auch unabhängig von der Referenz fehlerhaft.
- **RISIKO**: Fehler ist plausibel, aber dynamisch noch zu reproduzieren.
- **QUALITÄT**: Tests, Wartbarkeit, Performance, Accessibility oder Release-Reife.

Die Prüfung war ein vollständiger statischer Review aller eingecheckten, selbst geschriebenen C++-/Header-/XAML-/IDL-/Installer-/Release-Dateien und der gesamten `todo.md`, ergänzt um gezielte Vergleiche mit allen Rust-Modulen. Generierte WinRT-/XAML-Dateien, Binärdateien und NuGet-Inhalte wurden nicht als eigener Anwendungscode bewertet. Der Debug-x64-Build wurde erfolgreich ausgeführt. Destruktive Tweaks, Updateinstallation, Deinstallation und Systemreparatur wurden aus Sicherheitsgründen nicht live ausgeführt; diese Punkte benötigen später VM-Tests.

## Fortschritt

Stand: **40 von 56 Auditpunkten behoben**, 16 offen. Erfolgreich behobene Punkte sind sowohl hier als auch direkt am jeweiligen Befund mit `[x]` markiert.

- [x] **Block 1 – Debloater-Parität:** AUD-004, AUD-005, AUD-006
- [x] **Block 2 – Settings/Persistenz:** AUD-011, AUD-012, AUD-013
- [x] **Block 3 – Exception/Handle/Event-Lifecycle:** AUD-010, AUD-017, AUD-018
- [x] **Block 4 – Extras-Zustände/Rollback:** AUD-025, AUD-027, AUD-029
- [x] **Block 5 – Sichere Systemaktionen:** AUD-028, AUD-040, AUD-041
- [x] **Block 6 – Updater Ende-zu-Ende:** AUD-001, AUD-002
- [ ] **Block 7 – Performance-Backend-Parität:** AUD-007 und AUD-008 verifiziert; AUD-009 offen
- [ ] **Block 8 – i18n und Encoding:** AUD-048 verifiziert; AUD-003 offen
- [ ] **Block 9 – Async, Cancellation und UI-Thread:** AUD-014 und AUD-031 behoben; AUD-015 und AUD-016 offen
- [x] **Block 10 – Netzwerk/Downloads:** AUD-019 bis AUD-024
- [ ] **Block 11 – Extras/Registry-Restpunkte:** AUD-026, AUD-030 und AUD-037 erledigt; AUD-036 offen
- [ ] **Block 12 – Prozesse:** AUD-034 und AUD-035 behoben; AUD-032 und AUD-033 offen
- [ ] **Block 13 – Logging/Diagnose/System Restore:** AUD-039 und AUD-042 behoben; AUD-038 offen
- [ ] **Block 14 – UI-Parität und Accessibility:** AUD-044, AUD-045 und AUD-047 behoben; AUD-043 und AUD-046 offen
- [ ] **Block 15 – Architektur/Tests/Release:** AUD-054 und AUD-056 behoben; AUD-049 bis AUD-053 und AUD-055 offen

## Kurzfazit

Der Rewrite kompiliert und die neun Seiten sind als native WinUI-3-Oberflächen vorhanden. Er ist aber noch **nicht funktionsidentisch** zum Rust-Stand und nicht releasefertig. Mehrere Aussagen in `todo.md` sind zu optimistisch. Die größten Blocker sind:

1. Der Update-Workflow ist in der UI und beim Start überhaupt nicht angebunden.
2. Deutsch/i18n ist faktisch nicht umgesetzt; der Sprachschalter speichert nur einen Wert.
3. AppX-Debloating verwendet andere, voraussichtlich falsche Install-/Remove-Kommandos.
4. Ein Teil der Performance-Katalogeinträge ist sichtbar, hat aber keinen funktionsfähigen Backend-Pfad.
5. System- und Registry-Aktionen sind nicht transaktional; Teilfehler hinterlassen Mischzustände.
6. Es gibt keine automatisierten Tests, keine ASan-Läufe und keine belegten Performance-/Leak-Messungen.

## P0 – Releaseblocker und kritische Abweichungen

### AUD-001 – Updateprüfung ist nicht angebunden

- Typ: **PARITÄT / BUG**
- [x] Status: **BEHOBEN IN BLOCK 6 (2026-09-08)** – Startup-Check, Bestätigungsdialog, verifizierter Download und Übergabe an Setup/Portable-Updater sind angebunden.
- Neu: `src/platform/src/update.cpp:97-107` implementiert Manifestprüfung und Download-Staging, hat aber außerhalb dieser Datei keine Aufrufer. `check_updates_on_startup` wird nur geladen/gespeichert (`src/app/SettingsPage.xaml.cpp:22,71`).
- Alt: `src/updater.rs` und `src/app/update.rs` bilden Check, Available-/Download-/Install-/Error-Zustände, 30-s-Cooldown sowie MSI-/Portable-Pfade ab.
- Folge: Der Schalter „Check updates on startup“ hat keine Wirkung; es gibt keinen manuellen Check, keinen Dialog, keinen Fortschritt und keinen Start des neuen Updaters.
- Korrektur: Einen Application-Service für Versionsvergleich, Cooldown, UI-Zustände, Artifact-Auswahl und `Winchisel.Updater.exe` anbinden; Startprüfung aus `Session`/App-Lifecycle auslösen.
- Test: Akzeptanzfall 20 vollständig für portable und installierte Variante, inklusive offline, 404, ungültiger Signatur, Hashfehler und Abbruch.

### AUD-002 – Versionsvergleich und installierte/portable Auswahl fehlen

- Typ: **PARITÄT**
- [x] Status: **BEHOBEN IN BLOCK 6 (2026-09-08)** – SemVer-Vergleich und Artifact-Auswahl `setup-x64`/`portable-x64` anhand des vom Portable-Host vererbten Pfads sind implementiert.
- Neu: `ReleaseManifest.version` wird gelesen, aber nirgends mit einer aktuellen Appversion verglichen. Ebenso existiert kein Aufrufer, der `portable-x64` oder Setup/MSI anhand des Installationsmodus auswählt.
- Alt: Vergleich mit `CARGO_PKG_VERSION`; separater MSI- und Portable-Ablauf (`src/updater.rs`).
- Folge: Selbst nach einfacher Anbindung würde jedes Release nur Metadaten liefern, ohne belastbare Entscheidung „neuer/gleich/älter“.

### AUD-003 – Sprachwechsel/i18n ist nicht umgesetzt

- Typ: **PARITÄT**
- Neu: Sichtbare Texte stehen nahezu vollständig hartcodiert auf Englisch in XAML und C++ (alle Pages, Dialoge, Status- und Fehlermeldungen). `Language` beeinflusst keine UI-Ressource.
- Alt: `src/i18n/en.rs` und `src/i18n/de.rs` enthalten jeweils über 1.300 Zeilen; Sprache wird unmittelbar auf alle sichtbaren Texte angewandt.
- Folge: Akzeptanzfall 5 ist nicht erfüllt; gespeichertes `German` erzeugt weiterhin eine englische App.
- Korrektur: `.resw`/MRT-Core-Ressourcen für EN/DE, stabile String-Keys und unmittelbares Rebinding/Recreate der sichtbaren Seite einführen. Auch Katalogname/-beschreibung, Dialoge, Accessibility-Namen und Backend-Fehler lokalisieren.

### AUD-004 – AppX-Debloater führt andere Aktionen als Rust aus

- Typ: **PARITÄT / BUG**
- [x] Status: **BEHOBEN IN BLOCK 1 (2026-09-08)** – AppX-Aktionen verwenden wieder die Rust-Semantik mit Aliasen, AllUsers und Manifestregistrierung; dynamischer VM-Test bleibt ausstehend.
- Neu: `src/platform/src/debloater.cpp:87` ruft `winget install/uninstall --name <package_name> --exact` auf.
- Alt: `src/app/debloater.rs:183-220` registriert vorhandene AppX-Manifeste mit `Add-AppxPackage` bzw. entfernt alle passenden All-Users-Pakete mit `Get-AppxPackage ... -AllUsers | Remove-AppxPackage`; Aliase werden berücksichtigt.
- Folge: Package-Family-Namen sind keine verlässlichen winget-Anzeigenamen. Install/Remove kann fehlschlagen, das falsche Paket treffen oder All-Users-Zustand ignorieren. Reinstall-Hinweise/Store-IDs des Katalogs werden nicht zur Aktion genutzt.
- Korrektur: Rust-Befehlssemantik portieren oder bewusst definierte PackageManager/PowerShell-Alternative verwenden; alle Aliase, AllUsers-Verhalten und Reinstall-Fälle einzeln testen.

### AUD-005 – AppX-Ist-Zustand hat eine andere Semantik

- Typ: **PARITÄT / BUG**
- [x] Status: **BEHOBEN IN BLOCK 1 (2026-09-08)** – Scan verwendet wieder `Get-AppxPackage` für den aktuellen Nutzer.
- Neu: `src/platform/src/debloater.cpp:60-66` durchsucht den maschinenweiten StateRepository-Cache.
- Alt: `Get-AppxPackage` listet die tatsächlich für den aktuellen Nutzer registrierten Pakete (`src/app/debloater.rs:647-649`).
- Folge: Provisionierte, gecachte, für andere Nutzer vorhandene oder nicht mehr registrierte Pakete können fälschlich als installiert erscheinen; umgekehrt können Zugriffsfehler alle Apps als nicht installiert darstellen.

### AUD-006 – Debloater-Scan kann niemals einen Fehler melden

- Typ: **BUG**
- [x] Status: **BEHOBEN IN BLOCK 1 (2026-09-08)** – Start- und Exitfehler aller drei parallelen PowerShell-Scans werden als Fehler an die UI propagiert.
- Neu: Fehlercodes der drei Scanprozesse werden in leere Sets umgewandelt (`lines()`); `scan_debloater_installed()` gibt trotzdem immer einen erfolgreichen `Result<vector<bool>>` zurück (`src/platform/src/debloater.cpp:69-78`). Die UI besitzt zwar „Scan failed“, dieser Pfad ist praktisch unerreichbar.
- Folge: Fehlendes DISM, Zugriff verweigert oder Prozessstartfehler zeigt irreführend „alles nicht installiert“.
- Korrektur: Exitcode, Startfehler und Parsefehler je Quelle typisiert propagieren; Teilresultate als „unbekannt“ statt `false` modellieren.

### AUD-007 – Performance-Einträge ohne Registry-Regel werden pauschal als Scheduled Task behandelt

- Typ: **BUG / PARITÄT**
- [x] Status: **DURCH VOLLSTÄNDIGEN COVERAGE-ABGLEICH KORRIGIERT (2026-09-08)** – Alle Toggle-IDs ohne Registryregel gehören zur Scheduled-Task-Gruppe und sind in `task_path` abgedeckt. Task-Lesefehler werden nun als Fehler statt als `false` propagiert.
- Neu: `PerformancePage::save_catalog_toggle()` ruft bei jedem Toggle ohne Registry-Regel `write_scheduled_task(item.id, enabled)` auf (`src/app/PerformancePage.xaml.cpp:259-261`). `write_scheduled_task` kennt jedoch nur eine begrenzte ID-Tabelle (`src/platform/src/performance.cpp:13-28`).
- Folge: Nicht-registrybasierte Spezial-Tweaks, die Dienste, PowerShell oder andere Windows-APIs benötigen, werden als unbekannte Task abgewiesen und zurückgesetzt. Sichtbar im Katalog bedeutet daher nicht funktionsfähig.
- Korrektur: Expliziten Backend-Typ pro Katalogeintrag generieren (`registry`, `service`, `task`, `dns`, `special`) und Exhaustiveness-Test hinzufügen. Keine implizite Fallback-Deutung.

### AUD-008 – Performance-Auswahlwerte sind nur für wenige IDs implementiert

- Typ: **PARITÄT**
- [x] Status: **DURCH VOLLSTÄNDIGEN COVERAGE-ABGLEICH KORRIGIERT (2026-09-08)** – Alle Selection-IDs sind abgedeckt: Registry-Sonderwerte, DNS, 35 Services sowie die zwei separat gerenderten Gaming-Auswahlen MouseHoverTime und BackgroundApps.
- Neu: `load_catalog_selections`/`save_catalog_selection` behandeln nur DNS, Win32PrioritySeparation, SvcHostSplitThreshold, VisualFXSetting und die hartcodierte Service-Liste (`src/app/PerformancePage.xaml.cpp:266-310`). Alle übrigen Selection-Einträge laufen in `continue`/`return`.
- Alt: `src/performance.rs` enthält individuelle Lese-/Schreiblogik und Profile für den vollständigen Katalog.
- Folge: Mehrere Dropdowns sehen bedienbar aus, ändern aber nichts; Quick Actions können die UI-Auswahl ändern, ohne den Systemzustand zu ändern.

### AUD-009 – Kein Rollback bei Multi-Registry-Tweaks und Profilen

- Typ: **BUG / DATENRISIKO**
- Neu: Multi-Target-Toggles, Privacy-Regeln, Browserregeln und Quick Actions schreiben nacheinander. Bei Fehler wird nur neu geladen; bereits erfolgreiche Schreibvorgänge bleiben bestehen (u. a. `PerformancePage.xaml.cpp`, `PrivacyPage.xaml.cpp`, `ExtrasPage.xaml.cpp:35-45`).
- Folge: Ein einzelner Toggle kann einen nicht definierten Mischzustand erzeugen. „Defaults“ oder „Recommended“ können halb angewandt sein, während die UI lediglich einen Fehler/Re-Load zeigt.
- Korrektur: Vorherwerte erfassen, atomare Action mit Rollback bzw. detailliertes Teilresultat; Profile mit Zusammenfassung und Wiederholbarkeit.

### AUD-010 – UnhandledException wird pauschal als behandelt markiert

- Typ: **BUG / RISIKO**
- [x] Status: **BEHOBEN IN BLOCK 3 (2026-09-08)** – Unbekannte UI-Ausnahmen werden geloggt/gemeldet, aber nicht mehr als sicher fortsetzbar markiert.
- Neu: `src/app/App.xaml.cpp:10-20` zeigt eine MessageBox und setzt `e.Handled(true)` für jede XAML-UnhandledException.
- Folge: Die App läuft nach beliebigen, möglicherweise zustandszerstörenden Ausnahmen weiter. Folgefehler und stille Datenkorruption sind wahrscheinlicher; Crashdiagnose wird erschwert.
- Korrektur: Nur klar erwartete, recoverable Ausnahmen behandeln; sonst strukturiert loggen und geordnet beenden/Crashdump ermöglichen.

## P1 – Hohe funktionale Abweichungen

### AUD-011 – Settings-Parser validiert kein JSON

- Typ: **PARITÄT / BUG**
- [x] Status: **BEHOBEN IN BLOCK 2 (2026-09-08)** – Das gesamte Dokument wird vor der kompatiblen Feldauswertung syntaktisch als JSON validiert; beschädigte Dateien fallen vollständig auf Defaults zurück.
- Neu: `src/core/src/settings.cpp:15-65` sucht Schlüssel per Stringsuche und akzeptiert Fragmente wie `"show_console": trueXYZ`, Duplikate und syntaktisch ungültiges JSON.
- Alt: `serde_json::from_str`; ungültige Datei führt vollständig zu Defaults.
- Folge: Beschädigte Settings werden teilweise übernommen statt definiert verworfen. Schlüsseltexte innerhalb anderer Strings können fehlinterpretiert werden.
- Korrektur: Echten JSON-Parser verwenden, Schema/Typen prüfen, unbekannte Felder tolerieren, aber bei Syntax-/Typfehlern das dokumentierte Default-Verhalten nutzen.

### AUD-012 – Settings werden nicht atomar geschrieben und Fehler verschwinden

- Typ: **BUG / DATENRISIKO**
- [x] Status: **BEHOBEN IN BLOCK 2 (2026-09-08)** – Schreiben erfolgt über `.tmp` plus atomaren, write-through Replace; Fehler werden an die Settings-Seite gereicht.
- Neu: `src/platform/src/system.cpp:173-184` öffnet die Zieldatei direkt mit `trunc`; `Session::set_settings` und Bootstrap ignorieren Fehler (`src/application/src/session.cpp:25,38`).
- Folge: Crash/Stromausfall kann eine leere/halbe Datei hinterlassen; UI behauptet implizit erfolgreiches Autosave. Dies widerspricht dem Logging-/Fehlerkonzept in `todo.md`.
- Korrektur: Neben-Datei schreiben, flush/close prüfen, atomar ersetzen; Fehler in der Settings-UI anzeigen und protokollieren.

### AUD-013 – Autostart-Fehler wird dem Nutzer nicht erklärt

- Typ: **PARITÄT / UX-BUG**
- [x] Status: **BEHOBEN IN BLOCK 2 (2026-09-08)** – Registry-/Speicherfehler setzen die Controls zurück und öffnen eine Fehler-InfoBar.
- Neu: Bei Registryfehler setzt `Session::set_settings` den bool zurück, verwirft aber Fehlerdetail und zeigt keinen Toast (`src/application/src/session.cpp:29-34`).
- Folge: Toggle springt scheinbar grundlos zurück.

### AUD-014 – Home-Abfragen laufen synchron auf dem UI-Thread

- Typ: **PERFORMANCE / HANG-RISIKO**
- [x] Status: **BEHOBEN IM ASYNC-MEHRFACHBLOCK (2026-09-08)** – Die vollständige Systemabfrage läuft per `resume_background`; nur das fertige Snapshot-Modell wird über die DispatcherQueue in die Controls übertragen. Ein Lauf-Guard verhindert überlappende 5-s-Abfragen, Timer und Callback halten die Page nicht künstlich am Leben.
- Neu: Konstruktor und 5-s-DispatcherTimer rufen `query_home_info()` direkt auf (`src/app/HomePage.xaml.cpp:14-21`). Dieses führt Registry-, DXGI-, Speicher-, Laufwerks- und weitere Systemabfragen aus (`src/platform/src/home.cpp`).
- Alt/Zielarchitektur: teure Abfragen im Worker; UI-Thread nur UI.
- Folge: periodische UI-Ruckler oder Hänger bei langsamen Treibern/Laufwerken. Das verletzt die eigene Architekturleitplanke.
- Korrektur: Snapshot im Hintergrund aktualisieren, Cancellation/Generation-ID verwenden, nur gebündelte UI-Zuweisung dispatchen.

### AUD-015 – Kindprozesse besitzen weder Timeout noch Cancellation

- Typ: **BUG / RESOURCE-RISIKO**
- Betroffen: Debloater, Downloads, Performance, Latency, Reparatur, Cleanup, Extras (`WaitForSingleObject(..., INFINITE)` in den Platform-Dateien).
- Folge: Hängt winget/DISM/netsh/powercfg, bleibt Operation bzw. App-Shutdown unbegrenzt blockiert. UI-Seiten-Futures können beim Zerstören blockieren.
- Korrektur: RAII-Prozessobjekt, Job Object mit Kill-on-close, Zeitlimit, Cancel-Button und definierter Shutdown.

### AUD-016 – `std::async` plus Page-Lebenszyklus kann Navigation/Shutdown blockieren

- Typ: **PERFORMANCE / LIFECYCLE-RISIKO**
- Neu: Debloater, Downloads, Latency und Settings halten `std::future` als Member. Destruktoren stoppen Timer, brechen die Arbeit aber nicht ab. Ein Future aus `std::async(std::launch::async)` kann bei Zerstörung auf Abschluss warten.
- Folge: Navigation oder App-Ende kann bis zum Ende von winget/DISM/Analyse hängen.
- Korrektur: cancellable Task-Service außerhalb kurzlebiger Page-Objekte; `std::jthread`/Stop-Token oder WinRT-Async mit sauberem Ownership-Modell.

### AUD-017 – Timer-Event in Latency wird nicht deregistriert

- Typ: **MEMORY/LIFECYCLE-RISIKO**
- [x] Status: **BEHOBEN IN BLOCK 3 (2026-09-08)** – Weak Capture, gespeicherter Event-Token und explizite Deregistrierung ergänzt.
- Neu: `LatencyPage` registriert `timer_.Tick([this]...)` ohne Token und stoppt im Destruktor nur den Timer (`src/app/LatencyPage.xaml.cpp:19-25`).
- Folge: Gegenüber den anderen Pages ist der Handler-Lifecycle nicht explizit gelöst; je nach Event-Ownership drohen Referenz-/Callback-Probleme.
- Korrektur: Token speichern und im Destruktor entfernen; vorzugsweise Weak-Reference capturen.

### AUD-018 – Crypto-Handles lecken auf frühem Fehlerpfad

- Typ: **MEM-LEAK / HANDLE-LEAK**
- [x] Status: **BEHOBEN IN BLOCK 3 (2026-09-08)** – Algorithmus-/Key-Handles werden auch bei Import-/Property-Fehlern freigegeben.
- Neu: `verify_signature()` (`src/platform/src/update.cpp:58-75`) kehrt sofort zurück, wenn `BCryptImportKeyPair` nach erfolgreichem `BCryptOpenAlgorithmProvider` fehlschlägt. `algorithm` wird dann nicht geschlossen; analoge Property-/Create-Fehler sind nur teilweise geprüft.
- Korrektur: RAII-Wrapper für `BCRYPT_ALG_HANDLE`, `BCRYPT_KEY_HANDLE`, `BCRYPT_HASH_HANDLE`; jeden NTSTATUS prüfen.

### AUD-019 – WinHTTP ohne explizite Timeouts, Größenlimit und robuste URL-Pufferung

- Typ: **NETZWERK / DOS-RISIKO**
- [x] Status: **BEHOBEN IN BLOCK 10 (2026-09-08)** – Connect/Send/Receive-Timeouts sowie 4-MiB-Metadaten- und signiertes Artifact-Größenlimit ergänzt.
- Neu: `get_https()` setzt keine Connect/Send/Receive-Timeouts, liest Antwort unbegrenzt in RAM und nutzt feste Host-/Pfadpuffer (256/4096) (`src/platform/src/update.cpp:28-42`).
- Folge: Updatecheck kann lange hängen; übergroße Antwort kann hohen Speicherverbrauch erzeugen; lange URL schlägt unnötig fehl.
- Korrektur: Timeouts, maximale Manifest-/Artifactgröße vor/allokationsbegleitend, Redirect-/TLS-/Statusregeln und dynamische URL-Komponenten.

### AUD-020 – Downloads- und Debloater-Kommandos verwenden ANSI

- Typ: **BUG / KOMPATIBILITÄT**
- [x] Status: **BEHOBEN IN BLOCK 10 (2026-09-08)** – Downloads, Debloater, Performance und Latency verwenden durchgehend `CreateProcessW`.
- Neu: `CreateProcessA` und `GetTempFileNameA` in `download.cpp`, `debloater.cpp`, `performance.cpp`, `latency.cpp`.
- Folge: Nicht-ASCII-Pfade, Nutzerprofile, Paketnamen oder lokalisierte Ausgaben können falsch verarbeitet werden. Der Rest der App ist Unicode.
- Korrektur: durchgehend `CreateProcessW`, UTF-16-Argumente und klar definierte Output-Decodierung (Console-Codepage/UTF-8).

### AUD-021 – `winget list` wird als unstrukturiertes Textformat geparst

- Typ: **BUG / LOKALISIERUNG**
- [x] Status: **BEHOBEN IN BLOCK 10 (2026-09-08)** – winget wird nicht mehr über sein lokalisiertes Tabellenformat ausgewertet; IDs kommen exakt aus Export-JSON, klassische Programme ergänzend aus der Registry.
- Neu: Jede Ausgabezeile wird in ein Set gelegt und per Substring gegen Programmnamen geprüft (`src/platform/src/download.cpp:39-46,90-105`).
- Folge: Tabellenkopf, abgeschnittene Spalten, lokalisierte Ausgabe und ähnliche Namen erzeugen False Positives/Negatives.
- Korrektur: primär `winget export`-IDs plus Registry-Metadaten strukturiert auswerten; unstrukturiertes CLI-Tabellenformat nicht als Daten-API verwenden.

### AUD-022 – Download-Scan meldet Quellfehler nicht

- Typ: **BUG**
- [x] Status: **BEHOBEN IN BLOCK 10 (2026-09-08)** – Start-/Exitfehler des strukturierten winget-Exports werden an die vorhandene UI-Fehleranzeige propagiert.
- Neu: Fehlgeschlagener winget-export/list oder Registryzugriff wird in ein leeres Set umgewandelt; `scan_downloads_installed()` liefert immer Erfolg.
- Folge: Die vorhandene UI-Fehleranzeige ist weitgehend unerreichbar und Programme erscheinen als nicht installiert.

### AUD-023 – App-Installationen laufen strikt seriell und ohne Einzelfehlerdetails

- Typ: **PARITÄT / UX**
- [x] Status: **BEHOBEN IN BLOCK 10 (2026-09-08)** – Fehler enthalten Paketname und Exit-/Startursache. Serielle Installation bleibt bewusst bestehen, da parallele winget-Installationen am gemeinsamen Package-Manager konkurrieren können.
- Neu: `install_downloads()` installiert nacheinander und liefert nur Zähler (`src/platform/src/download.cpp:108-117`). Output/Exitcode pro Paket geht verloren.
- Folge: Nutzer kann fehlgeschlagenes Paket und Ursache nicht erkennen oder gezielt wiederholen.

### AUD-024 – Website-Aktion prüft URL und ShellExecute-Ergebnis nicht

- Typ: **BUG / SECURITY-HARDENING**
- [x] Status: **BEHOBEN IN BLOCK 10 (2026-09-08)** – Nur HTTPS wird geöffnet; ShellExecute-Fehler erscheinen in der InfoBar.
- Neu: Katalog-URL wird direkt an `ShellExecuteW` übergeben; Resultat wird ignoriert (`src/app/DownloadsPage.xaml.cpp:51`).
- Korrektur: nur `https` erlauben, Katalog beim Build validieren, Rückgabewert prüfen und Fehler anzeigen.

### AUD-025 – Extras: Browserzustand und geschriebene Regelmenge sind inkonsistent

- Typ: **BUG**
- [x] Status: **BEHOBEN IN BLOCK 4 (2026-09-08)** – Brave und Edge prüfen nun dieselbe vollständige Regelmenge, die beim Aktivieren geschrieben wird.
- Neu: Brave wird beim Laden nur anhand von 5 Regeln als aktiv erkannt, beim Schreiben werden 12 Regeln verändert (`src/app/ExtrasPage.xaml.cpp:23-24,43`). Edge-Zustand prüft weder EdgeUpdate-Regel noch Extension-Blocklist, obwohl diese geschrieben werden.
- Folge: UI kann „aktiv“ zeigen, obwohl nur ein Teilprofil vorliegt; beim Deaktivieren werden vorhandene, eventuell vom Admin gesetzte Policies gelöscht.
- Korrektur: exakt gleiche vollständige Regelmenge lesen/schreiben; Fremdwerte sichern bzw. Ownership definieren.

### AUD-026 – Extras deaktivieren löscht vorher existierende Richtlinien

- Typ: **DATENRISIKO / PARITÄT PRÜFEN**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Brave-/Edge-Aktivierung sichert jeden vorherigen DWORD-/String-Zustand einmalig unter dem Winchisel-Backup-Key. Ausschalten stellt vorhandene Werte wieder her oder löscht nur Werte, die vorher fehlten; ältere Installationen ohne Backup behalten den bisherigen Delete-Fallback.
- Neu: Brave/Edge „Off“ löscht sämtliche betroffenen Werte, ohne vorherigen Zustand zu kennen.
- Folge: Unternehmens-/Nutzerpolicies können unwiederbringlich entfernt werden. Gleiches Grundproblem gilt für mehrere Registry-Defaults.
- Korrektur: Semantik gegen Rust für jede Regel verifizieren; mindestens deutliche Warnung und Snapshot/Restore statt blindem Löschen.

### AUD-027 – Teredo-Operation ist nicht atomar

- Typ: **BUG**
- [x] Status: **BEHOBEN IN BLOCK 4 (2026-09-08)** – Der vorherige `DisabledComponents`-Wert wird bei einem nachfolgenden netsh-Fehler zurückgeschrieben.
- Neu: Erst wird `DisabledComponents` geändert, anschließend `netsh` ausgeführt (`src/platform/src/system.cpp:436-447`). Scheitert `netsh`, bleibt Registry geändert, UI zeigt Fehler.
- Korrektur: Vorwert sichern und bei Folgefehler rückrollen; Neustartanforderung ausweisen.

### AUD-028 – HPET „Default“ erzwingt `useplatformclock true`

- Typ: **PARITÄT / SYSTEMRISIKO**
- [x] Status: **BEHOBEN IN BLOCK 5 (2026-09-08)** – Deaktivieren des Tweaks entfernt den BCD-Wert und überlässt die Clock-Auswahl wieder Windows.
- Neu und Alt verwenden zwar on/off, aber ein echtes Windows-Default wäre in vielen Installationen das Entfernen des BCD-Eintrags (`bcdedit /deletevalue useplatformclock`) statt `true` (`src/platform/src/system.cpp:449-451`).
- Folge: „Disable aus“ ist nicht zwingend „Windows-Standard“. Dies war wahrscheinlich bereits ein Altverhalten und muss bewusst entschieden, nicht als Default bezeichnet werden.

### AUD-029 – Widgets-Ist-Zustand prüft nur, ob ein allgemeiner Repository-Key geöffnet werden kann

- Typ: **BUG**
- [x] Status: **BEHOBEN IN BLOCK 4 (2026-09-08)** – Der Package-Cache wird explizit nach `MicrosoftWindows.Client.WebExperience` durchsucht.
- Neu: `read_extras_command_state()` setzt `widgets_removed=true`, wenn der allgemeine Package-Data-Key nicht geöffnet werden kann (`src/platform/src/system.cpp:480-483`). Es wird nicht nach dem Windows Web Experience Pack gesucht.
- Folge: Registry-Berechtigungsfehler oder beschädigter Repositoryzugriff wird als „Widgets entfernt“ angezeigt; bei zugänglichem Key praktisch immer „nicht entfernt“.

### AUD-030 – Powerplan-Erkennung ist lokalisierungsabhängig

- Typ: **BUG**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Importierte GUID wird gespeichert und mit `powercfg /getactivescheme` verglichen; Planname und Ausgabesprache sind irrelevant.
- Neu: sucht im Text von `powercfg /list` nach `(Winchisel)` und `*` (`src/platform/src/system.cpp:478-480`).
- Folge: Ausgabeformat/Lokalisierung/Namensvarianten können falschen Zustand liefern. GUID sollte persistiert/strukturiert ermittelt werden.

## P1 – Prozesse, Privacy, Registry und Systemaktionen

### AUD-031 – Prozessseite führt teure Vollabfrage auf dem UI-Thread aus

- Typ: **PERFORMANCE**
- [x] Status: **BEHOBEN IM ASYNC-MEHRFACHBLOCK (2026-09-08)** – Toolhelp-Snapshot, Prozesshandles, Zeiten, Pfade, Priorität und Affinity werden in einem guarded Background-Lauf gesammelt. Erst der fertige Snapshot wird schwach referenziert auf dem UI-Thread übernommen und gerendert.
- Neu: `load_processes()` erstellt Toolhelp-Snapshot, öffnet Prozesse, liest Pfade/Zeiten und rendert die gesamte Tabelle aus dem 2-s-UI-Timer (`src/app/ProcessesPage.xaml.cpp:29-93`).
- Folge: Bei vielen Prozessen entstehen regelmäßige UI-Pausen und große Control-Allokationen.
- Korrektur: Datenerfassung und Sortierung im Worker, UI-Diff/Virtualisierung statt komplettes Neurendern.

### AUD-032 – Prozess-CPU-Wert ist gegenüber Rust zu verifizieren

- Typ: **PARITÄT / TESTLÜCKE**
- Neu berechnet CPU aus Prozess-/Systemzeit-Snapshots in eigener Logik; Rust nutzt sein bestehendes Modell samt Active/User-Filter und Labelcache (`src/app/processes.rs`).
- Erforderlich: Auf 1/64+ logischen CPUs, kurzlebigen Prozessen, PID-Reuse und suspendierten Prozessen vergleichen; Prozentdefinition (Gesamtsystem vs. ein Kern) festschreiben.

### AUD-033 – Affinity unterstützt nur eine Prozessgruppe/64 Bits

- Typ: **BUG / SKALIERBARKEIT**
- Neu iteriert nur über `sizeof(DWORD_PTR)*8` und nutzt `Get/SetProcessAffinityMask` (`src/app/ProcessesPage.xaml.cpp:97,112,119-123`).
- Folge: Systeme mit mehr als 64 logischen Prozessoren/mehreren Processor Groups werden unvollständig behandelt.
- Korrektur: CPU Sets/Processor Groups berücksichtigen oder Einschränkung klar anzeigen.

### AUD-034 – „Always“-Regeln sind nur Setzen, kein sauberer Restore-Workflow

- Typ: **PARITÄT / UX**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Beide „Always“-Menüs besitzen einen expliziten Default-Eintrag; er entfernt den jeweiligen IFEO-Wert und räumt leere `PerfOptions`-/Image-Keys auf. Fehler und Erfolg werden sichtbar gemeldet.
- Neu: `set_always()` schreibt IFEO/PerfOptions; ein allgemeiner Lösch-/Defaultpfad ist nicht ersichtlich (`src/app/ProcessesPage.xaml.cpp:99-100`).
- Folge: Dauerhafte Prioritäten können schwer rückgängig zu machen sein; leere PerfOptions-Schlüssel bleiben möglich.
- Korrektur: explizites „Default/Regel entfernen“, Wert- und Key-Cleanup, Bestätigung und Fehlerdetail.

### AUD-035 – Undokumentierte ProcessInformation-Klasse als Magic Number

- Typ: **WARTBARKEIT / KOMPATIBILITÄT**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Die InformationClass ist als `ProcessIoPriority` benannt und an Set/Query konsistent.
- Neu: I/O-Priorität verwendet `33` direkt für Set/NtQuery (`src/app/ProcessesPage.xaml.cpp:98,101`).
- Folge: schlecht prüfbar und SDK-/OS-fehleranfällig.
- Korrektur: benannten kompatiblen Typ/Wrapper, statische Größenprüfung und OS-Fehlerausgabe verwenden.

### AUD-036 – Privacy-Sonderzustände und Profile brauchen vollständigen Referenztest

- Typ: **PARITÄT / TESTLÜCKE**
- Neu: UAC, Smart App Control, PowerShell Policy und Ads-Modus sind separate UI-Sonderfälle neben generierten Registry-Regeln (`src/app/PrivacyPage.xaml.cpp`).
- Risiko: Einzelregel-UI, abhängige Regeln und Quick Profiles können unterschiedliche Wahrheitsdefinitionen verwenden; Smart App Control ist nicht beliebig reversibel.
- Korrektur: pro Rust-ID Golden-Test für Read, Enable, Disable, Recommended, Default und Mixed State. Irreversible/OS-gesteuerte Optionen ausdrücklich warnen.

### AUD-037 – Registry-Views sind nicht explizit

- Typ: **PARITÄT / KOMPATIBILITÄT**
- [x] Status: **FÜR DEN FESTGELEGTEN SCOPE VERIFIZIERT (2026-09-08)** – Alle Projekte und Releaseartefakte sind ausschließlich x64; native x64-Registryview ist damit definiert. ARM64 bleibt laut `todo.md` ein späteres Architekturthema.
- Neu: Registryzugriffe verwenden überwiegend nur `KEY_READ/KEY_WRITE`, ohne `KEY_WOW64_64KEY`/`KEY_WOW64_32KEY` (`src/platform/src/registry.cpp` und direkte Page-Zugriffe).
- Folge: x64 funktioniert derzeit meist erwartbar; ARM64/32-bit-Zukunft und explizite WOW6432-Semantik sind nicht festgelegt. Downloadscan kompensiert nur teilweise manuell.

### AUD-038 – Fehlerdetails verschwinden an vielen UI-Grenzen

- Typ: **QUALITÄT / SUPPORTABILITY**
- Beispiele: Extras zeigt nur „Could not apply the setting“, Performance lädt still zurück, Autostart springt zurück, Paketaktionen liefern nur Zähler.
- Folge: Fehler sind weder für Nutzer noch Support reproduzierbar; Ziel „reproduzierbar protokollieren“ ist nicht erfüllt.

### AUD-039 – Boot-Log liegt im Temp-Ordner statt im dokumentierten Logpfad

- Typ: **PARITÄT ZUR ZIELARCHITEKTUR**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Thread-sicheres, zeitgestempeltes Log unter `%APPDATA%\Winchisel\logs\winchisel.log` umgesetzt.
- Neu: `%TEMP%\winchisel-boot.log` (`src/platform/src/system.cpp:199-212`).
- Todo/Architektur: `%APPDATA%\Winchisel\logs\` plus Debug-Ausgabe.
- Folge: Kein einheitliches Log, keine Rotation, mögliche Vermischung paralleler Läufe.

### AUD-040 – Temp-Cleanup verschluckt alle Einzel- und Enumerationsfehler

- Typ: **BUG / UX**
- [x] Status: **BEHOBEN IN BLOCK 5 (2026-09-08)** – Lösch-/Enumerationsfehler werden gesammelt und als Partial Failure gemeldet; Permission-Denied wird kontrolliert behandelt.
- Neu: `remove_temp_files()` löscht rekursiv, löscht `error_code` nach jedem Eintrag und gibt am Ende immer Erfolg zurück (`src/platform/src/system.cpp:352-373`).
- Folge: „Erfolg“ trotz zahlreicher nicht gelöschter Dateien; Junction/Reparse-Point-Verhalten ist nicht explizit abgesichert.
- Korrektur: Reparse Points nicht traversieren, erlaubte Roots kanonisch prüfen, Fehler zählen/anzeigen und Partial Success modellieren.

### AUD-041 – Disk Cleanup ist funktional erweitert, aber nicht als Abweichung dokumentiert

- Typ: **PARITÄT / PRODUKTENTSCHEIDUNG**
- [x] Status: **BEHOBEN IN BLOCK 5 (2026-09-08)** – Das versteckte DISM `/ResetBase` wurde entfernt; die Aktion entspricht wieder dem Rust-Workflow.
- Neu: Nach `cleanmgr /VERYLOWDISK` läuft zusätzlich DISM `/StartComponentCleanup /ResetBase` (`src/platform/src/system.cpp:345-350`).
- Alt/Todo: Disk Cleanup startet `cleanmgr`.
- Folge: `/ResetBase` ist wesentlich invasiver und verhindert die Deinstallation bereits installierter Updates. Das ist keine harmlose Rewrite-Äquivalenz.
- Korrektur: Aus Standardworkflow entfernen oder als getrennte, deutlich bestätigte Aktion dokumentieren.

### AUD-042 – Restore Point beendet BEGIN bei Zwischenfehler nicht garantiert

- Typ: **RISIKO**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – BEGIN und END verwenden dieselbe gesicherte Sequenznummer; beide Phasen protokollieren Phase, Sequenz und Win32-/Statusfehler. Nach erfolgreichem BEGIN wird END unmittelbar ausgeführt und ein END-Fehler detailliert zurückgegeben.
- Neu: BEGIN und END werden direkt nacheinander gesetzt (`src/platform/src/system.cpp:315-330`). Bei END-Fehler gibt es keine weitere Recovery/Diagnose.
- Korrektur: Windows-System-Restore-Semantik gegen Referenz/API-Dokumentation testen; Sequenznummer und Fehlerstatus vollständig loggen.

## P2 – UI, Accessibility, Architektur und Wartbarkeit

### AUD-043 – Keine Toast-Infrastruktur wie im Rust-Ist-Stand

- Typ: **PARITÄT**
- Neu: überwiegend lokale `InfoBar`, StatusText oder MessageBox. Globale Toasts unten rechts und ihr einheitlicher Success/Error/Warning-Lifecycle fehlen.
- Folge: Workflows und Feedback unterscheiden sich; Seitenwechsel kann Rückmeldung verlieren.

### AUD-044 – Fenster wird nicht zentriert

- Typ: **PARITÄT**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Startposition wird anhand der WorkArea des aktuellen Displays zentriert.
- Neu: nur `Resize({1280,720})` und Minimum (`src/app/MainWindow.xaml.cpp:51-58`).
- Alt: Startfenster zentriert.

### AUD-045 – Jede Navigation konstruiert Pages neu und verwirft Zustand

- Typ: **PARITÄT / PERFORMANCE**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – MainWindow cached jede Page-Instanz; Suche, Auswahl, Scroll-/Expander- und Ergebniszustände bleiben erhalten.
- Neu: `MainWindow::Nav_SelectionChanged` setzt jeweils `Content(make<Page>())` (`src/app/MainWindow.xaml.cpp:74-95`).
- Folge: Suchtext, Expand/Collapse, Auswahl, Scrollposition und laufende Resultate gehen beim Seitenwechsel verloren; teure Scans starten erneut. Rust hält Seitenzustände in der App-Struktur.
- Korrektur: Frame-Navigation mit Cache oder ViewModels/Application-Services mit klarer State-Lebensdauer.

### AUD-046 – Accessibility ist nicht systematisch umgesetzt

- Typ: **QUALITÄT / PARITÄT**
- Befund: Dynamisch erzeugte Toggles, Icon-/Expand-Buttons, Prozesskontextmenüs und Statusupdates besitzen keine systematische `AutomationProperties.Name/HelpText`, LiveRegion oder Focus-Strategie.
- Folge: Todo-Punkte zu Tastaturbedienung/Accessibility sind offen und die dynamischen Controls sind für Screenreader schwer verständlich.

### AUD-047 – Keine sichtbare Tastatur-/Shortcut-Parität

- Typ: **PARITÄT / TESTLÜCKE**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Die neun Hauptseiten sind zentral über `Ctrl+1` bis `Ctrl+9` erreichbar; die Accelerators setzen denselben NavigationView-Zustand wie Mausauswahl und werden vom Framework als behandelt markiert.
- Befund: Keine zentrale Accelerator-/Shortcut-Implementierung; Dialog-Fokus, ListView-Mehrfachauswahl und Kontextaktionen sind nicht als Tastaturworkflow getestet.

### AUD-048 – UI enthält Mojibake im eingecheckten Quelltext

- Typ: **BUG / ENCODING**
- [x] Status: **DURCH BYTE-/UTF-8-PRÜFUNG KORRIGIERT (2026-09-08)** – Die vermeintlichen Zeichen waren eine Konsolendecodierung des ersten Reviews; in den Quelldateien liegen korrekte UTF-8-Zeichen vor.
- Beispiele: `src/application/src/session.cpp` enthält `â€”`; `src/app/HomePage.xaml.cpp` enthält `Â·` und `â€”`.
- Folge: Trotz `/utf-8` werden diese bereits falsch gespeicherten Bytes als sichtbare falsche Zeichen ausgegeben.
- Korrektur: Dateien als korrektes UTF-8 normalisieren und Encoding-Test/Resource-Lokalisierung nutzen.

### AUD-049 – Starke Logikduplizierung und Minified-One-Line-C++

- Typ: **WARTBARKEIT / FEHLERRISIKO**
- Befund: Prozessstart/Pipe/Handle-Logik ist mehrfach kopiert; Registryzugriffe liegen teils im Platform-Layer, teils direkt in Pages. Mehrere Dateien bestehen aus extrem langen Einzeilern (`performance.cpp`, `DownloadsPage.xaml.cpp`, `ExtrasPage.xaml.cpp`, `ProcessesPage.xaml.cpp`).
- Folge: RAII, Fehlerbehandlung, Unicode und Timeouts werden inkonsistent; Reviews und gezielte Fixes sind unnötig riskant.
- Korrektur: gemeinsame `ProcessRunner`-/Registry-/Command-Action-Abstraktionen im Platform-Layer, formatierten Code erzwingen.

### AUD-050 – Schichtentrennung wird durch direkte Win32-Logik in Pages verletzt

- Typ: **ARCHITEKTUR**
- Beispiele: Extras schreibt Registry direkt; Processes öffnet Prozesse und IFEO-Registry direkt; Downloads öffnet URLs direkt.
- Folge: Geschäfts-/Plattformlogik ist nicht vollständig von WinUI getrennt, entgegen `todo.md` und `docs/architecture.md`; Unit-/Integrationstests werden erschwert.

### AUD-051 – Generierte Kataloge haben keinen nachvollziehbaren Generator im Repository

- Typ: **REPRODUZIERBARKEIT**
- Neu: `*_catalog.generated.hpp` und `latency_database.generated.hpp` sind eingecheckt, aber kein Generator/Mappingtest ist vorhanden.
- Folge: Änderungen am Rust-Referenzkatalog können nicht reproduzierbar neu erzeugt oder auf Verlust geprüft werden.
- Korrektur: Generator plus Golden-Dateien/Counts/ID-Uniqueness/Backend-Coverage einchecken.

### AUD-052 – Keine automatisierten Tests vorhanden

- Typ: **QUALITÄT / RELEASEBLOCKER**
- Befund: Keine Testprojekte oder Testquellen. Die in `todo.md` abgeleiteten 21 Akzeptanzfälle sind nicht automatisiert oder protokolliert.
- Erforderlich: Core-Unit-Tests, Registry-/Settings-Integrationstests mit isolierten Testkeys, Platform-Command-Fakes, Katalog-Golden-Tests und WinUI-Smoke-Tests.

### AUD-053 – Kein ASan-/Leak-/Shutdown-Nachweis

- Typ: **QUALITÄT**
- Befund: Projekte verwenden `/W4`, aber keine ASan-Konfiguration; keine Profiler-, Handle-, Heap- oder Langlaufberichte.
- Folge: Aussagen zu „keine Mem-Leaks“ oder Ressourcenfreigabe können derzeit nicht getroffen werden.

### AUD-054 – Warnungen werden nicht als Fehler behandelt

- Typ: **QUALITÄT**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – `TreatWarningAsError` ist zentral für alle eigenen MSBuild-Projekte aktiviert; die drei separat mit `cl` gebauten Releasewerkzeuge verwenden ebenfalls `/WX`. Die endgültige Warnungsfreiheit wird wie vereinbart im abschließenden Buildlauf geprüft.
- Befund: `/W4` ist gesetzt, `TreatWarningAsError` nicht. Der geprüfte Debug-Build war erfolgreich; damit ist aber „Warnungen dauerhaft null“ nicht CI-erzwungen.
- Korrektur: `/WX` zunächst für eigene Projekte, externe/generated Warnungen gezielt isolieren.

### AUD-055 – Release-/Installer-Workflows benötigen Sicherheits- und VM-Test

- Typ: **RISIKO / TESTLÜCKE**
- Befund: Custom Bundle/Bootstrap/Updater löschen oder ersetzen Dateien, erzeugen Shortcuts und Registryeinträge. Es existiert kein automatisierter Fresh-install/Upgrade/Uninstall-Test.
- Besonders prüfen: Pfadkanonisierung, beschädigtes Bundle, unvollständige Extraktion, gesperrte Dateien, Downgrade, Signaturkette, UAC-Cancel, laufende App, Recovery nach Stromausfall.

### AUD-056 – Deinstaller löscht den gesamten eigenen Installationsordner ohne Manifest

- Typ: **DATENRISIKO**
- [x] Status: **BEHOBEN IM MEHRFACHBLOCK (2026-09-08)** – Der Bootstrap schreibt beim Entpacken ein relatives Produktdatei-Manifest. Der Deinstaller akzeptiert daraus nur sichere relative Pfade, entfernt anschließend ausschließlich leere Verzeichnisse und bewahrt unbekannte Dateien ausdrücklich auf.
- Neu: `tools/release_bootstrap.cpp:55` iteriert über den Zielordner und `remove_all()` auf alles außer sich selbst.
- Folge: Falls Nutzerdateien oder fremde Dateien im Installationsordner liegen, werden sie mitgelöscht. Recovery besteht nur teilweise über Pending-Reboot.
- Korrektur: Nur manifestierte Produktdateien löschen; unbekannte Dateien erhalten bzw. Nutzer explizit informieren.

## Abgleich der `todo.md`-Behauptungen

| Aussage/Checkbox | Auditstatus | Begründung |
|---|---|---|
| Privacy vollständig verdrahtet | **Teilweise / neu testen** | Katalog vorhanden, aber Sonderzustände, Profile, Rollback, i18n und Golden-Tests fehlen. |
| Latency vollständig | **Teilweise** | Port vorhanden; Lifecycle, Unicode, Cancellation und Ergebnisparität sind nicht belegt. |
| Debloater Ende-zu-Ende/vollständig | **Nicht erfüllt** | AppX-Aktion und Installed-Semantik weichen grundlegend ab; Fehler werden verschluckt. |
| Downloads Ende-zu-Ende | **Teilweise** | UI/Katalog vorhanden; Scanfehler, Textparsing, Detailfehler und Cancellation fehlen. |
| Processes Ende-zu-Ende | **Teilweise** | Kernfunktionen vorhanden; UI-Thread-Arbeit, >64 CPUs, Restore dauerhafter Regeln und Paritätstests offen. |
| Persistenz kompatibel | **Teilweise** | Schlüssel/Dateipfad passen; Parsersemantik und atomisches Speichern nicht. |
| UI-Thread ausschließlich UI | **Nicht erfüllt** | Home und Processes führen Systemarbeit direkt auf dem UI-Thread aus. |
| Completion statt Polling | **Nicht erfüllt** | 120-/200-ms-DispatcherTimer pollen Futures in mehreren Pages. |
| Logging `%APPDATA%\Winchisel\logs` | **Nicht erfüllt** | Nur Bootlog im Temp-Verzeichnis; viele Fehler werden nicht geloggt. |
| Settings-Seite vollständig | **Teilweise** | Aktionen vorhanden; i18n, Fehlerfeedback, Updatefunktion und robuste Persistenz fehlen. |
| Update-Sicherheitsmodell umgesetzt | **Backend teilweise, Workflow fehlt** | Signiertes Manifest/Hash und Updatercode vorhanden, aber ohne Aufrufer/Version-/Installationsentscheidung. |
| Native Release-Pipeline abgeschlossen | **Nicht verifiziert** | Code vorhanden; Fresh VM, Upgrade, Uninstall, Recovery und Produktionssignierung offen. |

## Empfohlene Korrekturreihenfolge

1. **Wahrheit im Backlog herstellen:** betroffene „vollständig“-Markierungen in `todo.md` wieder öffnen und dieses Audit verlinken.
2. **Testfundament:** Katalog-Golden-Tests, Settings-Tests und fakebarer ProcessRunner, bevor Tweaks geändert werden.
3. **P0-Parität:** Updater anbinden, i18n einführen, AppX-Debloater korrigieren, vollständiges Performance-Backend-Mapping erzwingen.
4. **Sichere Mutationen:** Registry-/Service-/Task-Actions mit Vorwerten, Rollback, Partial-Result und Logging.
5. **Lifecycle/Performance:** Home/Processes aus UI-Thread; cancellable Tasks, keine Future-Polltimer, Timeouts/Job Objects.
6. **Systemaktionen:** Extras, Cleanup, Restore, Repair und HPET/Teredo in einer VM gegen Rust und Windows-Default testen.
7. **UI-Parität:** Page-State, Toasts, Tastatur, Screenreader, Focus, EN/DE.
8. **Releasehärtung:** `/WX`, ASan, Application Verifier/Handle-Leak-Test, Langlauf, Fresh-install/Upgrade/Uninstall und signierte Produktionsartefakte.

## Verifikationsmatrix für die schrittweise Abarbeitung

Für jeden Eintrag sollte ein Fix erst als abgeschlossen gelten, wenn folgende Felder dokumentiert sind:

| Feld | Erwartung |
|---|---|
| Rust-Referenz | Datei/Funktion und beobachtete Semantik |
| Neuer Sollzustand | identisch oder bewusst genehmigte Abweichung |
| Positivtest | Aktion funktioniert und UI zeigt korrekten Zustand |
| Negativtest | Zugriff verweigert, Tool fehlt, Exitcode ungleich 0 |
| Abbruch/Shutdown | keine Hänger, Prozesse/Handles werden beendet |
| Wiederholung | idempotent; zweiter Lauf beschädigt nichts |
| Mixed State | Teilzustand wird sichtbar und nicht fälschlich als On/Off dargestellt |
| Sprache/A11y | EN/DE, Tastatur, Screenreader/AutomationName |
| Ressourcen | keine wachsenden Handles/Threads/Heap-Allokationen im Langlauf |
| Beleg | automatisierter Test oder VM-Protokoll mit Build/OS-Version |

## Bereits erfolgreich verifiziert

- Debug x64 baut mit der explizit gefundenen VS-2026-MSBuild-Toolchain erfolgreich bis `out\x64\Debug\Winchisel.exe` und `Winchisel.Updater.exe`.
- Solution-Schichten Core/Platform/Application/App sind physisch vorhanden.
- Windows-Buildcheck (`>=26100`) und Elevation vor WinUI-Start sind vorhanden.
- Settings-Pfad und die vier historischen Schlüssel sind grundsätzlich kompatibel.
- Hauptnavigation enthält alle neun erwarteten Seiten.
- Fenstergröße 1280×720 und Mindestgröße 1100×650 sind gesetzt.
- Kataloge für Debloater, Downloads, Performance, Privacy und Latency sind eingecheckt.
- Mehrere Handle-Pfade schließen Ressourcen im Normalfall korrekt; der Review fand keinen allgemeinen, stetigen Heap-Leak-Beweis. Der konkrete BCrypt-Frühfehler-Leak und die genannten Lifecycle-Risiken bleiben offen.

## Dynamisch noch zwingend zu prüfen

- Vergleich alt/neu auf derselben frischen Windows-11-24H2-VM mit Registry-/Service-/Task-Snapshots vor und nach jedem Tweak.
- Startupzeit, Working Set, Private Bytes, Handles, Threads und CPU idle/aktive Scans über mindestens 30 Minuten.
- Navigation während laufendem winget/DISM/Latency/Repair sowie sofortiges Schließen der App.
- Systeme mit deutscher Windows-Anzeigesprache, Nicht-ASCII-Benutzerpfad, ohne winget, offline und mit Proxy.
- 64+ logische Prozessoren bzw. mehrere Processor Groups.
- Upgrade portable→portable, Setup→Setup, beschädigtes/signaturfalsches Release, gesperrte Zieldateien und UAC-Abbruch.
- Deinstallation mit zusätzlichen fremden Dateien im Installationsordner.
