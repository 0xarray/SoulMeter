// Injectable capture DLL. Detours the game's own packet (de)serialisers and
// forwards complete plaintext frames to SoulMeter.exe over the named pipe
// \\.\pipe\SoulMeterHook as [uint32 LE length][frame bytes]. Never blocks the
// game's network thread: the hooks only push into an in-process queue, and a
// dedicated writer thread drains it to the pipe.

#include <windows.h>
#include <cstdint>

#include "gamecmd.h"
#include "loadopt.h"
#include "loadprof.h"
#include "sockethooks.h"
#include "stream.h"

namespace {

const wchar_t* kPipeName = L"\\\\.\\pipe\\SoulMeterHook";
const wchar_t* kCmdPipeName = L"\\\\.\\pipe\\SoulMeterHookCmd";
constexpr size_t kBatchCap = 256 * 1024;
constexpr DWORD kMaxCmdLen = 64;

volatile LONG g_running = 1;
HANDLE g_hPipe = INVALID_HANDLE_VALUE;

bool ConnectPipe() {
    if (g_hPipe != INVALID_HANDLE_VALUE)
        return true;

    for (int i = 0; i < 300 && g_running; i++) {
        HANDLE h = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            g_hPipe = h;
            return true;
        }
        if (GetLastError() == ERROR_PIPE_BUSY)
            WaitNamedPipeW(kPipeName, 1000);
        Sleep(100);
    }
    return false;
}

bool WriteAll(HANDLE h, const uint8_t* data, DWORD len) {
    DWORD off = 0;
    while (off < len) {
        DWORD written = 0;
        if (!WriteFile(h, data + off, len - off, &written, nullptr) || written == 0)
            return false;
        off += written;
    }
    return true;
}

DWORD WINAPI WriterThread(LPVOID) {
    uint8_t* batch = new uint8_t[kBatchCap];
    LONG64 lastPingEmit = 0;

    while (g_running) {
        LONG64 now = GetTickCount64();

        if (!ConnectPipe()) {
            Sleep(250);
            continue;
        }

        if (now - lastPingEmit >= 1000) {
            lastPingEmit = now;
            uint8_t pingFrame[13];
            size_t pingLen = 0;
            BuildPingFrame(pingFrame, &pingLen);
            if (g_frameQueue.PushFrame(pingFrame, (uint32_t)pingLen))
                InterlockedExchange64(&g_lastPingAt, now);
        }

        if (g_frameQueue.Available() == 0) {
            Sleep(2);
            continue;
        }

        size_t used = 0;
        for (;;) {
            if (kBatchCap - used < SMH_MAX_RECORD)
                break;
            uint32_t n = g_frameQueue.PopRecord(batch + used, (uint32_t)(kBatchCap - used));
            if (n == 0)
                break;
            used += n;
        }
        if (used == 0)
            continue;

        if (!WriteAll(g_hPipe, batch, (DWORD)used)) {
            CloseHandle(g_hPipe);
            g_hPipe = INVALID_HANDLE_VALUE;
        }
    }

    delete[] batch;
    return 0;
}

bool ReadAll(HANDLE h, uint8_t* buf, DWORD len) {
    DWORD off = 0;
    while (off < len) {
        DWORD rd = 0;
        if (!ReadFile(h, buf + off, len - off, &rd, nullptr) || rd == 0)
            return false;
        off += rd;
    }
    return true;
}

// Hotkey commands from the meter. Separate from the capture pipe so a command
// channel that never connects cannot disturb the packet stream.
DWORD WINAPI CommandThread(LPVOID) {
    while (g_running) {
        // Arming needs the game window, which exists long before the meter has
        // anything to send.
        if (!GameCmdInit()) {
            Sleep(500);
            continue;
        }

        HANDLE h = CreateFileW(kCmdPipeName, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            Sleep(500);
            continue;
        }

        for (;;) {
            uint32_t len = 0;
            if (!ReadAll(h, (uint8_t*)&len, 4))
                break;
            if (len < 5 || len > kMaxCmdLen)
                break;
            uint8_t body[kMaxCmdLen];
            if (!ReadAll(h, body, len))
                break;
            GameCmdPost(body[0], *(uint32_t*)(body + 1));
        }
        CloseHandle(h);
    }
    return 0;
}

DWORD WINAPI SetupThread(LPVOID) {
    // Before waiting on SoulWorker64.dll: the archive mounting this measures
    // happens during engine init, which is over before the netMgr exists.
    LoadProfInstall();

    // Injection happens ~30ms after process start, so SoulWorker64.dll is not
    // loaded yet and its netMgr is constructed later still.
    while (g_running && !HookInstall())
        Sleep(100);
    if (!g_running)
        return 0;

    // HookInstall succeeding means SoulWorker64.dll is mapped, which is all the
    // image patches need.
    LoadOptApply();

    HANDLE hWriter = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (hWriter)
        CloseHandle(hWriter);

    HANDLE hCmd = CreateThread(nullptr, 0, CommandThread, nullptr, 0, nullptr);
    if (hCmd)
        CloseHandle(hCmd);

    while (g_running) {
        Sleep(1000);
        LoadProfDumpReport();
    }
    HookUninstall();
    LoadProfShutdown();
    GameCmdShutdown();
    return 0;
}

} // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE h = CreateThread(nullptr, 0, SetupThread, nullptr, 0, nullptr);
        if (h)
            CloseHandle(h);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_running, 0);
        if (g_hPipe != INVALID_HANDLE_VALUE) {
            CloseHandle(g_hPipe);
            g_hPipe = INVALID_HANDLE_VALUE;
        }
    }
    return TRUE;
}
