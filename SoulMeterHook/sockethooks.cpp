// Wire frame: magic(2) total_length(2, LE) flag(1) seq(1) [mode(1) nonce(4)] body(N)
// body = [cat][cmd][padLen][pad][payload]
// The meter's const byte is 1=recv / 2=send, the reverse of the wire flag, so
// byte[4] is normalized by hook direction.
//
// KR 2.4.28.5 widened the header 6 -> 11 bytes (crypto mode moved into byte 6,
// plus a 4-byte nonce) for a new AES-CTR mode 3. The body is unchanged, so
// frames are normalised back to the 6-byte form here and the meter is
// untouched. Header size is read out of the image, not assumed per client - GB
// and JP are both still on the 6-byte form.

#include "sockethooks.h"

#include <MinHook.h>

#include <cstring>

#include "gamecmd.h"
#include "peutil.h"

ByteQueue g_frameQueue(8 * 1024 * 1024);
volatile LONG g_pingMs = 0;
volatile LONG64 g_lastPingAt = 0;

namespace {

// Serialise mode dispatcher, matched only to the first branch: 2.4.28.5
// inserted a `cmp edx, 3` arm ahead of the plaintext one, which is what stopped
// the old signature matching, and JP has only the two. Still unique in every GB,
// KR and JP image checked.
constexpr uint8_t kSerializeSig[] = {
    0x49, 0x8B, 0xC0,               // mov  rax, r8
    0x83, 0xFA, 0x01,               // cmp  edx, 1
    0x75, 0x10,                     // jnz  next_mode
    0x4C, 0x8B, 0x44, 0x24, 0x28,   // mov  r8, [rsp+28]
    0x49, 0x8B, 0xD1,               // mov  rdx, r9
    0x48, 0x8B, 0xC8,               // mov  rcx, rax
    0xE9, 0x00, 0x00, 0x00, 0x00,   // jmp  serialise_obfuscated
};
constexpr char kSerializeMask[] = "xxxxxxxxxxxxxxxxxxxx????";

// Plaintext arm, present in both dispatcher forms; its target spells out the
// packet layout.
constexpr uint8_t kPlainBranch[] = {
    0x85, 0xD2,                     // test edx, edx
    0x75, 0x10,                     // jnz  fail
    0x4C, 0x8B, 0x44, 0x24, 0x28,   // mov  r8, [rsp+28]
    0x49, 0x8B, 0xD1,               // mov  rdx, r9
    0x48, 0x8B, 0xC8,               // mov  rcx, rax
    0xE9,                           // jmp  serialise_plain
};

constexpr size_t kDispatcherScan = 0x60;
constexpr size_t kSerialiseScan = 0x60;
constexpr size_t kDeobfScan = 0x30;

constexpr int kPktOffFlag = 4;
constexpr int kPktOffKey = 5;

// Read back from serialise_plain at install time.
struct PktLayout {
    uint32_t headerSize;   // wire header bytes ahead of the body
    uint32_t bodyPtrOff;   // pkt + this -> body pointer
    uint32_t bodyLenOff;   // pkt + this -> u16 body length
};

PktLayout g_layout = { 0, 0, 0 };

constexpr size_t kMaxFrame = 65535;
constexpr size_t kMinBody = 3;   // cat, cmd, padLen

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

// Rewrites the wire header into the 6-byte form the meter parses. The mode and
// nonce the new header added are dropped - the game already applied them.
size_t NormaliseHeader(uint8_t* out, const uint8_t* wire, size_t bodyLen, uint8_t dir) {
    size_t total = bodyLen + SMH_HEADER_SIZE;
    out[0] = wire[0];
    out[1] = wire[1];
    out[2] = (uint8_t)(total & 0xFF);
    out[3] = (uint8_t)((total >> 8) & 0xFF);
    out[kPktOffFlag] = dir;
    out[kPktOffKey] = wire[kPktOffKey];
    return total;
}

// `len` is the on-wire total, header included.
void EmitFrame(const uint8_t* src, size_t len, uint8_t dir) {
    if (!src || len < g_layout.headerSize + kMinBody || len > kMaxFrame)
        return;
    size_t bodyLen = len - g_layout.headerSize;
    static thread_local uint8_t frame[kMaxFrame];
    size_t total = NormaliseHeader(frame, src, bodyLen, dir);
    memcpy(frame + SMH_HEADER_SIZE, src + g_layout.headerSize, bodyLen);
    DetectHeartbeat(frame, total, dir);
    g_frameQueue.PushFrame(frame, (uint32_t)total);
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
            // pkt's first headerSize bytes are the header the serialiser just
            // wrote out verbatim.
            uint16_t bodyLen = *(uint16_t*)(pkt + g_layout.bodyLenOff);
            const uint8_t* body = *(const uint8_t**)(pkt + g_layout.bodyPtrOff);
            size_t total = (size_t)bodyLen + SMH_HEADER_SIZE;
            if (body && bodyLen >= kMinBody && total <= kMaxFrame) {
                static thread_local uint8_t frame[kMaxFrame];
                NormaliseHeader(frame, pkt, bodyLen, 2);
                memcpy(frame + SMH_HEADER_SIZE, body, bodyLen);
                DetectHeartbeat(frame, total, 2);
                g_frameQueue.PushFrame(frame, (uint32_t)total);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ret;
}

// Sole `pat`-prefixed instruction in the window, returning its imm8 operand.
bool FindImm8(const uint8_t* fn, size_t len, const uint8_t* pat, size_t patLen, uint32_t* out) {
    const uint8_t* found = nullptr;
    for (size_t i = 0; i + patLen < len; i++) {
        if (memcmp(fn + i, pat, patLen) != 0)
            continue;
        if (found)
            return false;
        found = fn + i;
    }
    if (!found)
        return false;
    *out = found[patLen];
    return true;
}

// serialise_plain reads the body length, adds the header size, then hands the
// body pointer to memcpy. Those three immediates are the layout.
bool ResolveLayout(const uint8_t* dispatcher, uint8_t* base, uint8_t* imgEnd, PktLayout* out) {
    const uint8_t* branch = nullptr;
    for (size_t i = 0; i + sizeof(kPlainBranch) <= kDispatcherScan; i++) {
        if (memcmp(dispatcher + i, kPlainBranch, sizeof(kPlainBranch)) == 0) {
            branch = dispatcher + i;
            break;
        }
    }
    if (!branch)
        return false;

    const uint8_t* jmp = branch + sizeof(kPlainBranch) - 1;
    const uint8_t* plain = jmp + 5 + *(const int32_t*)(jmp + 1);
    if (plain < base || plain + kSerialiseScan > imgEnd)
        return false;

    static const uint8_t kBodyLen[] = { 0x0F, 0xB7, 0x41 };  // movzx eax, word [rcx+imm8]
    static const uint8_t kHeader[] = { 0x66, 0x83, 0xC0 };   // add   ax, imm8
    static const uint8_t kBodyPtr[] = { 0x48, 0x8B, 0x53 };  // mov   rdx, [rbx+imm8]

    PktLayout l = { 0, 0, 0 };
    if (!FindImm8(plain, kSerialiseScan, kBodyLen, sizeof(kBodyLen), &l.bodyLenOff) ||
        !FindImm8(plain, kSerialiseScan, kHeader, sizeof(kHeader), &l.headerSize) ||
        !FindImm8(plain, kSerialiseScan, kBodyPtr, sizeof(kBodyPtr), &l.bodyPtrOff))
        return false;

    // The body pointer sits directly behind the header, the length behind that.
    // 6 (GB, JP) and 11 (KR 2.4.28.5) all hold; anything else is a layout this
    // no longer describes, so fail rather than emit garbage.
    if (l.headerSize < SMH_HEADER_SIZE || l.headerSize > 32 ||
        l.bodyPtrOff != l.headerSize || l.bodyLenOff <= l.bodyPtrOff)
        return false;

    *out = l;
    return true;
}

// The deobfuscator opens by reading the frame length out of the wire header -
// `movzx r32, word [r9+2]`, r9 being the source buffer - within its first few
// instructions in every client checked. Neither of the other functions that can
// sit in the neighbouring slot (the serialise dispatcher itself, the
// argument-spill stub at vtbl[0]) carries it, so it is enough to tell them
// apart.
bool LooksLikeDeobf(const uint8_t* fn) {
    for (size_t i = 0; i + 5 <= kDeobfScan; i++) {
        if (fn[i] == 0x41 && fn[i + 1] == 0x0F && fn[i + 2] == 0xB7 &&
            (fn[i + 3] & 0xC7) == 0x41 && fn[i + 4] == 0x02)
            return true;
    }
    return false;
}

// The deobfuscator sits next to the serialise dispatcher in the vtable, but not
// always on the same side: GB and KR declare serialise first (deobf at +1), the
// older JP build declares it second (deobf at -1). Null unless exactly one of
// the two neighbours looks the part.
uint8_t* DeobfNeighbour(const pe::Section& rdata, size_t slotOff, const pe::Section& text) {
    uint8_t* found = nullptr;
    for (int step = -1; step <= 1; step += 2) {
        if (step < 0 && slotOff < sizeof(void*))
            continue;
        size_t off = step < 0 ? slotOff - sizeof(void*) : slotOff + sizeof(void*);
        if (off + sizeof(void*) > rdata.size)
            continue;

        uint8_t* fn = *(uint8_t* const*)(rdata.data + off);
        if (fn < text.data || fn + kDeobfScan > text.data + text.size)
            continue;
        if (!LooksLikeDeobf(fn))
            continue;
        if (found && found != fn)
            return nullptr;
        found = fn;
    }
    return found;
}

// Fails until SoulWorker64.dll is loaded. Both targets come out of the image
// itself, so this does not wait on the netMgr being constructed.
bool ResolveTargets(void** outSerialize, void** outDeobf, PktLayout* outLayout) {
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

        // The dispatcher is only reached through the vtable, so every .rdata
        // pointer to it is one, and the deobfuscator is the slot beside it. JP
        // carries two copies of the table, so the slots are all walked and have
        // to agree rather than the lone pointer fixing the table.
        uint8_t* fnDeo = nullptr;
        for (size_t off = 0; off + sizeof(void*) <= rdata.size; off += sizeof(void*)) {
            if (*(const uint8_t* const*)(rdata.data + off) != fnSer)
                continue;

            uint8_t* neighbour = DeobfNeighbour(rdata, off, text);
            if (!neighbour || (fnDeo && fnDeo != neighbour))
                return false;
            fnDeo = neighbour;
        }
        if (!fnDeo)
            return false;

        if (fnSer + kDispatcherScan > imgEnd || !ResolveLayout(fnSer, base, imgEnd, outLayout))
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
    PktLayout layout = { 0, 0, 0 };
    if (!ResolveTargets(&fnSer, &fnDeo, &layout))
        return false;

    // Set before the hooks go live; both callbacks read it.
    g_layout = layout;
    {
        char msg[128];
        wsprintfA(msg, "[SoulMeterHook] layout: header %u bodyPtr +%u bodyLen +%u\n",
                  layout.headerSize, layout.bodyPtrOff, layout.bodyLenOff);
        OutputDebugStringA(msg);
    }

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
