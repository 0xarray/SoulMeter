// Wire frame: magic(2) total_length(2, LE) flag(1) seq(1) body(N)
// body = [cat][cmd][padLen][pad][payload]
// The meter's const byte is 1=recv / 2=send, the reverse of the wire flag, so
// byte[4] is normalized by hook direction.

#include "sockethooks.h"

#include <MinHook.h>

ByteQueue g_frameQueue(8 * 1024 * 1024);
volatile LONG g_pingMs = 0;
volatile LONG64 g_lastPingAt = 0;

namespace {

// SoulWorker64.dll, imagebase 0x180000000, GB build 01317b93.
// Only the netMgr global is pinned; both targets come from its vtable.
constexpr uint32_t kNetMgrRva = 0x195D178;

constexpr int kVtSlotSerialize = 1;
constexpr int kVtSlotDeobf = 2;

constexpr int kPktOffFlag = 4;
constexpr int kPktOffBodyPtr = 6;
constexpr int kPktOffBodyLen = 18;

constexpr size_t kMaxFrame = 65535;
constexpr size_t kMinFrame = SMH_HEADER_SIZE + 3;

typedef char(__fastcall* SerializeFn)(void*, uint32_t, uint8_t*, uint8_t*, uint16_t*);
typedef char(__fastcall* DeobfFn)(void*, uint32_t, uint32_t, uint8_t*, uint8_t*);

SerializeFn OrigSerialize = nullptr;
DeobfFn OrigDeobf = nullptr;

volatile LONG g_live = 0;
bool g_mhReady = false;

// Heartbeat (cat=1, cmd=6) is one request/one reply at ~1/sec. Pairing on
// arrival order avoids needing the pad length (payload starts at 9 + frame[8])
// and does not assume the server echoes our tick back. The send hook runs on
// the game thread and the recv hook on the IOCP worker, so the pending
// timestamp is handed between them atomically; 0 means nothing outstanding.
volatile LONG64 g_hbSentAt = 0;

inline LONG64 NowMs() { return (LONG64)GetTickCount64(); }

void DetectHeartbeat(const uint8_t* frame, size_t size, uint8_t dir) {
    if (size < 9)
        return;
    if (frame[6] != 0x01 || frame[7] != 0x06)
        return;

    if (dir == 2) {
        InterlockedExchange64(&g_hbSentAt, NowMs());
        return;
    }

    LONG64 sentAt = InterlockedExchange64(&g_hbSentAt, 0);
    if (!sentAt)
        return;
    LONG64 rtt = NowMs() - sentAt;
    if (rtt < 0 || rtt >= 60000)
        return;
    InterlockedExchange(&g_pingMs, (LONG)rtt);
}

void EmitFrame(const uint8_t* src, size_t len, uint8_t dir) {
    if (!src || len < kMinFrame || len > kMaxFrame)
        return;
    static thread_local uint8_t frame[kMaxFrame];
    memcpy(frame, src, len);
    frame[kPktOffFlag] = dir;
    DetectHeartbeat(frame, len, dir);
    g_frameQueue.PushFrame(frame, (uint32_t)len);
}

char __fastcall HookedDeobf(void* self, uint32_t mode, uint32_t seq, uint8_t* src, uint8_t* dst) {
    char ret = OrigDeobf(self, mode, seq, src, dst);
    __try {
        if (ret && src && dst)
            EmitFrame(dst, *(uint16_t*)(src + 2), 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ret;
}

char __fastcall HookedSerialize(void* self, uint32_t mode, uint8_t* pkt, uint8_t* dst,
                                uint16_t* outLen) {
    char ret = OrigSerialize(self, mode, pkt, dst, outLen);
    __try {
        if (ret && pkt) {
            uint16_t bodyLen = *(uint16_t*)(pkt + kPktOffBodyLen);
            size_t total = (size_t)bodyLen + SMH_HEADER_SIZE;
            const uint8_t* body = *(const uint8_t**)(pkt + kPktOffBodyPtr);
            if (body && total >= kMinFrame && total <= kMaxFrame) {
                static thread_local uint8_t frame[kMaxFrame];
                memcpy(frame, pkt, SMH_HEADER_SIZE);
                *(uint16_t*)(frame + 2) = (uint16_t)total;
                memcpy(frame + SMH_HEADER_SIZE, body, bodyLen);
                frame[kPktOffFlag] = 2;
                DetectHeartbeat(frame, total, 2);
                g_frameQueue.PushFrame(frame, (uint32_t)total);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ret;
}

// Fails until the game has loaded its module and constructed the netMgr; the
// vtable pointer and both slots must land inside the module for that to pass.
bool ResolveTargets(void** outSerialize, void** outDeobf) {
    HMODULE game = GetModuleHandleW(L"SoulWorker64.dll");
    if (!game)
        return false;

    uint8_t* base = (uint8_t*)game;
    __try {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;
        uint8_t* imgEnd = base + nt->OptionalHeader.SizeOfImage;

        void** netMgr = (void**)(base + kNetMgrRva);
        uint8_t** vtbl = (uint8_t**)*netMgr;
        if ((uint8_t*)vtbl < base || (uint8_t*)vtbl >= imgEnd)
            return false;

        uint8_t* fnSer = vtbl[kVtSlotSerialize];
        uint8_t* fnDeo = vtbl[kVtSlotDeobf];
        if (fnSer < base || fnSer >= imgEnd || fnDeo < base || fnDeo >= imgEnd)
            return false;

        *outSerialize = fnSer;
        *outDeobf = fnDeo;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void BuildPingFrame(uint8_t* out, size_t* outLen) {
    LONG ping = InterlockedCompareExchange(&g_pingMs, 0, 0);
    uint32_t p = (uint32_t)ping;
    out[0] = 0x00; out[1] = 0x00;
    out[2] = 0x0D; out[3] = 0x00;
    out[4] = 0x03;
    out[5] = 0x00;
    out[6] = 0x01; out[7] = 0x01;
    out[8] = 0x00;
    out[9] = (uint8_t)(p & 0xFF);
    out[10] = (uint8_t)((p >> 8) & 0xFF);
    out[11] = (uint8_t)((p >> 16) & 0xFF);
    out[12] = (uint8_t)((p >> 24) & 0xFF);
    *outLen = 13;
}

bool HooksAreLive() { return InterlockedCompareExchange(&g_live, 0, 0) != 0; }

bool HookInstall() {
    if (HooksAreLive())
        return true;

    void* fnSer = nullptr;
    void* fnDeo = nullptr;
    if (!ResolveTargets(&fnSer, &fnDeo))
        return false;

    if (!g_mhReady) {
        MH_STATUS s = MH_Initialize();
        if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED)
            return false;
        g_mhReady = true;
    }

    if (MH_CreateHook(fnDeo, (void*)&HookedDeobf, (void**)&OrigDeobf) != MH_OK)
        return false;
    if (MH_CreateHook(fnSer, (void*)&HookedSerialize, (void**)&OrigSerialize) != MH_OK) {
        MH_RemoveHook(fnDeo);
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        MH_RemoveHook(fnDeo);
        MH_RemoveHook(fnSer);
        return false;
    }

    InterlockedExchange(&g_live, 1);
    return true;
}

void HookUninstall() {
    if (!g_mhReady)
        return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_mhReady = false;
    InterlockedExchange(&g_live, 0);
}
