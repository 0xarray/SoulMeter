#include "loadopt.h"

#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "peutil.h"

namespace {

volatile LONG g_zoneDelayDone = 0;

void Log(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

// CGameWorld's zone-warp handler arms a LogicTimer with a hardcoded 0.5s and
// only sends the warp when it expires, so every zone transition pays half a
// second of loading screen after the map is already resident.
//
//   mov  rax, [rsp+0E0h]
//   add  rax, 98h
//   mov  r8b, 1
//   movss xmm1, cs:flt_0_5      <-- replaced with xorps xmm1, xmm1
//   mov  rcx, rax
//   call LogicTimer::SetTimer
//
// The 0.5f itself is a shared literal with 60+ referrers across the image
// (animation blends, physics, camera), so the constant must not be touched --
// only this one instruction.
//
// LogicTimer::Update expires on `accumulated >= duration`, and dt >= 0 is never
// < 0, so a zero duration fires on the next tick rather than never.
constexpr uint8_t kZoneDelaySig[] = {
    0x48, 0x8B, 0x84, 0x24, 0xE0, 0x00, 0x00, 0x00,   // mov  rax, [rsp+0E0h]
    0x48, 0x05, 0x98, 0x00, 0x00, 0x00,               // add  rax, 98h
    0x41, 0xB0, 0x01,                                 // mov  r8b, 1
    0xF3, 0x0F, 0x10, 0x0D, 0x00, 0x00, 0x00, 0x00,   // movss xmm1, [rip+d]
    0x48, 0x8B, 0xC8,                                 // mov  rcx, rax
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,               // call cs:SetTimer
};
constexpr char kZoneDelayMask[] = "xxxxxxxxxxxxxxxxxxxxx????xxxxx????";

constexpr size_t kMovssOffset = 17;
constexpr size_t kMovssLen = 8;

// xorps xmm1, xmm1 + a 5-byte nop, so the replacement is length-preserving.
constexpr uint8_t kZeroXmm1[kMovssLen] = {
    0x0F, 0x57, 0xC9,
    0x0F, 0x1F, 0x44, 0x00, 0x00,
};

// The same LogicTimer is stamped with ID 0x3EA a few instructions earlier.
// Requiring it means a signature that drifted onto some other SetTimer call
// site is rejected instead of silently zeroing an unrelated timer.
constexpr uint8_t kTimerIdMark[] = { 0xBA, 0xEA, 0x03, 0x00, 0x00 };
constexpr size_t kIdSearchBack = 256;

bool PatchZoneLoadingDelay(const pe::Section& text) {
    const uint8_t* hit = pe::FindUnique(text, kZoneDelaySig, kZoneDelayMask);
    if (!hit) {
        Log("[swopt] zone delay: signature missing or ambiguous, skipped\n");
        return false;
    }

    size_t back = (size_t)(hit - text.data) < kIdSearchBack ? (size_t)(hit - text.data)
                                                            : kIdSearchBack;
    if (!pe::Contains(hit - back, back, kTimerIdMark, sizeof(kTimerIdMark))) {
        Log("[swopt] zone delay: timer id 0x3EA not found near site, skipped\n");
        return false;
    }

    uint8_t* target = (uint8_t*)hit + kMovssOffset;
    DWORD old = 0;
    if (!VirtualProtect(target, kMovssLen, PAGE_EXECUTE_READWRITE, &old)) {
        Log("[swopt] zone delay: VirtualProtect failed (%lu)\n", GetLastError());
        return false;
    }
    memcpy(target, kZeroXmm1, kMovssLen);
    VirtualProtect(target, kMovssLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, kMovssLen);

    return true;
}

} // namespace

int LoadOptApply() {
    HMODULE game = GetModuleHandleW(L"SoulWorker64.dll");
    if (!game)
        return 0;

    uint8_t* base = (uint8_t*)game;
    int applied = 0;

    __try {
        pe::Section text = { nullptr, 0 };
        if (!pe::FindSection(base, ".text", &text))
            return 0;

        if (InterlockedCompareExchange(&g_zoneDelayDone, 1, 0) == 0) {
            if (PatchZoneLoadingDelay(text))
                applied++;
            else
                InterlockedExchange(&g_zoneDelayDone, 0);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return applied;
    }

    return applied;
}
