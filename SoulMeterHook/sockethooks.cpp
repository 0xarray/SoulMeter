// Wire frame: magic(2) total_length(2, LE) flag(1) seq(1) body(N)
// body = [cat][cmd][padLen][pad][payload]
// The meter's const byte is 1=recv / 2=send, the reverse of the wire flag, so
// byte[4] is normalized by hook direction.

#include "sockethooks.h"

#include <MinHook.h>

#include <cstring>

#include "gamecmd.h"
#include "peutil.h"

ByteQueue g_frameQueue(8 * 1024 * 1024);
volatile LONG g_pingMs = 0;
volatile LONG64 g_lastPingAt = 0;

namespace {

// Slot-1 mode dispatcher. Verified unique in both the GB (01317b93) and KR
// (b3e171a2) clients, which share this netcode.
constexpr uint8_t kSerializeSig[] = {
    0x49, 0x8B, 0xC0,               // mov  rax, r8
    0x83, 0xFA, 0x01,               // cmp  edx, 1
    0x75, 0x10,                     // jnz  plaintext
    0x4C, 0x8B, 0x44, 0x24, 0x28,   // mov  r8, [rsp+28]
    0x49, 0x8B, 0xD1,               // mov  rdx, r9
    0x48, 0x8B, 0xC8,               // mov  rcx, rax
    0xE9, 0x00, 0x00, 0x00, 0x00,   // jmp  serialise_obfuscated
    0x85, 0xD2,                     // test edx, edx
    0x75, 0x10,                     // jnz  fail
    0x4C, 0x8B, 0x44, 0x24, 0x28,   // mov  r8, [rsp+28]
};
constexpr char kSerializeMask[] = "xxxxxxxxxxxxxxxxxxxx????xxxxxxxxx";

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
    // `self` is the netMgr the maze senders take as `this`.
    GameCmdSetNetMgr(self);
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

// Fails until SoulWorker64.dll is loaded. Both targets come out of the image
// itself, so this does not wait on the netMgr being constructed.
bool ResolveTargets(void** outSerialize, void** outDeobf) {
    HMODULE game = GetModuleHandleW(L"SoulWorker64.dll");
    if (!game)
        return false;

    uint8_t* base = (uint8_t*)game;
    __try {
        IMAGE_NT_HEADERS64* nt = pe::NtHeaders(base);
        if (!nt)
            return false;
        uint8_t* imgEnd = base + nt->OptionalHeader.SizeOfImage;

        pe::Section text = { nullptr, 0 };
        pe::Section rdata = { nullptr, 0 };
        if (!pe::FindSection(base, ".text", &text) || !pe::FindSection(base, ".rdata", &rdata))
            return false;

        const uint8_t* fnSer = pe::FindUnique(text, kSerializeSig, kSerializeMask);
        if (!fnSer)
            return false;

        // The dispatcher is only reached through the vtable, so the lone
        // .rdata pointer to it fixes the table.
        const uint8_t* slot = nullptr;
        for (size_t off = 0; off + sizeof(void*) <= rdata.size; off += sizeof(void*)) {
            if (*(const uint8_t* const*)(rdata.data + off) != fnSer)
                continue;
            if (slot)
                return false;
            slot = rdata.data + off;
        }
        if (!slot)
            return false;

        uint8_t** vtbl = (uint8_t**)(slot - kVtSlotSerialize * sizeof(void*));
        if ((uint8_t*)vtbl < rdata.data ||
            (uint8_t*)(vtbl + kVtSlotDeobf + 1) > rdata.data + rdata.size)
            return false;

        uint8_t* fnDeo = vtbl[kVtSlotDeobf];
        if (fnDeo < base || fnDeo >= imgEnd)
            return false;

        *outSerialize = (void*)fnSer;
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
