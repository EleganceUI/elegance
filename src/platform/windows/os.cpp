// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Core's backend (core/os_backend.h) on Windows. Its standard library has <stacktrace>, so the
// stack functions aren't needed here.

#include "core/os_backend.h"

#include <elg/error.h>

#include <process.h>
#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace elg::detail::os {

std::filesystem::path executable_path() {
    wchar_t buffer[MAX_PATH];
    if (const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH); length > 0 && length < MAX_PATH) {
        return std::wstring(buffer, length);
    }
    return {};
}

int process_id() noexcept {
    return _getpid();
}

namespace {

std::wstring widen(const std::string& text) {
    std::wstring out(text.size(), L'\0');
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                                      static_cast<int>(out.size()));
    out.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    return out;
}

// A command: cmd.exe in a job of its own, which every process it starts joins and which, closed,
// ends them all; and the read end of the pipe its output and errors go to.
struct Command {
    HANDLE process = nullptr;
    HANDLE job = nullptr;
    HANDLE output = nullptr;
    std::atomic<bool> ended{false};

    Command() = default;
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;
    ~Command() {
        for (HANDLE handle : {process, job, output}) {
            if (handle) {
                CloseHandle(handle);
            }
        }
    }
};

} // namespace

void* start_command(const std::string& command) {
    auto started = std::make_unique<Command>();
    // The shell's standard output and error: a pipe whose write end only it gets. Its input: NUL.
    SECURITY_ATTRIBUTES inherited{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE write_end = nullptr;
    if (!CreatePipe(&started->output, &write_end, &inherited, 0)) {
        return nullptr;
    }
    SetHandleInformation(started->output, HANDLE_FLAG_INHERIT, 0);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherited, OPEN_EXISTING, 0,
                               nullptr);
    const auto close_child_ends = [&] {
        CloseHandle(write_end);
        if (input != INVALID_HANDLE_VALUE) {
            CloseHandle(input);
        }
    };
    // The job ends every process in it when its last handle closes: when the command is
    // finished or ended, or this process exits.
    started->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!started->job || input == INVALID_HANDLE_VALUE ||
        !SetInformationJobObject(started->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
        close_child_ends();
        return nullptr;
    }

    // Only these two handles are inherited, whatever else other threads make inheritable.
    HANDLE handles[] = {write_end, input};
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<std::byte> storage(size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size) ||
        !UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles),
                                   nullptr, nullptr)) {
        close_child_ends();
        return nullptr;
    }
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = input;
    startup.StartupInfo.hStdOutput = write_end;
    startup.StartupInfo.hStdError = write_end;
    startup.lpAttributeList = attributes;
    // /s: cmd takes what's between the first and last quote as the command, quotes and all.
    std::wstring line = L"cmd.exe /d /s /c \"" + widen(command) + L"\"";
    PROCESS_INFORMATION info{};
    // Suspended until it's in the job, so that nothing it starts is outside it.
    const BOOL created = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                        CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                        nullptr, &startup.StartupInfo, &info);
    DeleteProcThreadAttributeList(attributes);
    close_child_ends(); // the shell has its copies: the output ends when they're closed
    if (!created) {
        return nullptr;
    }
    started->process = info.hProcess;
    if (!AssignProcessToJobObject(started->job, info.hProcess)) {
        TerminateProcess(info.hProcess, 1);
        CloseHandle(info.hThread);
        return nullptr;
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    return started.release();
}

std::size_t read_command_output(void* command, std::span<char> buffer) {
    auto& running = *static_cast<Command*>(command);
    if (running.ended) {
        return 0;
    }
    // Ends with the last process that holds the pipe: when the command exits, or end_command()
    // ends its job.
    DWORD read = 0;
    const auto size = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), 1u << 30));
    if (!ReadFile(running.output, buffer.data(), size, &read, nullptr)) {
        return 0;
    }
    return running.ended ? 0 : read;
}

void end_command(void* command) noexcept {
    auto& running = *static_cast<Command*>(command);
    running.ended = true;
    TerminateJobObject(running.job, 1);
}

int finish_command(void* command) noexcept {
    const std::unique_ptr<Command> running(static_cast<Command*>(command));
    WaitForSingleObject(running->process, INFINITE);
    DWORD status = 0;
    if (running->ended || !GetExitCodeProcess(running->process, &status)) {
        return -1;
    }
    return static_cast<int>(status);
}

// COM, in the multithreaded apartment: Elegance's threads decode images (WIC), present
// (DXGI, DirectComposition) and play media (Media Foundation).
bool begin_elegance_thread() noexcept {
    return SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
}

void end_elegance_thread(bool begun) noexcept {
    if (begun) {
        CoUninitialize();
    }
}

bool threads_end_before_static_destruction() noexcept {
#if defined(ELG_STATIC_DEFINE)
    return false; // linked into the program: exit() destroys them while its threads still run
#else
    return true; // a DLL: unloaded after ExitProcess has ended the other threads
#endif
}

bool debugger_present() noexcept {
    return IsDebuggerPresent() != 0;
}

void debug_break() noexcept {
    __debugbreak();
}

void* open_library(const std::filesystem::path& path) {
    void* const handle = LoadLibraryW(path.c_str());
    if (!handle) {
        throw ResourceError(std::format("can't load \"{}\" (error {})", path.string(), GetLastError()));
    }
    return handle;
}

void* library_symbol(void* library, const char* name) noexcept {
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
}

void close_library(void* library) noexcept {
    FreeLibrary(static_cast<HMODULE>(library));
}

} // namespace elg::detail::os
