#include "pch.h"
#include ".\Soulworker Packet\PipeReceiver.h"
#include ".\Soulworker Packet\SWPacketMaker.h"
#include ".\Util\Log.h"
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace {

volatile LONG g_pipeRunning = 1;
volatile LONG g_connected = 0;

constexpr uint32_t kMaxMessage = 4 * 1024 * 1024;

// The meter runs elevated and the game does not, so a pipe created with the
// default security descriptor is unreachable from the hook. Grant Everyone
// access and drop the object's mandatory label to Low.
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

void HandleMessage(const uint8_t* data, uint32_t len) {
    if (len < sizeof(SWHEADER) || len > kMaxMessage)
        return;
    std::vector<unsigned char> packet(data, data + len);
    SWPACKETMAKER.CreateSWPacket(packet);
}

bool ReadAll(HANDLE hPipe, uint8_t* buf, uint32_t len) {
    uint32_t off = 0;
    while (off < len) {
        DWORD rd = 0;
        if (!ReadFile(hPipe, buf + off, len - off, &rd, nullptr) || rd == 0)
            return false;
        off += rd;
    }
    return true;
}

DWORD WINAPI PipeServerThread(LPVOID) {
    while (g_pipeRunning) {
        SECURITY_ATTRIBUTES sa;
        HANDLE hPipe = CreateNamedPipeW(
            PIPE_RECEIVER_PIPE_NAME, PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
            0, 4 * 1024 * 1024, 0, PipeSecurity(&sa));
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

        InterlockedExchange(&g_connected, 1);

        for (;;) {
            uint32_t len = 0;
            if (!ReadAll(hPipe, (uint8_t*)&len, 4))
                break;
            if (len == 0 || len > kMaxMessage)
                break;
            std::vector<uint8_t> buf(len);
            if (!ReadAll(hPipe, buf.data(), len))
                break;
            HandleMessage(buf.data(), len);
        }
        CloseHandle(hPipe);
        InterlockedExchange(&g_connected, 0);
    }
    return 0;
}

} // namespace

DWORD PipeReceiverStart() {
    HANDLE h = CreateThread(nullptr, 0, PipeServerThread, nullptr, 0, nullptr);
    return (h != nullptr) ? ERROR_SUCCESS : GetLastError();
}

bool PipeReceiverIsConnected() {
    return InterlockedCompareExchange(&g_connected, 0, 0) != 0;
}
