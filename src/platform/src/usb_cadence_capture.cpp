// Produktions-Capture für die USB-Completion-Kadenz (nativ, ETW-Realtime).
// Siehe Header für Verträge. ETW-Payloads sind kein stabiler öffentlicher
// Vertrag: alle Feldzugriffe sind namensbasierte Best-Effort-Lookups.

#include "winchisel/platform/usb_cadence_capture.hpp"

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <pdh.h>
#include <tdh.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "pdh.lib")

namespace winchisel::platform::cadence {
namespace {

constexpr GUID kUcxGuid = {0x36da592d, 0xe43a, 0x4e28, {0xaf, 0x6f, 0x4b, 0xc5, 0x7c, 0x5a, 0x11, 0xe8}};
constexpr GUID kHub3Guid = {0xac52ad17, 0xcc01, 0x4f85, {0x8d, 0xf5, 0x4d, 0xce, 0x43, 0x33, 0xc9, 0x9b}};

constexpr USHORT kUcxTransferStartId = 26; // URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER /Start
constexpr USHORT kUcxTransferStopA = 27;   // /Stop Variante A
constexpr USHORT kUcxTransferStopB = 28;   // /Stop Variante B
constexpr USHORT kUcxDeviceRundownId = 5;  // UCX Device Rundown (Speed, PortPath)
constexpr USHORT kUcxEndpointRundownId = 6;// UCX Endpoint Rundown (Pipe, bInterval)
constexpr USHORT kHub3DeviceRundownId = 6; // USB Hub Driver Rundown (Pfad, Deskriptor)

bool is_admin() {
    BOOL admin = FALSE;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID group = nullptr;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &group) != FALSE) {
        CheckTokenMembership(nullptr, group, &admin);
        FreeSid(group);
    }
    return admin != FALSE;
}

std::string narrow(std::wstring_view value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), length, nullptr, nullptr);
    return result;
}

// Fehlertext mit Win32-Code (kein stilles Verschlucken).
std::string win32_detail(char const* what, ULONG code) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%s (Win32 %lu)", what, code);
    return std::string(buffer);
}

// Best-Effort UInt64-Lookup per TDH-Property-Name.
bool prop_u64(PEVENT_RECORD event, wchar_t const* name, unsigned long long& out) {
    PROPERTY_DATA_DESCRIPTOR desc{};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
    desc.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    ULONG status = TdhGetPropertySize(event, 0, nullptr, 1, &desc, &size);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (size != 1 && size != 2 && size != 4 && size != 8) return false;
    unsigned long long raw = 0;
    status = TdhGetProperty(event, 0, nullptr, 1, &desc, size, reinterpret_cast<PBYTE>(&raw));
    if (status != ERROR_SUCCESS) return false;
    // Größen < 8 Byte sind little-endian in raw zero-extended.
    if (size < 8) raw &= (size == 4 ? 0xFFFFFFFFULL : size == 2 ? 0xFFFFULL : 0xFFULL);
    out = raw;
    return true;
}

bool prop_u32(PEVENT_RECORD event, wchar_t const* name, unsigned long& out) {
    unsigned long long wide = 0;
    if (!prop_u64(event, name, wide) || wide > 0xFFFFFFFFULL) return false;
    out = static_cast<unsigned long>(wide);
    return true;
}

// Best-Effort String-Lookup (UnicodeString/AnsiString).
bool prop_string(PEVENT_RECORD event, wchar_t const* name, std::string& out) {
    PROPERTY_DATA_DESCRIPTOR desc{};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
    desc.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    ULONG status = TdhGetPropertySize(event, 0, nullptr, 1, &desc, &size);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (size == 0 || size > 32768) return false;
    std::vector<BYTE> buffer(size);
    status = TdhGetProperty(event, 0, nullptr, 1, &desc, size, buffer.data());
    if (status != ERROR_SUCCESS) return false;
    if (size >= 2 && buffer[1] == 0) {
        // WCHAR-String (ggf. ohne Terminator)
        std::wstring text(reinterpret_cast<wchar_t*>(buffer.data()), size / 2);
        while (!text.empty() && text.back() == L'\0') text.pop_back();
        out = narrow(text);
    } else {
        std::string text(reinterpret_cast<char*>(buffer.data()), size);
        while (!text.empty() && text.back() == '\0') text.pop_back();
        out = text;
    }
    return true;
}

