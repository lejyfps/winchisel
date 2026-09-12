// Winchisel ETW spike (Phase 1 Release-Gate).
//
// Verifies on the current machine, without changing product code:
//  1. UCX + USBHUB3 providers are registered (TdhEnumerateProviders).
//  2. Their manifest schemas expose the fields DeepPoll relies on
//     (fid_URB_Ptr, fid_UsbDevice, fid_PipeHandle, descriptor payload).
//  3. A private realtime ETW session (StartTrace/EnableTraceEx2/QPC,
//     ProcessTrace, RAII stop via ControlTrace) can capture them, with
//     loss counters (EventsLost/BuffersLost) reported.
//  4. Distinct fid_UsbDevice handles are observable (two identical
//     devices stay separable) - reported, not asserted.
//
// Usage:
//   etw_spike.exe [--seconds N] [--preflight-only] [--dump-schema] [--save-etl=path]
// Exit codes: 0 = gate passed, 1 = gate failed, 3 = admin required for capture.
// --save-etl writes a private file-mode ETL (fixture recording / support
// artifact) instead of consuming realtime; needs admin like live capture.
//
// Notes for the record:
//  - ETW provider names/payloads are NOT a stable public contract; every
//    field access below is a preflighted best-effort lookup by name.
//  - Session name is unique per process; we never stop foreign sessions.
//  - Timestamps use PROCESS_TRACE_MODE_RAW_TIMESTAMP (QPC ticks) and are
//    converted to microseconds with QueryPerformanceFrequency.

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "tdh.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

constexpr GUID kUcxGuid = {0x36da592d, 0xe43a, 0x4e28, {0xaf, 0x6f, 0x4b, 0xc5, 0x7c, 0x5a, 0x11, 0xe8}};
constexpr GUID kHub3Guid = {0xac52ad17, 0xcc01, 0x4f85, {0x8d, 0xf5, 0x4d, 0xce, 0x43, 0x33, 0xc9, 0x9b}};

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

std::wstring session_name() {
    wchar_t buf[64]{};
    swprintf_s(buf, L"winchisel-etw-spike-%lu", GetCurrentProcessId());
    return std::wstring(buf);
}

struct Counts {
    uint64_t ucx = 0;
    uint64_t hub3 = 0;
    uint64_t other = 0;
    uint64_t urb_start = 0;
    uint64_t urb_stop = 0;
    uint64_t first_qpc = 0;
    uint64_t last_qpc = 0;
    std::unordered_set<unsigned long long> devices;
    bool saw_urb_ptr_field = false;
    bool saw_device_field = false;
    bool saw_pipe_field = false;
    bool saw_descriptor_field = false;
};

// Best-effort UInt64 property lookup by name via TDH.
bool try_get_u64(PEVENT_RECORD event, const wchar_t* name, unsigned long long& out) {
    ULONG size = 0;
    // Query required size first; TDH may return ERROR_INSUFFICIENT_BUFFER.
    PROPERTY_DATA_DESCRIPTOR desc{};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
    desc.ArrayIndex = ULONG_MAX;
    ULONG status = TdhGetPropertySize(event, 0, nullptr, 1, &desc, &size);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (size != sizeof(unsigned long long) && size != sizeof(unsigned long) && size != sizeof(unsigned short)) {
        // Accept 1/2/4/8 byte integers; normalize below.
        if (size != 1 && size != 2 && size != 4 && size != 8) return false;
    }
    unsigned long long raw = 0;
    status = TdhGetProperty(event, 0, nullptr, 1, &desc, size, reinterpret_cast<PBYTE>(&raw));
    if (status != ERROR_SUCCESS) return false;
    out = raw; // little-endian zero-extended for sub-8-byte widths
    return true;
}

bool event_has_property(PEVENT_RECORD event, const wchar_t* name) {
    ULONG size = 0;
    PROPERTY_DATA_DESCRIPTOR desc{};
    desc.PropertyName = reinterpret_cast<ULONGLONG>(name);
    desc.ArrayIndex = ULONG_MAX;
    ULONG status = TdhGetPropertySize(event, 0, nullptr, 1, &desc, &size);
    return status == ERROR_SUCCESS || status == ERROR_INSUFFICIENT_BUFFER;
}

