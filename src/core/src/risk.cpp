#include "winchisel/core/risk.hpp"

#include "winchisel/core/tweak.hpp"

#include <string>
#include <utility>
#include <vector>

namespace winchisel::core {
namespace {

std::string lower_ascii(std::string_view text) {
    std::string lowered(text);
    for (auto& c : lowered) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return lowered;
}

bool contains_any(std::string const& haystack, std::string_view const* keywords, std::size_t count) {
    for (std::size_t i{}; i < count; ++i) {
        if (haystack.find(keywords[i]) != std::string::npos) return true;
    }
    return false;
}

// Security boundaries: flipping these weakens protection for the whole machine.
constexpr std::string_view kSecurityKeywords[] = {
    "defender",
    "deviceguard",
    "bitlocker",
    "hypervisorenforcedcodeintegrity",
    "hvci",
    "smartscreen",
    "firewall",
    "applocker",
    "consentprompt",
    "securedesktop",
    "verifiedandreputable",
    "credentialguard",
    "virtualizationbasedsecurity",
    "memoryintegrity",
    "appmodelunlock",
};

// Disabling these can break networking, logon, or core OS services.
constexpr std::string_view kCriticalServices[] = {
    "dnscache",
    "dhcp",
    "nlasvc",
    "rpcss",
    "dcomlaunch",
    "cryptsvc",
    "lanmanworkstation",
    "nsi",
    "winmgmt",
    "eventlog",
    "schedule",
    "iphlpsvc",
    "keyiso",
    "samss",
    "profsvc",
};

// Security products as services: turning them off drops protection.
constexpr std::string_view kSecurityServices[] = {
    "windefend",
    "wscsvc",
    "mpssvc",
    "bfe",
    "sense",
    "wdfilter",
};

// Task paths that are pure telemetry/diagnostics.
constexpr std::string_view kTelemetryPathKeywords[] = {
    "application experience",
    "customer experience",
    "diskdiagnostic",
    "feedback",
    "siuf",
    "error reporting",
    "sqm",
    "marebackup",
};

// Explicit verdicts where the rules would misjudge a catalog entry.
constexpr std::pair<std::string_view, TweakRisk> kCatalogOverrides[] = {
    {"security-developer-mode", TweakRisk::moderate},
    {"visual-effects-mode", TweakRisk::safe},
    {"gaming-performance-explorer-mouse-hover-time", TweakRisk::safe},
    {"gaming-dns-server", TweakRisk::moderate},
    // Only hides Defender notification toasts; protection itself is untouched,
    // so the "defender" security keyword must not rate it risky.
    {"notifications-windows-security", TweakRisk::moderate},
};

// Hand-built Extras toggles have no catalog rules, so they are rated here.
constexpr std::pair<std::string_view, TweakRisk> kExtrasRisks[] = {
    {"modern_standby", TweakRisk::safe},
    {"sync_provider", TweakRisk::safe},
    {"ctfmon", TweakRisk::moderate},
    {"ctfmon_dll", TweakRisk::moderate},
    {"timer_resolution", TweakRisk::safe},
    {"ipv6", TweakRisk::moderate},
    {"ps7", TweakRisk::safe},
    {"brave", TweakRisk::moderate},
    {"edge", TweakRisk::moderate},
    {"long_paths", TweakRisk::safe},
    {"developer_mode", TweakRisk::moderate},
    {"verbose_boot", TweakRisk::safe},
};

constexpr std::pair<std::string_view, std::string_view> kServices[] = {
    {"gaming-sysmain-service", "SysMain"},
    {"gaming-windows-search-service", "WSearch"},
    {"gaming-print-spooler-service", "Spooler"},
    {"gaming-telemetry-service", "DiagTrack"},
    {"gaming-connected-devices-platform-service", "CDPSvc"},
    {"gaming-compatibility-assistant-service", "PcaSvc"},
    {"gaming-error-reporting-service", "WerSvc"},
    {"gaming-geolocation-service", "lfsvc"},
    {"gaming-retail-demo-service", "RetailDemo"},
    {"gaming-insider-service", "wisvc"},
    {"gaming-phone-service", "PhoneSvc"},
    {"gaming-wallet-service", "WalletService"},
    {"gaming-smart-card-services", "SCardSvr"},
    {"gaming-maps-broker-service", "MapsBroker"},
    {"gaming-fax-service", "Fax"},
    {"gaming-wmp-network-service", "WMPNetworkSvc"},
    {"gaming-mixed-reality-service", "MixedRealityOpenXRSvc"},
    {"gaming-mobile-hotspot-service", "icssvc"},
    {"gaming-sms-router-service", "SmsRouter"},
    {"gaming-parental-controls-service", "WpcMonSvc"},
    {"gaming-payments-nfc-service", "SEMgrSvc"},
    {"gaming-spot-verifier-service", "svsvc"},
    {"gaming-remote-access-manager", "RasMan"},
    {"gaming-remote-access-auto", "RasAuto"},
    {"gaming-remote-desktop-services", "TermService"},
    {"gaming-remote-desktop-configuration", "SessionEnv"},
    {"gaming-remote-desktop-port-redirector", "UmRdpService"},
    {"gaming-xbox-auth-manager", "XblAuthManager"},
    {"gaming-xbox-game-save", "XblGameSave"},
    {"gaming-xbox-networking", "XboxNetApiSvc"},
    {"gaming-biometric-service", "WbioSrvc"},
    {"gaming-touch-keyboard-service", "TabletInputService"},
    {"gaming-sensor-monitoring-service", "SensrSvc"},
    {"gaming-sensor-data-service", "SensorDataService"},
    {"gaming-ai-fabric-service", "AIFabricSvc"},
};

constexpr std::pair<std::string_view, std::string_view> kTaskPaths[] = {
    {"CompatibilityAppraiserTask", "\\Microsoft\\Windows\\Application Experience\\Microsoft Compatibility Appraiser"},
    {"ProgramDataUpdaterTask", "\\Microsoft\\Windows\\Application Experience\\ProgramDataUpdater"},
    {"CEIPConsolidatorTask", "\\Microsoft\\Windows\\Customer Experience Improvement Program\\Consolidator"},
    {"UsbCeipTask", "\\Microsoft\\Windows\\Customer Experience Improvement Program\\UsbCeip"},
    {"DiskDiagnosticTask", "\\Microsoft\\Windows\\DiskDiagnostic\\Microsoft-Windows-DiskDiagnosticDataCollector"},
    {"FeedbackDmClientTask", "\\Microsoft\\Windows\\Feedback\\Siuf\\DmClient"},
    {"FeedbackDmClientDownloadTask", "\\Microsoft\\Windows\\Feedback\\Siuf\\DmClientOnScenarioDownload"},
    {"ErrorReportingQueueTask", "\\Microsoft\\Windows\\Windows Error Reporting\\QueueReporting"},
    {"SqmTask", "\\Microsoft\\Windows\\PI\\Sqm-Tasks"},
    {"MareBackupTask", "\\Microsoft\\Windows\\Application Experience\\MareBackup"},
    {"StartupAppTask", "\\Microsoft\\Windows\\Application Experience\\StartupAppTask"},
    {"MapsUpdateTask", "\\Microsoft\\Windows\\Maps\\MapsUpdateTask"},
    {"AutochkProxyTask", "\\Microsoft\\Windows\\Autochk\\Proxy"},
    {"FamilySafetyTask", "\\Microsoft\\Windows\\Shell\\FamilySafetyMonitor"},
    {"PowerEfficiencyTask", "\\Microsoft\\Windows\\Power Efficiency Diagnostics\\AnalyzeSystem"},
    {"WindowsAIRecallConfig", "\\Microsoft\\Windows\\WindowsAI\\RecallConfiguration"},
    {"WindowsAIRecallPipeline", "\\Microsoft\\Windows\\WindowsAI\\RecallPipeline"},
    {"OfficeActionsServer", "\\Microsoft\\Office\\Office Actions Server"},
};

constexpr std::pair<std::string_view, TweakRisk> kSelections[] = {
    {"gaming-win32-priority", TweakRisk::moderate},
    {"gaming-performance-svchost-split-threshold", TweakRisk::moderate},
    {"gaming-dns-server", TweakRisk::moderate},
    {"visual-effects-mode", TweakRisk::safe},
    {"gaming-background-apps", TweakRisk::moderate},
};

constexpr std::string_view kSpecialIds[] = {
    "gaming-gpu-amd-power",
    "gaming-gpu-nvidia-power",
    "gaming-gpu-intel-display",
    "gaming-usb-selective-suspend",
    "gaming-hibernate-fast-startup",
    "updates-system-protection",
};

bool offers_disabled(std::string_view id) {
    for (auto const& entry : get_performance_catalog()) {
        if (entry.id != id) continue;
        return lower_ascii(entry.options).find("disabled") != std::string::npos;
    }
    return false;
}

bool is_special_id(std::string_view id) {
    for (auto const known : kSpecialIds) {
        if (known == id) return true;
    }
    return false;
}

}  // namespace

std::string_view service_name_for_id(std::string_view id) {
    for (auto const& [key, name] : kServices) {
        if (key == id) return name;
    }
    return {};
}

std::string_view task_path_for_id(std::string_view id) {
    for (auto const& [key, path] : kTaskPaths) {
        if (key == id) return path;
    }
    return {};
}

TweakRisk assess_registry_targets(std::span<RegistryTarget const> targets) {
    bool touches_machine = false;
    for (auto const& target : targets) {
        if (contains_any(lower_ascii(target.key_path), kSecurityKeywords, std::size(kSecurityKeywords)))
            return TweakRisk::risky;
        if (target.hive == RegistryHive::local_machine) touches_machine = true;
    }
    return touches_machine ? TweakRisk::moderate : TweakRisk::safe;
}

TweakRisk assess_service(std::string_view service, bool can_disable) {
    const auto lowered = lower_ascii(service);
    for (auto const critical : kSecurityServices) {
        if (lowered == critical) return TweakRisk::risky;
    }
    for (auto const critical : kCriticalServices) {
        if (lowered == critical) return TweakRisk::risky;
    }
    return can_disable ? TweakRisk::moderate : TweakRisk::safe;
}

TweakRisk assess_task_path(std::string_view full_path) {
    if (contains_any(lower_ascii(full_path), kTelemetryPathKeywords, std::size(kTelemetryPathKeywords)))
        return TweakRisk::safe;
    return TweakRisk::moderate;
}

TweakRisk assess_extras(std::string_view key) {
    for (auto const& [name, risk] : kExtrasRisks) {
        if (name == key) return risk;
    }
    return TweakRisk::moderate;
}

TweakRisk assess_performance(std::string_view id) {
    for (auto const& [key, risk] : kCatalogOverrides) {
        if (key == id) return risk;
    }
    bool has_rule = false;
    bool touches_machine = false;
    for (auto const& rule : get_performance_registry_rules()) {
        if (rule.id != id) continue;
        has_rule = true;
        if (contains_any(lower_ascii(rule.path), kSecurityKeywords, std::size(kSecurityKeywords)))
            return TweakRisk::risky;
        if (rule.root != 0) touches_machine = true;
    }
    if (has_rule) return touches_machine ? TweakRisk::moderate : TweakRisk::safe;
    if (const auto service = service_name_for_id(id); !service.empty())
        return assess_service(service, offers_disabled(id));
    if (const auto path = task_path_for_id(id); !path.empty()) return assess_task_path(path);
    if (is_special_id(id)) return TweakRisk::moderate;
    for (auto const& [key, risk] : kSelections) {
        if (key == id) return risk;
    }
    return TweakRisk::moderate;
}

TweakRisk assess_privacy(std::string_view id) {
    for (auto const& [key, risk] : kCatalogOverrides) {
        if (key == id) return risk;
    }
    bool has_rule = false;
    bool touches_machine = false;
    for (auto const& rule : get_privacy_registry_rules()) {
        if (rule.id != id) continue;
        has_rule = true;
        const std::string key_path = std::string(rule.path) + "\\" + std::string(id);
        if (contains_any(lower_ascii(key_path), kSecurityKeywords, std::size(kSecurityKeywords)))
            return TweakRisk::risky;
        if (rule.root != 0) touches_machine = true;
    }
    if (has_rule) return touches_machine ? TweakRisk::moderate : TweakRisk::safe;
    return TweakRisk::moderate;
}

std::string_view risk_label_key(TweakRisk risk) {
    switch (risk) {
        case TweakRisk::safe: return "Safe";
        case TweakRisk::moderate: return "Moderate";
        case TweakRisk::risky: return "Risky";
    }
    return "Moderate";
}

}  // namespace winchisel::core
