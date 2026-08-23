#include "pch.h"
#include ".\Soulworker Packet\Injector.h"
#include ".\Util\Log.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <map>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace {

volatile LONG g_injectorRunning = 1;

constexpr DWORD kPollMs = 20;
constexpr int kMaxProbeFails = 5;

const wchar_t* kGameExeCandidates[] = {
    L"SoulWorker.exe",
    L"Soulworker.exe",
    L"soulworker.exe",
    L"SoulWorker_Client.exe",
    L"soulworker_client.exe",
};

const wchar_t* kHookDllName = L"SoulMeterHook.dll";

std::wstring GetHookDllPath() {
    wchar_t buf[MAX_PATH] = { 0 };
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L'\\');
    if (slash != std::wstring::npos)
        path.resize(slash + 1);
    return path + kHookDllName;
}

bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp = { 0 };
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = false;
    if (LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid)) {
        ok = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
             GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(hToken);
    return ok;
}

enum class Probe { NotGame, Game, Unknown };

Probe ProbeProcess(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return Probe::Unknown;

    wchar_t image[MAX_PATH] = { 0 };
    DWORD chars = MAX_PATH;
    Probe result = Probe::Unknown;

    if (QueryFullProcessImageNameW(h, 0, image, &chars)) {
        const wchar_t* base = wcsrchr(image, L'\\');
        base = base ? base + 1 : image;
        result = Probe::NotGame;
        for (const wchar_t* candidate : kGameExeCandidates) {
            if (_wcsicmp(base, candidate) == 0) {
                result = Probe::Game;
                break;
            }
        }
    }

    CloseHandle(h);
    return result;
}

enum class Target { NotReady, Ready, AlreadyHooked, WrongArch };

// A freshly created process has no module list until ntdll has finished loader
// init, so the module snapshot failing doubles as the "safe to inject yet" gate.
Target InspectTarget(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (h) {
        BOOL wow64 = FALSE;
        BOOL known = IsWow64Process(h, &wow64);
        CloseHandle(h);
        if (known && wow64)
            return Target::WrongArch;
    }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return Target::NotReady;

    MODULEENTRY32W me = { sizeof(me) };
    bool haveKernel32 = false;
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, kHookDllName) == 0) {
                CloseHandle(snap);
                return Target::AlreadyHooked;
            }
            if (_wcsicmp(me.szModule, L"kernel32.dll") == 0)
                haveKernel32 = true;
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);

    return haveKernel32 ? Target::Ready : Target::NotReady;
}

bool InjectInto(DWORD pid, const std::wstring& dllPath) {
    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                                  PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                              FALSE, pid);
    if (!hProc)
        return false;

    SIZE_T size = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(hProc, nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        CloseHandle(hProc);
        return false;
    }

    if (!WriteProcessMemory(hProc, remote, dllPath.c_str(), size, nullptr)) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!hThread) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return false;
    }

    DWORD wait = WaitForSingleObject(hThread, 5000);
    DWORD exitCode = 0;
    if (wait == WAIT_OBJECT_0)
        GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    if (wait == WAIT_OBJECT_0)
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);

    return wait == WAIT_OBJECT_0 && exitCode != 0;
}

struct ProcState {
    bool isGame = false;
    bool done = false;
    int probeFails = 0;
    int attempts = 0;
    ULONGLONG nextAttemptTick = 0;
};

ULONGLONG BackoffMs(int attempts) {
    ULONGLONG ms = 250ull * (ULONGLONG)attempts;
    return ms > 3000 ? 3000 : ms;
}

void ServiceGameProcess(DWORD pid, ProcState& st, const std::wstring& dllPath) {
    if (GetTickCount64() < st.nextAttemptTick)
        return;

    switch (InspectTarget(pid)) {
    case Target::WrongArch:
    case Target::AlreadyHooked:
        st.done = true;
        return;
    case Target::NotReady:
        return;
    case Target::Ready:
        break;
    }

    st.attempts++;
    if (InjectInto(pid, dllPath)) {
        st.done = true;
        return;
    }

    st.nextAttemptTick = GetTickCount64() + BackoffMs(st.attempts);
}

DWORD WINAPI InjectorThread(LPVOID) {
    std::wstring dllPath = GetHookDllPath();
    EnableDebugPrivilege();

    std::map<DWORD, ProcState> procs;
    std::vector<DWORD> pids(4096);

    while (g_injectorRunning) {
        DWORD needed = 0;
        if (EnumProcesses(pids.data(), (DWORD)(pids.size() * sizeof(DWORD)), &needed)) {
            DWORD count = needed / sizeof(DWORD);
            if (count == pids.size()) {
                pids.resize(pids.size() * 2);
                continue;
            }

            std::map<DWORD, ProcState> alive;
            for (DWORD i = 0; i < count; i++) {
                DWORD pid = pids[i];
                if (pid == 0)
                    continue;

                auto it = procs.find(pid);
                ProcState st = (it != procs.end()) ? it->second : ProcState();

                if ((it == procs.end() || (!st.isGame && st.probeFails > 0)) &&
                    st.probeFails < kMaxProbeFails) {
                    Probe p = ProbeProcess(pid);
                    if (p == Probe::Game) {
                        st.isGame = true;
                        st.probeFails = 0;
                    } else if (p == Probe::NotGame) {
                        st.isGame = false;
                        st.probeFails = kMaxProbeFails;
                    } else {
                        st.probeFails++;
                    }
                }

                if (st.isGame && !st.done)
                    ServiceGameProcess(pid, st, dllPath);

                alive.emplace(pid, st);
            }
            procs.swap(alive);
        }

        Sleep(kPollMs);
    }

    return 0;
}

} // namespace

DWORD InjectorStart() {
    HANDLE h = CreateThread(nullptr, 0, InjectorThread, nullptr, 0, nullptr);
    if (h == nullptr)
        return GetLastError();
    CloseHandle(h);
    return ERROR_SUCCESS;
}