std::string upper4(std::string value) {
    std::ranges::transform(value, value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

// "....VID_39AE&PID_400A...." -> vid/pid (Anzeige, kein Schlüssel).
void parse_vid_pid(std::string const& path, std::string& vid, std::string& pid) {
    auto find_hex = [&](char const* marker) -> std::string {
        const auto pos = path.find(marker);
        if (pos == std::string::npos || pos + 8 > path.size()) return {};
        std::string hex = path.substr(pos + 4, 4);
        if (!std::ranges::all_of(hex, [](unsigned char c) { return std::isxdigit(c) != 0; })) return {};
        return upper4(hex);
    };
    vid = find_hex("VID_");
    pid = find_hex("PID_");
}

// USB_DEVICE_SPEED (usbspec.h im WDK, per Header verifiziert):
// UsbLowSpeed=0, UsbFullSpeed=1, UsbHighSpeed=2, UsbSuperSpeed=3.
// Unbekannte Werte -> unknown, Rohwert bleibt für die Anzeige erhalten.
// (Am Live-Trace zu bestätigen, dass UCX genau dieses Enum loggt.)
UsbSpeed map_speed(unsigned long raw) {
    switch (raw) {
    case 0: return UsbSpeed::low;
    case 1: return UsbSpeed::full;
    case 2: return UsbSpeed::high;
    case 3: return UsbSpeed::super;
    default: return UsbSpeed::unknown;
    }
}

// Schnellstes Interrupt-IN bInterval aus einem Config-Deskriptor-Blob.
int fastest_b_interval(std::vector<BYTE> const& blob) {
    int best = 0;
    std::size_t i = 0;
    while (i + 1 < blob.size()) {
        const std::size_t len = blob[i];
        if (len < 2 || i + len > blob.size()) break;
        if (blob[i + 1] == 0x05 && len >= 7) { // Endpoint-Deskriptor
            const bool in = (blob[i + 2] & 0x80) != 0;
            const bool interrupt = (blob[i + 3] & 0x03) == 0x03;
            const int interval = blob[i + 6];
            if (in && interrupt && interval > 0 && (best == 0 || interval < best)) best = interval;
        }
        i += len;
    }
    return best;
}

bool prop_bytes(PEVENT_RECORD event, wchar_t const* name, std::vector<BYTE>& out) {
    PROPERTY_DATA_DESCRIPTOR desc{};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
    desc.ArrayIndex = ULONG_MAX;
    ULONG size = 0;
    ULONG status = TdhGetPropertySize(event, 0, nullptr, 1, &desc, &size);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (size == 0 || size > 65536) return false;
    out.assign(size, 0);
    return TdhGetProperty(event, 0, nullptr, 1, &desc, size, out.data()) == ERROR_SUCCESS;
}

struct PairHash {
    std::size_t operator()(std::pair<unsigned long long, unsigned long long> const& key) const noexcept {
        return std::hash<unsigned long long>{}(key.first * 0x9E3779B97F4A7C15ULL + key.second);
    }
};

// Begrenzung der Rohlisten: 8 kHz * 30 s = 240k; Cap mit Flag statt Absturz.
// Die Analyse arbeitet auf dem Kopf der Reihe (nach Warm-up-Skip).
constexpr std::size_t kMaxIntervalsPerPipe = 200000;
constexpr std::size_t kMaxConfigBlob = 4096;

struct EndpointRundown {
    unsigned long address = 0;
    unsigned long attributes = 0;
    unsigned long max_packet = 0;
    unsigned long interval = 0;
};

struct Collector {
    std::mutex mutex;
    // (device, pipe) -> letzte Completion-QPC
    std::unordered_map<std::pair<unsigned long long, unsigned long long>, long long, PairHash> last;
    // (device, urb) -> Start-QPC. URB-Pointer allein reicht nicht: zwei Geräte
    // können denselben Pointer-Wert recyceln.
    std::unordered_map<std::pair<unsigned long long, unsigned long long>, long long, PairHash> pending;
    unsigned long long starts_since_sweep = 0;
    std::unordered_map<std::pair<unsigned long long, unsigned long long>, std::vector<double>, PairHash> intervals;
    std::unordered_map<std::pair<unsigned long long, unsigned long long>, unsigned long long, PairHash> completions;
    std::unordered_map<std::pair<unsigned long long, unsigned long long>, bool, PairHash> truncated;
    std::unordered_map<std::pair<unsigned long long, unsigned long long>, EndpointRundown, PairHash> endpoints;
    std::unordered_map<unsigned long long, std::vector<BYTE>> config_blobs;
    std::unordered_map<unsigned long long, DeviceIdentity> identities;
    long long frequency = 1;

    void on_record(PEVENT_RECORD event) {
        const GUID& id = event->EventHeader.ProviderId;
        const bool ucx = memcmp(&id, &kUcxGuid, sizeof(GUID)) == 0;
        const bool hub3 = memcmp(&id, &kHub3Guid, sizeof(GUID)) == 0;
        if (!ucx && !hub3) return;
        const USHORT event_id = event->EventHeader.EventDescriptor.Id;
        const UCHAR opcode = event->EventHeader.EventDescriptor.Opcode;
        const long long qpc = event->EventHeader.TimeStamp.QuadPart;

        std::lock_guard<std::mutex> lock(mutex);
        if (ucx && (event_id == kUcxTransferStartId || event_id == kUcxTransferStopA || event_id == kUcxTransferStopB)) {
            unsigned long long device = 0, pipe = 0, urb = 0;
            if (!prop_u64(event, L"fid_UsbDevice", device) || device == 0) return;
            prop_u64(event, L"fid_PipeHandle", pipe); // 0 ok
            if (!prop_u64(event, L"fid_URB_Ptr", urb) || urb == 0) return;
            auto& identity = identities[device];
            identity.etw_device = device;
            const auto key = std::make_pair(device, pipe);
            if (opcode == 1) {
                const auto urb_key = std::make_pair(device, urb);
                if (pending.size() < 65536) pending[urb_key] = qpc;
                // Zeitbasierte Entsorgung (amortisiert): ab 8k ausstehenden URBs
                // alle Starts älter als 5 s verwerfen (verwaiste Dispatches ohne
                // Complete, z.B. abgebrochene Transfers). Gedrosselt auf max.
                // jeden 1024. Start, damit der Sweep kein Hotspot wird.
                if (pending.size() >= 8192 && frequency > 0 && (++starts_since_sweep & 1023) == 0) {
                    const long long cutoff = qpc - 5 * frequency;
                    for (auto it = pending.begin(); it != pending.end();) {
                        if (it->second < cutoff) it = pending.erase(it);
                        else ++it;
                    }
                }
                return;
            }
            // Stop: Completion-Intervall je (Device, Pipe) bilden.
            pending.erase(std::make_pair(device, urb));
            auto& slot = last[key];
            if (slot != 0 && qpc > slot) {
                const double us = qpc_delta_us(slot, qpc, frequency);
                if (us >= 0.0 && us < 50000.0) {
                    auto& series = intervals[key];
                    if (series.size() < kMaxIntervalsPerPipe) {
                        series.push_back(us);
                    } else {
                        truncated[key] = true;
                    }
                    if (intervals[key].size() == 1) identities[device].pipe = pipe;
                }
            }
            slot = qpc;
            ++completions[key];
            return;
        }
        if (ucx && event_id == kUcxDeviceRundownId) {
            unsigned long long device = 0;
            if (!prop_u64(event, L"fid_UsbDevice", device) || device == 0) return;
            auto& identity = identities[device];
            identity.etw_device = device;
            unsigned long speed = 0;
            if (prop_u32(event, L"DeviceSpeed", speed)) {
                identity.speed_raw = speed;
                identity.speed = map_speed(speed);
            }
            std::string port;
            if (prop_string(event, L"PortPath", port)) identity.port_path = port;
            return;
        }
        if (ucx && event_id == kUcxEndpointRundownId) {
            unsigned long long device = 0, pipe = 0;
            if (!prop_u64(event, L"fid_UsbDevice", device) || device == 0) return;
            prop_u64(event, L"fid_PipeHandle", pipe);
            unsigned long interval = 0;
            if (prop_u32(event, L"fid_bInterval", interval) && interval > 0) {
                auto& identity = identities[device];
                identity.etw_device = device;
                if (identity.b_interval == 0 || static_cast<int>(interval) < identity.b_interval)
                    identity.b_interval = static_cast<int>(interval);
                if (pipe != 0 && identity.pipe == 0) identity.pipe = pipe;
            }
            // Endpoint-Detail je (Device, Pipe) für die Anzeige des gemessenen
            // Endpoints (Feldnamen per Schema-Dump verifiziert).
            if (pipe != 0) {
                EndpointRundown rundown;
                unsigned long address = 0, attributes = 0, max_packet = 0;
                if (prop_u32(event, L"fid_bEndpointAddress", address)) rundown.address = address;
                if (prop_u32(event, L"fid_bmAttributes", attributes)) rundown.attributes = attributes;
                if (prop_u32(event, L"fid_wMaxPacketSize", max_packet)) rundown.max_packet = max_packet;
                rundown.interval = interval;
                if (rundown.address != 0) endpoints[std::make_pair(device, pipe)] = rundown;
            }
            return;
        }
        if (hub3 && event_id == kHub3DeviceRundownId) {
            unsigned long long device = 0;
            if (!prop_u64(event, L"fid_UsbDevice", device) || device == 0) return;
            auto& identity = identities[device];
            identity.etw_device = device;
            std::string path;
            if (prop_string(event, L"fid_DeviceInterfacePath", path) && !path.empty()) {
                identity.interface_path = path;
                parse_vid_pid(path, identity.vid, identity.pid);
            }
            std::string port;
            if (prop_string(event, L"fid_PortPath", port) && !port.empty() && identity.port_path.empty())
                identity.port_path = port;
            std::vector<BYTE> cfg;
            if (prop_bytes(event, L"fid_ConfigurationDescriptor", cfg) && !cfg.empty()) {
                const int best = fastest_b_interval(cfg);
                if (best > 0 && (identity.b_interval == 0 || best < identity.b_interval))
                    identity.b_interval = best;
                // Blob für Companion-Lookup am Ende aufheben (begrenzt).
                if (config_blobs[device].empty() && cfg.size() <= 65536) {
                    const std::size_t take = cfg.size() < kMaxConfigBlob ? cfg.size() : kMaxConfigBlob;
                    config_blobs[device].assign(cfg.begin(), cfg.begin() + take);
                }
            }
            return;
        }
    }
};

void CALLBACK record_thunk(PEVENT_RECORD event) {
    auto* collector = reinterpret_cast<Collector*>(event->UserContext);
    if (collector != nullptr) collector->on_record(event);
}

struct Session {
    TRACEHANDLE handle = 0;
    std::wstring name;
    std::vector<BYTE> storage;
    bool active = false;
    ~Session() { stop(); }
    void stop() {
        if (!active) return;
        active = false;
        auto* props = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(storage.data());
        ControlTraceW(handle, name.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        // Hinweis: kein CloseTrace hier — handle ist ein Controller-Handle aus
        // StartTrace, kein Consumer-Handle aus OpenTrace (das schließt der Aufrufer).
        handle = 0;
    }
};

bool manifest_has(const GUID& guid, wchar_t const* property, ULONG* count = nullptr) {
    ULONG bytes = 0;
    ULONG status = TdhEnumerateManifestProviderEvents(const_cast<LPGUID>(&guid), nullptr, &bytes);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (bytes == 0) return false;
    std::vector<BYTE> buffer(bytes);
    auto* info = reinterpret_cast<PPROVIDER_EVENT_INFO>(buffer.data());
    status = TdhEnumerateManifestProviderEvents(const_cast<LPGUID>(&guid), info, &bytes);
    if (status != ERROR_SUCCESS || info->NumberOfEvents == 0) return false;
    if (count != nullptr) *count = info->NumberOfEvents;
    for (ULONG i = 0; i < info->NumberOfEvents; ++i) {
        ULONG info_bytes = 0;
        status = TdhGetManifestEventInformation(const_cast<LPGUID>(&guid), &info->EventDescriptorsArray[i],
                                                nullptr, &info_bytes);
        if (status != ERROR_INSUFFICIENT_BUFFER) continue;
        std::vector<BYTE> evt(info_bytes);
        auto* meta = reinterpret_cast<PTRACE_EVENT_INFO>(evt.data());
        status = TdhGetManifestEventInformation(const_cast<LPGUID>(&guid), &info->EventDescriptorsArray[i],
                                                meta, &info_bytes);
        if (status != ERROR_SUCCESS) continue;
        for (ULONG p = 0; p < meta->PropertyCount; ++p) {
            auto* pname = reinterpret_cast<wchar_t const*>(evt.data() + meta->EventPropertyInfoArray[p].NameOffset);
            if (wcscmp(pname, property) == 0) return true;
        }
    }
    return false;
}

bool provider_registered(const GUID& guid) {
    ULONG bytes = 0;
    ULONG status = TdhEnumerateProviders(nullptr, &bytes);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (bytes == 0) return false;
    std::vector<BYTE> buffer(bytes);
    auto* list = reinterpret_cast<PPROVIDER_ENUMERATION_INFO>(buffer.data());
    if (TdhEnumerateProviders(list, &bytes) != ERROR_SUCCESS) return false;
    for (ULONG i = 0; i < list->NumberOfProviders; ++i) {
        if (memcmp(&list->TraceProviderInfoArray[i].ProviderGuid, &guid, sizeof(GUID)) == 0) return true;
    }
    return false;
}

} // namespace

bool etw_providers_available(std::string* detail) {
    ULONG ucx_count = 0, hub3_count = 0;
    const bool ucx = provider_registered(kUcxGuid) && manifest_has(kUcxGuid, L"fid_URB_Ptr", &ucx_count);
    const bool hub3 = provider_registered(kHub3Guid) && manifest_has(kHub3Guid, L"fid_UsbDevice", &hub3_count);
    if (detail != nullptr) {
        char buffer[160]{};
        std::snprintf(buffer, sizeof(buffer), "UCX:%s(%lu) HUB3:%s(%lu)", ucx ? "ok" : "missing", ucx_count,
                      hub3 ? "ok" : "missing", hub3_count);
        *detail = buffer;
    }
    return ucx && hub3;
}

winchisel::core::Result<CaptureOutcome> capture_usb_cadence(CaptureConfig const& config) {
    if (!is_admin()) {
        return std::unexpected(winchisel::core::Error{
            "Administratorrechte erforderlich: ETW-Kernel-Capture braucht erhöhte Rechte. "
            "Winchisel erhöht starten und Messung wiederholen."});
    }
    std::string detail;
    if (!etw_providers_available(&detail)) {
        return std::unexpected(winchisel::core::Error{
            "USB-ETW-Provider nicht verfügbar (" + detail + "). Keine scheinbare Messung möglich."});
    }
    int seconds = config.seconds < 1 ? 1 : config.seconds > 30 ? 30 : config.seconds;

    Session session;
    wchar_t name[64]{};
    swprintf_s(name, L"winchisel-cadence-%lu", GetCurrentProcessId());
    session.name = name;
    const std::size_t props_size = sizeof(EVENT_TRACE_PROPERTIES) + (session.name.size() + 1) * sizeof(wchar_t) + 4096;
    session.storage.assign(props_size, 0);
    auto* props = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(session.storage.data());
    props->Wnode.BufferSize = static_cast<ULONG>(props_size);
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1; // QPC
    props->LogFileMode = EVENT_TRACE_REAL_TIME_MODE;
    props->MaximumBuffers = 64;
    props->MinimumBuffers = 8;
    props->BufferSize = 128;
    props->FlushTimer = 1;
    props->LogFileNameOffset = 0;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);

    ULONG status = StartTraceW(&session.handle, session.name.c_str(), props);
    if (status != ERROR_SUCCESS) return std::unexpected(winchisel::core::Error{win32_detail("StartTrace fehlgeschlagen", status)});

    // RAII ab hier: jeder Fehlerweg stoppt die eigene Session (niemals fremde).
    session.active = true;
    auto fail = [&](char const* what, ULONG code) {
        session.stop();
        return std::unexpected(winchisel::core::Error{win32_detail(what, code)});
    };

    ENABLE_TRACE_PARAMETERS params{};
    params.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
    status = EnableTraceEx2(session.handle, &kUcxGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                            0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0, &params);
    if (status != ERROR_SUCCESS) return fail("UCX-Provider aktivieren fehlgeschlagen", status);
    status = EnableTraceEx2(session.handle, &kHub3Guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, TRACE_LEVEL_VERBOSE,
                            0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0, &params);
    if (status != ERROR_SUCCESS) return fail("HUB3-Provider aktivieren fehlgeschlagen", status);

    Collector collector;
    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);
    collector.frequency = qpf.QuadPart > 0 ? qpf.QuadPart : 1;

    EVENT_TRACE_LOGFILEW logfile{};
    logfile.LoggerName = session.name.data();
    logfile.ProcessTraceMode =
        PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    logfile.EventRecordCallback = &record_thunk;
    logfile.Context = &collector;
    TRACEHANDLE trace = OpenTraceW(&logfile);
    if (trace == INVALID_PROCESSTRACE_HANDLE) {
        const DWORD code = GetLastError();
        session.stop();
        return std::unexpected(winchisel::core::Error{win32_detail("OpenTrace fehlgeschlagen", code)});
    }

    std::thread worker([&] { ProcessTrace(&trace, 1, nullptr, nullptr); });
    LARGE_INTEGER qpc_freq{};
    LARGE_INTEGER qpc_start{};
    QueryPerformanceFrequency(&qpc_freq);
    QueryPerformanceCounter(&qpc_start);
    if (qpc_freq.QuadPart <= 0) qpc_freq.QuadPart = 10000000;
    const double budget_us = static_cast<double>(seconds) * 1e6;
    auto elapsed_us = [&]() -> double {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return qpc_delta_us(qpc_start.QuadPart, now.QuadPart, qpc_freq.QuadPart);
    };

    // Grobe CPU-Zeitreihe (fail-open): erste Probe verwerfen (PDH braucht zwei
    // Samples für % Processor Time), danach ~500-ms-Raster auf der QPC-Achse.
    PDH_HQUERY pdh_query = nullptr;
    PDH_HCOUNTER pdh_counter = nullptr;
    const bool pdh_ok = PdhOpenQueryW(nullptr, 0, &pdh_query) == ERROR_SUCCESS &&
                        PdhAddEnglishCounterW(pdh_query, L"\\Processor(_Total)\\% Processor Time", 0,
                                              &pdh_counter) == ERROR_SUCCESS &&
                        PdhCollectQueryData(pdh_query) == ERROR_SUCCESS;
    std::vector<CpuSample> cpu;
    double next_cpu_us = 500000.0;

    bool cancelled = false;
    for (;;) {
        const double elapsed = elapsed_us();
        if (elapsed >= budget_us) break;
        if (config.cancel != nullptr && config.cancel->load()) {
            cancelled = true;
            break;
        }
        if (pdh_ok && elapsed >= next_cpu_us) {
            next_cpu_us = elapsed + 500000.0;
            if (PdhCollectQueryData(pdh_query) == ERROR_SUCCESS) {
                PDH_FMT_COUNTERVALUE value{};
                if (PdhGetFormattedCounterValue(pdh_counter, PDH_FMT_DOUBLE, nullptr, &value) ==
                        ERROR_SUCCESS &&
                    value.CStatus == ERROR_SUCCESS) {
                    double busy = value.doubleValue;
                    if (busy < 0.0) busy = 0.0;
                    if (busy > 100.0) busy = 100.0;
                    cpu.push_back({elapsed, busy});
                }
            }
        }
        if (config.progress) {
            char text[96]{};
            const int pct = budget_us > 0.0 ? static_cast<int>(elapsed * 100.0 / budget_us) : 100;
            std::snprintf(text, sizeof(text), "Erfasse USB-Kadenz... %ds",
                          seconds - static_cast<int>(elapsed / 1e6));
            config.progress(pct, text);
        }
        Sleep(200);
    }
    if (pdh_ok) PdhCloseQuery(pdh_query);
    session.stop(); // entblockt ProcessTrace; danach Join
    if (worker.joinable()) worker.join();
    CloseTrace(trace);

    CaptureOutcome outcome;
    outcome.events_lost = props->EventsLost;
    outcome.buffers_written = props->BuffersWritten;
    outcome.realtime_buffers_lost = props->RealTimeBuffersLost;
    outcome.aborted = cancelled;
    outcome.cpu = std::move(cpu);
    {
        std::lock_guard<std::mutex> lock(collector.mutex);
        for (auto const& [key, series] : collector.intervals) {
            DeviceCapture device;
            if (auto found = collector.identities.find(key.first); found != collector.identities.end())
                device.identity = found->second;
            device.identity.etw_device = key.first;
            if (device.identity.pipe == 0) device.identity.pipe = key.second;
            device.intervals_us = series;
            if (auto count = collector.completions.find(key); count != collector.completions.end())
                device.completions = count->second;
            if (auto cut = collector.truncated.find(key); cut != collector.truncated.end())
                device.truncated = cut->second;
            // Endpoint-Detail der Haupt-Pipe + Companion aus dem Config-Blob.
            if (auto ep = collector.endpoints.find(std::make_pair(key.first, device.identity.pipe));
                ep != collector.endpoints.end()) {
                device.identity.endpoint_address = ep->second.address;
                device.identity.endpoint_attributes = ep->second.attributes;
                device.identity.max_packet = ep->second.max_packet;
            }
            if (auto blob = collector.config_blobs.find(key.first);
                blob != collector.config_blobs.end() && device.identity.endpoint_address != 0) {
                std::vector<unsigned char> view(blob->second.begin(), blob->second.end());
                const auto endpoint = endpoint_detail(view, device.identity.endpoint_address);
                if (endpoint.found && endpoint.has_companion) {
                    device.identity.has_companion = true;
                    device.identity.max_burst = endpoint.max_burst;
                    device.identity.bytes_per_interval = endpoint.bytes_per_interval;
                }
                if (endpoint.found && endpoint.has_interface) {
                    device.identity.has_interface = true;
                    device.identity.interface_number = endpoint.interface_number;
                    device.identity.alt_setting = endpoint.alt_setting;
                }
            }
            outcome.devices.push_back(std::move(device));
        }
    }
    std::ranges::sort(outcome.devices, std::greater<>(),
                      [](DeviceCapture const& d) { return d.completions; });
    return outcome;
}

} // namespace winchisel::platform::cadence
