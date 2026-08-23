#include "pch.h"
#include ".\Soulworker Packet\HookCommand.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Util\Log.h"
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace {

constexpr int kQueueSize = 8;
constexpr DWORD kFrameLen = 9;
constexpr DWORD kKeepAliveMs = 1000;

// Ignored by the hook. Exists so a dead peer is noticed by a failing write
// rather than at the next hotkey press - otherwise a game restart leaves this
// side connected to nothing and the new hook unable to take the instance.
constexpr uint8_t kOpKeepAlive = 0;

struct Command {
    uint8_t op;
    uint32_t arg;
};

volatile LONG g_running = 1;

CRITICAL_SECTION g_cs;
bool g_csReady = false;
HANDLE g_wake = nullptr;
volatile LONG g_connected = 0;

Command g_queue[kQueueSize];
int g_qHead = 0;
int g_qTail = 0;

// Same reasoning as the capture pipe: the meter is elevated and the game is not.
const wchar_t* kPipeSddl = L"D:(A;;GA;;;WD)S:(ML;;NW;;;LW)";

PSECURITY_DESCRIPTOR g_pipeSd = nullptr;

SECURITY_ATTRIBUTES* PipeSecurity(SECURITY_ATTRIBUTES* sa) {
    if (!g_pipeSd) {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kPipeSddl, SDDL_REVISION_1,
                                                                  &g_pipeSd, nullptr))
            return nullptr;
    }
    sa->nLength = sizeof(*sa);
    sa->lpSecurityDescriptor = g_pipeSd;
    sa->bInheritHandle = FALSE;
    return sa;
}

bool PopCommand(Command* out) {
    EnterCriticalSection(&g_cs);
    bool has = g_qHead != g_qTail;
    if (has) {
        *out = g_queue[g_qTail];
        g_qTail = (g_qTail + 1) % kQueueSize;
    }
    LeaveCriticalSection(&g_cs);
    return has;
}

bool WriteCommand(HANDLE hPipe, uint8_t op, uint32_t arg) {
    uint8_t frame[kFrameLen];
    *(uint32_t*)frame = 5;
    frame[4] = op;
    *(uint32_t*)(frame + 5) = arg;

    DWORD written = 0;
    return WriteFile(hPipe, frame, kFrameLen, &written, nullptr) && written == kFrameLen;
}

void ClearQueue() {
    EnterCriticalSection(&g_cs);
    g_qTail = g_qHead;
    LeaveCriticalSection(&g_cs);
}

// Outbound only: a dead hook shows up as a failed write, not a closed read.
DWORD WINAPI CommandServerThread(LPVOID) {
    while (g_running) {
        SECURITY_ATTRIBUTES sa;
        HANDLE hPipe = CreateNamedPipeW(
            HOOK_COMMAND_PIPE_NAME, PIPE_ACCESS_OUTBOUND,
            PIPE_TYPE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
            4096, 0, 0, PipeSecurity(&sa));
        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(1000);
            continue;
        }

        BOOL connected = ConnectNamedPipe(hPipe, nullptr);
        if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
            CloseHandle(hPipe);
            Sleep(100);
            continue;
        }

        // Anything queued while disconnected is stale by now.
        ClearQueue();
        InterlockedExchange(&g_connected, 1);
        LogInstance.WriteLog("Hook command channel connected");

        ULONGLONG lastKeepAlive = GetTickCount64();
        bool broken = false;

        while (g_running && !broken) {
            WaitForSingleObject(g_wake, 250);

            Command cmd;
            while (!broken && PopCommand(&cmd))
                broken = !WriteCommand(hPipe, cmd.op, cmd.arg);

            ULONGLONG now = GetTickCount64();
            if (!broken && now - lastKeepAlive >= kKeepAliveMs) {
                lastKeepAlive = now;
                broken = !WriteCommand(hPipe, kOpKeepAlive, 0);
            }
        }

        InterlockedExchange(&g_connected, 0);
        DisconnectNamedPipe(hPipe);
        CloseHandle(hPipe);
    }
    return 0;
}

} // namespace

DWORD HookCommandStart() {
    if (!g_csReady) {
        InitializeCriticalSection(&g_cs);
        g_csReady = true;
    }
    if (!g_wake) {
        g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_wake)
            return GetLastError();
    }
    HANDLE h = CreateThread(nullptr, 0, CommandServerThread, nullptr, 0, nullptr);
    return (h != nullptr) ? ERROR_SUCCESS : GetLastError();
}

bool HookCommandIsConnected() {
    return InterlockedCompareExchange(&g_connected, 0, 0) != 0;
}

// Queued rather than written here: this runs on the UI thread holding the damage
// meter lock, and a wedged hook must not stall the overlay.
bool HookCommandSend(uint8_t op, uint32_t arg) {
    if (!g_csReady || !HookCommandIsConnected())
        return false;

    EnterCriticalSection(&g_cs);
    int next = (g_qHead + 1) % kQueueSize;
    bool ok = next != g_qTail;
    if (ok) {
        g_queue[g_qHead].op = op;
        g_queue[g_qHead].arg = arg;
        g_qHead = next;
    }
    LeaveCriticalSection(&g_cs);

    if (ok)
        SetEvent(g_wake);
    return ok;
}

void HookCommandRestartMaze() {
    if (DAMAGEMETER.isTownMap())
        return;

    uint32_t id = DAMAGEMETER.GetMyID();
    if (!id)
        return;

    if (!HookCommandSend(HOOK_CMD_RESTART_MAZE, id))
        LogInstance.WriteLog("Restart maze hotkey: hook not connected");
}

void HookCommandExitMaze() {
    if (DAMAGEMETER.isTownMap())
        return;

    if (!HookCommandSend(HOOK_CMD_EXIT_MAZE, 0))
        LogInstance.WriteLog("Exit maze hotkey: hook not connected");
}
