// Injectable capture DLL. Detours the game's own packet (de)serialisers and
// forwards complete plaintext frames to SoulMeter.exe over the named pipe
// \\.\pipe\SoulMeterHook as [uint32 LE length][frame bytes]. Never blocks the
// game's network thread: the hooks only push into an in-process queue, and a
// dedicated writer thread drains it to the pipe.
//
// Several meters may be open at once, each one its own server instance of the
// pipe, so both channels are one-to-many: every captured frame is written to
// all of them, and every meter gets its own command reader.

#include <windows.h>
#include <cstdint>

#include "blockcache.h"
#include "gamecmd.h"
#include "loadopt.h"
#include "md5cache.h"
#include "sockethooks.h"
#include "stream.h"

namespace {

const wchar_t* kPipeName = L"\\\\.\\pipe\\SoulMeterHook";
const wchar_t* kCmdPipeName = L"\\\\.\\pipe\\SoulMeterHookCmd";
constexpr size_t kBatchCap = 256 * 1024;
constexpr DWORD kMaxCmdLen = 64;
constexpr int kMaxMeters = 8;
constexpr ULONGLONG kProbeIntervalMs = 250;

volatile LONG g_running = 1;

struct MeterLink {
    HANDLE h;
    ULONG pid;
};

// Touched only by the writer thread.
MeterLink g_meters[kMaxMeters];
int g_meterCount = 0;
ULONGLONG g_nextProbe = 0;

bool KnownMeter(ULONG pid) {
    for (int i = 0; i < g_meterCount; i++) {
        if (g_meters[i].pid == pid)
            return true;
    }
    return false;
}

void DropMeter(int i) {
    CloseHandle(g_meters[i].h);
    g_meters[i] = g_meters[--g_meterCount];
}

// A meter keeps exactly one listener pending, so an extra CreateFileW only
// lands on a meter nothing is attached to yet. Dedup by the server's pid all
// the same: a meter that drops a connection and re-listens while our handle to
// it is still open would otherwise be counted twice and get every frame twice.
void ProbeForMeters() {
    ULONGLONG now = GetTickCount64();
    if (now < g_nextProbe)
        return;
    g_nextProbe = now + kProbeIntervalMs;

    while (g_meterCount < kMaxMeters) {
        HANDLE h = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return;

        ULONG pid = 0;
        if (!GetNamedPipeServerProcessId(h, &pid) || KnownMeter(pid)) {
            CloseHandle(h);
            return;
        }

        g_meters[g_meterCount].h = h;
        g_meters[g_meterCount].pid = pid;
        g_meterCount++;
    }
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

        ProbeForMeters();
        if (g_meterCount == 0) {
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

        // Backwards: DropMeter fills the hole with the last entry.
        for (int i = g_meterCount - 1; i >= 0; i--) {
            if (!WriteAll(g_meters[i].h, batch, (DWORD)used))
                DropMeter(i);
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

CRITICAL_SECTION g_cmdCs;
ULONG g_cmdPids[kMaxMeters];
int g_cmdCount = 0;

// False when this meter is already attached, or when there is no room left.
bool ClaimCmdMeter(ULONG pid) {
    EnterCriticalSection(&g_cmdCs);
    bool claimed = g_cmdCount < kMaxMeters;
    for (int i = 0; claimed && i < g_cmdCount; i++) {
        if (g_cmdPids[i] == pid)
            claimed = false;
    }
    if (claimed)
        g_cmdPids[g_cmdCount++] = pid;
    LeaveCriticalSection(&g_cmdCs);
    return claimed;
}

void ReleaseCmdMeter(ULONG pid) {
    EnterCriticalSection(&g_cmdCs);
    for (int i = 0; i < g_cmdCount; i++) {
        if (g_cmdPids[i] == pid) {
            g_cmdPids[i] = g_cmdPids[--g_cmdCount];
            break;
        }
    }
    LeaveCriticalSection(&g_cmdCs);
}

struct CmdLink {
    HANDLE h;
    ULONG pid;
};

// One reader per meter: the read blocks, so a hotkey pressed in one meter must
// not sit behind another meter's idle channel.
DWORD WINAPI CommandReaderThread(LPVOID param) {
    CmdLink* link = (CmdLink*)param;

    for (;;) {
        uint32_t len = 0;
        if (!ReadAll(link->h, (uint8_t*)&len, 4))
            break;
        if (len < 5 || len > kMaxCmdLen)
            break;
        uint8_t body[kMaxCmdLen];
        if (!ReadAll(link->h, body, len))
            break;
        GameCmdPost(body[0], *(uint32_t*)(body + 1));
    }

    CloseHandle(link->h);
    ReleaseCmdMeter(link->pid);
    delete link;
    return 0;
}

// Hotkey commands from the meters. Separate from the capture pipe so a command
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

        ULONG pid = 0;
        if (!GetNamedPipeServerProcessId(h, &pid) || !ClaimCmdMeter(pid)) {
            CloseHandle(h);
            Sleep(500);
            continue;
        }

        CmdLink* link = new CmdLink{ h, pid };
        HANDLE reader = CreateThread(nullptr, 0, CommandReaderThread, link, 0, nullptr);
        if (reader) {
            CloseHandle(reader);
            // Straight back around: another meter may be listening right now.
        } else {
            delete link;
            CloseHandle(h);
            ReleaseCmdMeter(pid);
            Sleep(500);
        }
    }
    return 0;
}

DWORD WINAPI SetupThread(LPVOID) {
    InitializeCriticalSection(&g_cmdCs);

    // Before waiting on SoulWorker64.dll: the archives are mounted during
    // engine init, which is over before the netMgr exists.
    BlockCacheInstall();

    // Injection happens ~30ms after process start, so SoulWorker64.dll is not
    // loaded yet and its netMgr is constructed later still.
    while (g_running && !HookInstall())
        Sleep(100);
    if (!g_running)
        return 0;

    // HookInstall succeeding means SoulWorker64.dll is mapped, which is all the
    // image patches need.
    LoadOptApply();

    // Must be armed before the client starts hashing the archives, which is
    // ~30s into a cold start -- long after SoulWorker64.dll is mapped.
    Md5CacheInstall();

    HANDLE hWriter = CreateThread(nullptr, 0, WriterThread, nullptr, 0, nullptr);
    if (hWriter)
        CloseHandle(hWriter);

    HANDLE hCmd = CreateThread(nullptr, 0, CommandThread, nullptr, 0, nullptr);
    if (hCmd)
        CloseHandle(hCmd);

    while (g_running)
        Sleep(1000);
    HookUninstall();
    Md5CacheShutdown();
    BlockCacheShutdown();
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
        while (g_meterCount > 0)
            DropMeter(g_meterCount - 1);
    }
    return TRUE;
}