void CALLBACK record_callback(PEVENT_RECORD event) {
    auto* counts = reinterpret_cast<Counts*>(event->UserContext);
    if (counts == nullptr || event->EventHeader.ProviderId.Data1 == 0) return;
    const GUID& id = event->EventHeader.ProviderId;
    const bool ucx = memcmp(&id, &kUcxGuid, sizeof(GUID)) == 0;
    const bool hub3 = memcmp(&id, &kHub3Guid, sizeof(GUID)) == 0;
    if (ucx) ++counts->ucx;
    else if (hub3) ++counts->hub3;
    else ++counts->other;

    const ULONGLONG ts = event->EventHeader.TimeStamp.QuadPart;
    if (counts->first_qpc == 0) counts->first_qpc = static_cast<uint64_t>(ts);
    counts->last_qpc = static_cast<uint64_t>(ts);

    // Opcode 1 = Start, 2 = Stop for the UCX transfer events (best-effort).
    if (ucx) {
        if (event->EventHeader.EventDescriptor.Opcode == 1) ++counts->urb_start;
        if (event->EventHeader.EventDescriptor.Opcode == 2) ++counts->urb_stop;
    }

    unsigned long long device = 0;
    if (try_get_u64(event, L"fid_UsbDevice", device)) {
        counts->saw_device_field = true;
        if (device != 0) counts->devices.insert(device);
    }
    if (event_has_property(event, L"fid_URB_Ptr")) counts->saw_urb_ptr_field = true;
    if (event_has_property(event, L"fid_PipeHandle")) counts->saw_pipe_field = true;
    if (event_has_property(event, L"fid_ConfigurationDescriptor")) counts->saw_descriptor_field = true;
}

// Preflight: is the provider GUID registered at all?
bool provider_registered(const GUID& guid) {
    ULONG bytes = 0;
    ULONG status = TdhEnumerateProviders(nullptr, &bytes);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (bytes == 0) return false;
    std::vector<BYTE> buffer(bytes);
    auto* list = reinterpret_cast<PPROVIDER_ENUMERATION_INFO>(buffer.data());
    status = TdhEnumerateProviders(list, &bytes);
    if (status != ERROR_SUCCESS) return false;
    for (ULONG i = 0; i < list->NumberOfProviders; ++i) {
        if (memcmp(&list->TraceProviderInfoArray[i].ProviderGuid, &guid, sizeof(GUID)) == 0) return true;
    }
    return false;
}

// Preflight: does any manifest event of the provider mention a property?
bool manifest_mentions(const GUID& guid, const wchar_t* property, ULONG& out_events) {
    out_events = 0;
    ULONG bytes = 0;
    ULONG status = TdhEnumerateManifestProviderEvents(const_cast<LPGUID>(&guid), nullptr, &bytes);
    if (status != ERROR_SUCCESS && status != ERROR_INSUFFICIENT_BUFFER) return false;
    if (bytes == 0) return false;
    std::vector<BYTE> buffer(bytes);
    auto* info = reinterpret_cast<PPROVIDER_EVENT_INFO>(buffer.data());
    status = TdhEnumerateManifestProviderEvents(const_cast<LPGUID>(&guid), info, &bytes);
    if (status != ERROR_SUCCESS) return false;
    out_events = info->NumberOfEvents;
    if (info->NumberOfEvents == 0) return false;
    // Scan all manifest events: the URB transfer events are not necessarily
    // among the first entries (UCX exposes ~79 events).
    for (ULONG i = 0; i < info->NumberOfEvents; ++i) {
        ULONG info_bytes = 0;
        status = TdhGetManifestEventInformation(const_cast<LPGUID>(&guid), &info->EventDescriptorsArray[i], nullptr, &info_bytes);
        if (status != ERROR_INSUFFICIENT_BUFFER) continue;
        std::vector<BYTE> evt_buf(info_bytes);
        auto* evt = reinterpret_cast<PTRACE_EVENT_INFO>(evt_buf.data());
        status = TdhGetManifestEventInformation(const_cast<LPGUID>(&guid), &info->EventDescriptorsArray[i], evt, &info_bytes);
        if (status != ERROR_SUCCESS) continue;
        for (ULONG p = 0; p < evt->PropertyCount; ++p) {
            const wchar_t* pname = reinterpret_cast<const wchar_t*>(evt_buf.data() + evt->EventPropertyInfoArray[p].NameOffset);
            if (wcscmp(pname, property) == 0) return true;
        }
    }
    return false;
}

