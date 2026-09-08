#pragma once

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstddef>

namespace winchisel::platform::detail {

struct ProcessWaitResult {
    DWORD exit_code{};
    bool timed_out{};
};

inline HANDLE attach_kill_job(HANDLE process) {
    const auto job = CreateJobObjectW(nullptr, nullptr);
    if (!job) return nullptr;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(job, process)) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

inline ProcessWaitResult wait_process(HANDLE process, DWORD timeout_ms) {
    const auto job = attach_kill_job(process);
    if (WaitForSingleObject(process, timeout_ms) == WAIT_TIMEOUT) {
        if (job) TerminateJobObject(job, ERROR_TIMEOUT); else TerminateProcess(process, ERROR_TIMEOUT);
        WaitForSingleObject(process, 5000);
        if (job) CloseHandle(job);
        return {ERROR_TIMEOUT, true};
    }
    DWORD code{};
    const auto result = GetExitCodeProcess(process, &code) ? ProcessWaitResult{code, false}
                                                            : ProcessWaitResult{GetLastError(), false};
    if (job) CloseHandle(job);
    return result;
}

template <typename Consumer>
ProcessWaitResult wait_process_with_pipe(HANDLE process, HANDLE pipe, DWORD timeout_ms, Consumer&& consume) {
    const auto job = attach_kill_job(process);
    const auto started = GetTickCount64();
    std::array<char, 4096> buffer{};
    bool process_done{};
    for (;;) {
        DWORD available{};
        if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) && available) {
            DWORD read{};
            const auto requested = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (ReadFile(pipe, buffer.data(), requested, &read, nullptr) && read) consume(buffer.data(), read);
            continue;
        }
        if (!process_done && WaitForSingleObject(process, 20) == WAIT_OBJECT_0) process_done = true;
        if (process_done) {
            if (!available) break;
            continue;
        }
        if (GetTickCount64() - started >= timeout_ms) {
            if (job) TerminateJobObject(job, ERROR_TIMEOUT); else TerminateProcess(process, ERROR_TIMEOUT);
            WaitForSingleObject(process, 5000);
            if (job) CloseHandle(job);
            return {ERROR_TIMEOUT, true};
        }
    }
    DWORD code{};
    const auto result = GetExitCodeProcess(process, &code) ? ProcessWaitResult{code, false}
                                                            : ProcessWaitResult{GetLastError(), false};
    if (job) CloseHandle(job);
    return result;
}

} // namespace winchisel::platform::detail
