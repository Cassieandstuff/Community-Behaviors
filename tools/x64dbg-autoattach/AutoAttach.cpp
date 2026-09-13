// ============================================================================
// CBDebugAutoAttach — DEV-ONLY SKSE plugin. NOT part of Community Behaviors.
//
// Purpose: the instant this plugin loads (well before the engine's first
// LoadClips), spawn x64dbg ALREADY ATTACHED to this process and block until the
// debugger is present. This lets you catch early breakpoints (e.g. the LoadClips
// call at skyrimse.exe+0xbc22e1) WITHOUT racing the launch — you launch the game
// normally through MO2, and x64dbg comes to you.
//
// Why a separate plugin (not code inside CB): CB's DLL is deployed over the live
// MO2 mod, so auto-launching a debugger from it would be a disaster for end users.
// This target is EXCLUDE_FROM_ALL, opt-in (-DCB_BUILD_AUTOATTACH=ON), and is NEVER
// referenced by cb_deploy — it physically cannot ship with CB. You ARM it by
// dropping this DLL into a dev-only MO2 mod's SKSE/Plugins/, and DISARM it by
// removing the DLL. Its mere presence is the switch (further guarded: it no-ops
// unless an x64dbg path is configured).
//
// Mechanism: a process cannot debug itself, so we launch `x64dbg -p <ourPID>` and
// wait for it to attach. x64dbg is spawned DETACHED + BREAKAWAY so it is not tied
// to (nor killed with) MO2's job object.
//
// Config (dev-machine specific — never hardcoded):
//   1) X64DBG_PATH environment variable  = full path to x64dbg.exe, OR
//   2) a "CBDebugAutoAttach.txt" sidecar next to this DLL, first line = the path.
//   Disable without removing the DLL: set X64DBG_PATH empty / delete the sidecar.
// ============================================================================
#include <Windows.h>

#include <SKSE/SKSE.h>   // SKSEPluginLoad macro (SKSE/Interfaces.h) + SKSE::Init

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {
    // Never soft-lock the game if x64dbg is missing / gets closed before attaching.
    constexpr auto kAttachTimeout = 60s;

    void Log(const std::string& msg) {
        // OutputDebugString shows up in x64dbg's own log and in DebugView — fitting
        // for a debug-only tool, and needs no log-file setup.
        const std::string line = "[CBDebugAutoAttach] " + msg + "\n";
        ::OutputDebugStringA(line.c_str());
    }

    std::wstring TrimPath(std::wstring s) {
        while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' ||
                              s.back() == L' '  || s.back() == L'\t')) {
            s.pop_back();
        }
        if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
            s = s.substr(1, s.size() - 2);
        }
        return s;
    }

    // Directory containing THIS DLL (so the sidecar config sits next to the plugin).
    std::wstring SelfDir() {
        wchar_t  path[MAX_PATH]{};
        HMODULE  self = nullptr;
        if (::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&SelfDir), &self) &&
            ::GetModuleFileNameW(self, path, MAX_PATH)) {
            std::wstring dir(path);
            if (const auto slash = dir.find_last_of(L"\\/"); slash != std::wstring::npos) {
                dir.resize(slash);
            }
            return dir;
        }
        return {};
    }

    std::wstring ResolveDebuggerPath() {
        // 1) explicit env var
        wchar_t env[MAX_PATH]{};
        if (const DWORD n = ::GetEnvironmentVariableW(L"X64DBG_PATH", env, MAX_PATH);
            n > 0 && n < MAX_PATH) {
            return TrimPath(env);
        }
        // 2) sidecar file next to the DLL
        const std::wstring dir = SelfDir();
        if (!dir.empty()) {
            const std::wstring cfg = dir + L"\\CBDebugAutoAttach.txt";
            if (std::FILE* f = ::_wfopen(cfg.c_str(), L"rt")) {
                wchar_t      line[MAX_PATH]{};
                std::wstring result;
                if (std::fgetws(line, MAX_PATH, f)) {
                    result = TrimPath(line);
                }
                std::fclose(f);
                return result;
            }
        }
        return {};
    }

    bool LaunchDebuggerAttached(const std::wstring& x64dbg) {
        const std::wstring cmd =
            L"\"" + x64dbg + L"\" -p " + std::to_wstring(::GetCurrentProcessId());

        auto spawn = [&](DWORD flags) -> bool {
            STARTUPINFOW        si{ sizeof(si) };
            PROCESS_INFORMATION pi{};
            std::wstring        mutableCmd = cmd;  // CreateProcessW may write to the buffer
            if (::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, flags,
                                 nullptr, nullptr, &si, &pi)) {
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
                return true;
            }
            return false;
        };

        // Prefer breakaway so x64dbg escapes MO2's job object; if the job forbids
        // breakaway, CreateProcess fails with that flag — fall back to a plain detach.
        if (spawn(DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB)) {
            return true;
        }
        Log("breakaway spawn failed (err " + std::to_string(::GetLastError()) +
            "), retrying without CREATE_BREAKAWAY_FROM_JOB");
        return spawn(DETACHED_PROCESS);
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse);

    if (::IsDebuggerPresent()) {
        Log("a debugger is already attached — nothing to do");
        return true;
    }

    const std::wstring x64dbg = ResolveDebuggerPath();
    if (x64dbg.empty()) {
        // Safe no-op: if this DLL is ever present without config, do nothing.
        Log("no x64dbg path configured (set X64DBG_PATH env var or a CBDebugAutoAttach.txt "
            "sidecar) — auto-attach disabled");
        return true;
    }

    Log("launching x64dbg attached to this process (pid " +
        std::to_string(::GetCurrentProcessId()) + ")...");
    if (!LaunchDebuggerAttached(x64dbg)) {
        Log("failed to launch x64dbg (err " + std::to_string(::GetLastError()) + ")");
        return true;
    }

    const auto deadline = std::chrono::steady_clock::now() + kAttachTimeout;
    while (!::IsDebuggerPresent() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(100ms);
    }

    if (::IsDebuggerPresent()) {
        Log("debugger attached — breaking now so you can arm breakpoints (continue to proceed "
            "into load / LoadClips)");
        __debugbreak();  // lands you in x64dbg right here
    } else {
        Log("timed out waiting for x64dbg to attach — continuing without a debugger");
    }
    return true;
}