struct Session {
    TRACEHANDLE handle = 0;
    std::wstring name;
    std::vector<BYTE> props_buffer;
    bool active = false;
    ~Session() { stop(); }
    void stop() {
        if (!active) return;
        active = false;
        auto* props = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(props_buffer.data());
        ControlTraceW(handle, name.c_str(), props, EVENT_TRACE_CONTROL_STOP);
        // Hinweis: kein CloseTrace hier — Controller-Handle, kein Consumer-Handle.
        handle = 0;
    }
};

} // namespace

int wmain(int argc, wchar_t** argv) {
    int seconds = 4;
    bool preflight_only = false;
    bool dump_schema = false;
    std::wstring save_etl;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--preflight-only") == 0) preflight_only = true;
        if (wcscmp(argv[i], L"--dump-schema") == 0) dump_schema = true;
        if (wcsncmp(argv[i], L"--seconds=", 10) == 0) seconds = _wtoi(argv[i] + 10);
        if (wcsncmp(argv[i], L"--save-etl=", 11) == 0) save_etl = argv[i] + 11;
    }
    if (seconds < 1) seconds = 1;
    if (seconds > 30) seconds = 30;

    LARGE_INTEGER qpf{};
    QueryPerformanceFrequency(&qpf);

    printf("winchisel ETW spike (Phase 1 gate)\n");
    printf("admin: %s\n", is_admin() ? "yes" : "no");
    printf("qpc_hz: %lld\n", static_cast<long long>(qpf.QuadPart));

    const bool ucx_reg = provider_registered(kUcxGuid);
    const bool hub3_reg = provider_registered(kHub3Guid);
    printf("provider Microsoft-Windows-USB-UCX registered: %s\n", ucx_reg ? "yes" : "NO");
    printf("provider Microsoft-Windows-USB-USBHUB3 registered: %s\n", hub3_reg ? "yes" : "NO");

    ULONG ucx_events = 0, hub3_events = 0;
    const bool ucx_schema = ucx_reg && manifest_mentions(kUcxGuid, L"fid_URB_Ptr", ucx_events);
    const bool hub3_schema = hub3_reg && manifest_mentions(kHub3Guid, L"fid_UsbDevice", hub3_events);
    printf("ucx manifest events: %lu, mentions fid_URB_Ptr: %s\n", ucx_events, ucx_schema ? "yes" : "NO");
    printf("hub3 manifest events: %lu, mentions fid_UsbDevice: %s\n", hub3_events, hub3_schema ? "yes" : "NO");

    if (dump_schema) {
        // Dump distinct manifest property names per provider so the schema
        // contract can be recorded in todo.md even when the gate fails.
        for (int p = 0; p < 2; ++p) {
            const GUID& guid = p == 0 ? kUcxGuid : kHub3Guid;
            ULONG bytes = 0;
            ULONG st = TdhEnumerateManifestProviderEvents(const_cast<LPGUID>(&guid), nullptr, &bytes);
            if (st != ERROR_SUCCESS && st != ERROR_INSUFFICIENT_BUFFER) continue;
            std::vector<BYTE> buf(bytes);
            auto* pi = reinterpret_cast<PPROVIDER_EVENT_INFO>(buf.data());
            st = TdhEnumerateManifestProviderEvents(const_cast<LPGUID>(&guid), pi, &bytes);
            if (st != ERROR_SUCCESS) continue;
            printf("--- %s: %lu events ---\n", p == 0 ? "UCX" : "HUB3", pi->NumberOfEvents);
            for (ULONG i = 0; i < pi->NumberOfEvents; ++i) {
                ULONG ib = 0;
                st = TdhGetManifestEventInformation(const_cast<LPGUID>(&guid), &pi->EventDescriptorsArray[i], nullptr, &ib);
                if (st != ERROR_INSUFFICIENT_BUFFER) {
                    if (i < 4) printf("  ev %lu: no manifest info (status %lu)\n", i, st);
                    continue;
                }
                std::vector<BYTE> eb(ib);
                auto* ei = reinterpret_cast<PTRACE_EVENT_INFO>(eb.data());
                st = TdhGetManifestEventInformation(const_cast<LPGUID>(&guid), &pi->EventDescriptorsArray[i], ei, &ib);
                if (st != ERROR_SUCCESS) continue;
                const wchar_t* task = reinterpret_cast<const wchar_t*>(eb.data() + ei->TaskNameOffset);
                printf("  ev %lu id=%u ver=%u op=%u task=%ls props=%lu\n", i,
                       pi->EventDescriptorsArray[i].Id, pi->EventDescriptorsArray[i].Version,
                       pi->EventDescriptorsArray[i].Opcode, ei->TaskNameOffset ? task : L"?",
                       ei->PropertyCount);
                for (ULONG q = 0; q < ei->PropertyCount; ++q) {
                    const wchar_t* pn = reinterpret_cast<const wchar_t*>(eb.data() + ei->EventPropertyInfoArray[q].NameOffset);
                    printf("      %ls\n", pn);
                }
            }
        }
    }

    bool gate = ucx_reg && hub3_reg && ucx_schema && hub3_schema;
    if (preflight_only || !is_admin()) {
        if (!is_admin() && !preflight_only) {
            printf("capture: SKIPPED (admin required) -> exit 3\n");
            return 3;
        }
        printf("preflight-only gate: %s\n", gate ? "PASS" : "FAIL");
        return gate ? 0 : 1;
    }

    // --- Private Session: Realtime oder ETL-Datei (Fixture-Recording) ---
    // ETL-Modus (--save-etl): gleiche Provider, Datei statt Realtime-Consumer.
    // Dient als Fixture-Quelle für Parser-Tests und Support-Artefakt.
    const bool file_mode = !save_etl.empty();
    Session session;
    session.name = session_name();
    const size_t props_size = sizeof(EVENT_TRACE_PROPERTIES) + (session.name.size() + 1) * sizeof(wchar_t) +
                              (file_mode ? (save_etl.size() + 1) * sizeof(wchar_t) : 0) + 4096;
    session.props_buffer.assign(props_size, 0);
    auto* props = reinterpret_cast<PEVENT_TRACE_PROPERTIES>(session.props_buffer.data());
    props->Wnode.BufferSize = static_cast<ULONG>(props_size);
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1; // QPC
    props->LogFileMode = file_mode ? EVENT_TRACE_FILE_MODE_SEQUENTIAL : EVENT_TRACE_REAL_TIME_MODE;
    props->MaximumBuffers = 64;
    props->MinimumBuffers = 8;
    props->BufferSize = 128;
    props->FlushTimer = 1;
    props->LogFileNameOffset = 0;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    if (file_mode) {
        auto* base = reinterpret_cast<wchar_t*>(session.props_buffer.data() + sizeof(EVENT_TRACE_PROPERTIES));
        wcscpy_s(base, session.name.size() + 1, session.name.c_str());
        auto* file = base + session.name.size() + 1;
        wcscpy_s(file, save_etl.size() + 1, save_etl.c_str());
        props->LogFileNameOffset =
            static_cast<ULONG>(reinterpret_cast<BYTE*>(file) - session.props_buffer.data());
    }

    ULONG status = StartTraceW(&session.handle, session.name.c_str(), props);
    if (status != ERROR_SUCCESS) {
        printf("StartTrace failed: %lu\n", status);
        return 1;
    }
    session.active = true;

    ENABLE_TRACE_PARAMETERS params{};
    params.Version = ENABLE_TRACE_PARAMETERS_VERSION_2;
    params.EnableProperty = EVENT_ENABLE_PROPERTY_PROCESS_START_KEY;
    status = EnableTraceEx2(session.handle, &kUcxGuid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                            TRACE_LEVEL_VERBOSE, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0, &params);
    if (status != ERROR_SUCCESS) {
        printf("EnableTraceEx2 UCX failed: %lu\n", status);
        return 1;
    }
    status = EnableTraceEx2(session.handle, &kHub3Guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER,
                            TRACE_LEVEL_VERBOSE, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0, &params);
    if (status != ERROR_SUCCESS) {
        printf("EnableTraceEx2 HUB3 failed: %lu\n", status);
        return 1;
    }

    Counts counts{};
    TRACEHANDLE trace = INVALID_PROCESSTRACE_HANDLE;
    if (!file_mode) {
        EVENT_TRACE_LOGFILEW logfile{};
        logfile.LoggerName = const_cast<LPWSTR>(session.name.c_str());
        logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
        logfile.EventRecordCallback = &record_callback;
        logfile.Context = &counts;
        trace = OpenTraceW(&logfile);
        if (trace == INVALID_PROCESSTRACE_HANDLE) {
            printf("OpenTrace failed: %lu\n", GetLastError());
            return 1;
        }
    }

    printf("capturing %d s on session %ls ... move USB devices now\n", seconds, session.name.c_str());
    HANDLE thread = nullptr;
    if (!file_mode) {
        thread = CreateThread(nullptr, 0,
            [](LPVOID p) -> DWORD {
                auto* t = reinterpret_cast<TRACEHANDLE*>(p);
                ProcessTrace(t, 1, nullptr, nullptr);
                return 0;
            },
            &trace, 0, nullptr);
    }
    Sleep(static_cast<DWORD>(seconds) * 1000);
    // Stop session first; this unblocks ProcessTrace.
    session.stop();
    if (thread != nullptr) {
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
    }
    if (trace != INVALID_PROCESSTRACE_HANDLE) CloseTrace(trace);

    if (file_mode) {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        const bool exists = GetFileAttributesExW(save_etl.c_str(), GetFileExInfoStandard, &info) != FALSE;
        ULARGE_INTEGER size{};
        if (exists) {
            size.LowPart = info.nFileSizeLow;
            size.HighPart = info.nFileSizeHigh;
        }
        printf("etl: %ls exists=%s bytes=%llu loss: events_lost=%lu buffers_written=%lu\n",
               save_etl.c_str(), exists ? "yes" : "NO",
               static_cast<unsigned long long>(size.QuadPart),
               props->EventsLost, props->BuffersWritten);
        const bool ok = exists && size.QuadPart > 0 && props->EventsLost == 0;
        printf("gate: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    const double span_us = (counts.last_qpc > counts.first_qpc && qpf.QuadPart > 0)
        ? (static_cast<double>(counts.last_qpc - counts.first_qpc) * 1e6 / static_cast<double>(qpf.QuadPart))
        : 0.0;
    printf("events ucx=%llu hub3=%llu other=%llu\n",
           static_cast<unsigned long long>(counts.ucx),
           static_cast<unsigned long long>(counts.hub3),
           static_cast<unsigned long long>(counts.other));
    printf("urb opcode start=%llu stop=%llu\n",
           static_cast<unsigned long long>(counts.urb_start),
           static_cast<unsigned long long>(counts.urb_stop));
    printf("span_us=%.0f distinct_usb_devices=%zu\n", span_us, counts.devices.size());
    printf("fields live: urb_ptr=%s device=%s pipe=%s descriptor=%s\n",
           counts.saw_urb_ptr_field ? "yes" : "no",
           counts.saw_device_field ? "yes" : "no",
           counts.saw_pipe_field ? "yes" : "no",
           counts.saw_descriptor_field ? "yes" : "no");
    printf("loss: events_lost=%lu buffers_written=%lu log_buffers_lost=%lu realtime_lost=%lu\n",
           props->EventsLost, props->BuffersWritten, props->LogBuffersLost, props->RealTimeBuffersLost);

    const bool loss_ok = props->EventsLost == 0 && props->RealTimeBuffersLost == 0;
    const bool traffic_ok = counts.ucx > 0;
    gate = gate && loss_ok && traffic_ok;
    printf("gate: %s (traffic=%s loss_free=%s)\n",
           gate ? "PASS" : "FAIL",
           traffic_ok ? "yes" : "NO - no UCX traffic during window",
           loss_ok ? "yes" : "NO");
    return gate ? 0 : 1;
}
