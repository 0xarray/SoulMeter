// Frame cap and camera zoom-out limit, driven from the meter over the command
// pipe. Both are data the game already owns: a frame-time float it writes
// itself, and a field on the camera modifier. Neither patches code.

#include "gamecmd.h"
#include "gametweak.h"

#include <windows.h>
#include <MinHook.h>

#include <cstdarg>
#include <cstdio>

#include "peutil.h"

namespace {

const wchar_t* kGameModule = L"SoulWorker64.dll";

void Log(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA("[SoulMeterHook] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

// ---------------------------------------------------------------- frame cap

// The main loop paces itself off one "minimum frame time" float, and hands the
// game logic the measured delta either way, so writing 1/N into it is what the
// game's own frame-rate setting and General.ini's [Test Info] FPS already do.
//
// Never write 0. The timer's Update() sits inside the same `minFrameTime > 0`
// branch as the limiter, so zeroing the float - or nopping the compare that
// guards it - stops the game clock instead of freeing it. That is where a "20x
// speed" unlock comes from.
//
// CGraphicOption::SetFrameRate's switch over the frame-rate index, matched on
// the compare chain rather than the float loads so the constants stay free to
// be cross-checked separately.
//
//   cmp [rsp+4], 0 / jz case0 ... cmp [rsp+4], 4 / jz case4 / jmp done
constexpr uint8_t kFpsChainSig[] = {
    0x83, 0x7C, 0x24, 0x04, 0x00, 0x74, 0x00,
    0x83, 0x7C, 0x24, 0x04, 0x01, 0x74, 0x00,
    0x83, 0x7C, 0x24, 0x04, 0x02, 0x74, 0x00,
    0x83, 0x7C, 0x24, 0x04, 0x03, 0x74, 0x00,
    0x83, 0x7C, 0x24, 0x04, 0x04, 0x74, 0x00,
    0xEB, 0x00,
};
constexpr char kFpsChainMask[] = "xxxxxx?xxxxxx?xxxxxx?xxxxxx?xxxxxx?x?";

// The tail of the same function:
//   movss xmm0, [rip+one] / divss xmm0, [rsp] / movss [rip+minFrameTime], xmm0
//   add rsp, 18h / retn
constexpr uint8_t kFpsTailSig[] = {
    0xF3, 0x0F, 0x10, 0x05, 0x00, 0x00, 0x00, 0x00,
    0xF3, 0x0F, 0x5E, 0x04, 0x24,
    0xF3, 0x0F, 0x11, 0x05, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x83, 0xC4, 0x18,
    0xC3,
};
constexpr char kFpsTailMask[] = "xxxx????xxxxxxxxx????xxxxx";

// The frame rates the five cases select between, in order. Requiring them means
// a chain that drifted onto some other 0..4 switch is rejected.
constexpr float kFpsChoices[5] = { 60.0f, 75.0f, 144.0f, 165.0f, 240.0f };
constexpr size_t kFpsTailScan = 0x80;

constexpr uint32_t kFpsMin = 30;
constexpr uint32_t kFpsMax = 1000;

// "No cap" is still a frame time, never 0: the timer's own Update() sits inside
// the `minFrameTime > 0` branch, so zeroing the float stops the game clock
// instead of freeing it. One ten-thousandth of a second is a target the game
// will never reach, which is the same thing without the trap.
constexpr uint32_t kFpsUncapped = 10000;

float* g_minFrameTime = nullptr;

// What the game last had in there, so turning the cap back off restores the
// setting the player actually chose rather than whatever it held at startup.
// Anything that is not our own last write came from the game.
float g_gameFrameTime = 0.0f;
float g_ourFrameTime = 0.0f;
volatile LONG g_wantFps = 0;
volatile LONG g_capApplied = 0;
volatile LONG g_running = 0;

// Null unless `p` is a `movss xmm0, [rip+d]` pointing at a float in the image.
// The constants live in .rdata, so the bound is the whole module, not .text.
const float* RipFloat(const uint8_t* p, uint8_t* base, size_t imageSize) {
    int32_t disp = *(const int32_t*)(p + 4);
    const uint8_t* target = p + 8 + disp;
    if (target < base || target + 4 > base + imageSize)
        return nullptr;
    return (const float*)target;
}

float* ResolveMinFrameTime(uint8_t* base, const pe::Section& text) {
    IMAGE_NT_HEADERS64* nt = pe::NtHeaders(base);
    if (!nt)
        return nullptr;
    size_t imageSize = nt->OptionalHeader.SizeOfImage;

    const uint8_t* chain = pe::FindUnique(text, kFpsChainSig, kFpsChainMask);
    if (!chain) {
        Log("fps: frame-rate switch missing or ambiguous");
        return nullptr;
    }

    // The five case bodies each load one constant; the shared tail then divides
    // 1.0 by whichever was chosen and stores the result.
    const uint8_t* limit = chain + kFpsTailScan;
    const uint8_t* textEnd = text.data + text.size - sizeof(kFpsTailSig);
    if (limit > textEnd)
        limit = textEnd;

    int found = 0;
    const uint8_t* p = chain;
    while (p < limit && found < 5) {
        if (p[0] == 0xF3 && p[1] == 0x0F && p[2] == 0x10 && p[3] == 0x05) {
            const float* v = RipFloat(p, base, imageSize);
            if (!v || *v != kFpsChoices[found]) {
                Log("fps: case %d is not %.0f", found, kFpsChoices[found]);
                return nullptr;
            }
            found++;
            p += 8;
            continue;
        }
        p++;
    }
    if (found != 5) {
        Log("fps: only %d of 5 frame-rate constants found", found);
        return nullptr;
    }

    // p now sits just past the last case body, on the shared tail.
    const uint8_t* tail = nullptr;
    for (const uint8_t* q = p; q <= limit && !tail; q++) {
        bool hit = true;
        for (size_t i = 0; i < sizeof(kFpsTailSig) && hit; i++) {
            if (kFpsTailMask[i] == 'x' && q[i] != kFpsTailSig[i])
                hit = false;
        }
        if (hit)
            tail = q;
    }
    if (!tail) {
        Log("fps: no store site after the frame-rate switch");
        return nullptr;
    }

    const float* one = RipFloat(tail, base, imageSize);
    if (!one || *one != 1.0f) {
        Log("fps: tail does not divide 1.0");
        return nullptr;
    }

    int32_t disp = *(const int32_t*)(tail + 17);
    uint8_t* slot = (uint8_t*)tail + 21 + disp;
    if (slot < base || slot + 4 > base + imageSize) {
        Log("fps: store target is outside the image");
        return nullptr;
    }

    float cur = *(float*)slot;
    if (!(cur > 0.0f) || cur > 0.2f) {
        Log("fps: +%08X holds %f, not a frame time", (uint32_t)(slot - base), cur);
        return nullptr;
    }

    Log("fps: frame time at +%08X (currently %.1f fps)", (uint32_t)(slot - base), 1.0f / cur);
    return (float*)slot;
}

// The game rewrites the frame time whenever the player applies its own graphics
// options, so the cap has to be held rather than set once.
DWORD WINAPI FrameCapThread(LPVOID) {
    while (g_running) {
        LONG fps = InterlockedCompareExchange(&g_wantFps, 0, 0);
        __try {
            if (fps > 0) {
                float want = 1.0f / (float)fps;
                float cur = *g_minFrameTime;
                if (cur != want) {
                    if (!g_capApplied || cur != g_ourFrameTime)
                        g_gameFrameTime = cur;
                    *g_minFrameTime = want;
                    g_ourFrameTime = want;
                    InterlockedExchange(&g_capApplied, 1);
                }
            } else if (g_capApplied) {
                if (g_gameFrameTime > 0.0f)
                    *g_minFrameTime = g_gameFrameTime;
                InterlockedExchange(&g_capApplied, 0);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        Sleep(200);
    }
    return 0;
}

// --------------------------------------------------------------------- FOV

// The look-at modifier holds a base distance, the zoom the wheel accumulates,
// and a min/max pair it clamps `base - zoom` against:
//
//   if (base - zoom < minDist) zoom = base - minDist;
//   if (base - zoom > maxDist) zoom = base - maxDist;
//   distance = base - zoom;
//
// maxDist is 400 in the field, which is what stops the wheel; the game's own
// boss camera runs 3500.
//
// Raise the field, do not remove the clamp. That same max is the denominator of
// two blends later in the frame - camera height and look-at offset, both lerped
// by (distance - min) / (max - min) - and the second is clamped at 0 but not at
// 1, so a distance past max makes them extrapolate the camera away instead of
// stopping.
//
// Matched with the four field displacements wild and the offsets read back out
// of the match; resolves two sites (ordinary and mounted camera) in GB, KR and
// JP, all at +0x24 base, +0x2C zoom, +0x30 min, +0x34 max, each enclosed by a
// (this, dt) function. The limit is rewritten on the way in so a camera-setting
// change cannot leave a stale one behind.
constexpr uint8_t kZoomClampSig[] = {
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,   // mov  rax, [rsp+d]
    0x48, 0x8B, 0x8C, 0x24, 0, 0, 0, 0,   // mov  rcx, [rsp+d]
    0xF3, 0x0F, 0x10, 0x40, 0,            // movss xmm0, [rax+base]
    0xF3, 0x0F, 0x5C, 0x41, 0,            // subss xmm0, [rcx+zoom]
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,
    0x0F, 0x2F, 0x40, 0,                  // comiss xmm0, [rax+min]
    0x73, 0,                              // jnb   past
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,
    0x48, 0x8B, 0x8C, 0x24, 0, 0, 0, 0,
    0xF3, 0x0F, 0x10, 0x40, 0,            // movss xmm0, [rax+base]
    0xF3, 0x0F, 0x5C, 0x41, 0,            // subss xmm0, [rcx+min]
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,
    0xF3, 0x0F, 0x11, 0x40, 0,            // movss [rax+zoom], xmm0
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,
    0x48, 0x8B, 0x8C, 0x24, 0, 0, 0, 0,
    0xF3, 0x0F, 0x10, 0x40, 0,            // movss xmm0, [rax+base]
    0xF3, 0x0F, 0x5C, 0x41, 0,            // subss xmm0, [rcx+zoom]
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,
    0x0F, 0x2F, 0x40, 0,                  // comiss xmm0, [rax+max]
    0x76, 0,                              // jbe   past
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,
    0x48, 0x8B, 0x8C, 0x24, 0, 0, 0, 0,
    0xF3, 0x0F, 0x10, 0x40, 0,            // movss xmm0, [rax+base]
    0xF3, 0x0F, 0x5C, 0x41, 0,            // subss xmm0, [rcx+max]
    0x48, 0x8B, 0x84, 0x24, 0, 0, 0, 0,
    0xF3, 0x0F, 0x11, 0x40, 0,            // movss [rax+zoom], xmm0
};
constexpr char kZoomClampMask[] =
    "xxxx????xxxx????"
    "xxxx?xxxx?"
    "xxxx????xxx?x?"
    "xxxx????xxxx????"
    "xxxx?xxxx?"
    "xxxx????xxxx?"
    "xxxx????xxxx????"
    "xxxx?xxxx?"
    "xxxx????xxx?x?"
    "xxxx????xxxx????"
    "xxxx?xxxx?"
    "xxxx????xxxx?";

// Displacement byte positions within a match.
constexpr size_t kOffBase[4] = { 20, 60, 99, 139 };
constexpr size_t kOffZoom[4] = { 25, 78, 104, 157 };
constexpr size_t kOffMin[2] = { 37, 65 };
constexpr size_t kOffMax[2] = { 116, 144 };

// One site for the ordinary camera, one for the mounted one.
constexpr int kMaxZoomHooks = 4;

// 7.5x the field camera's own 400, and below the 3500 the game already uses for
// boss fights. Far enough out that the camera's collision raycast and the far
// clip plane are what stop the wheel rather than this.
constexpr float kUnlockedMaxDistance = 3000.0f;

typedef void(__fastcall* CamUpdateFn)(void* self, float dt);

struct ZoomHook {
    void* fn;
    CamUpdateFn orig;
    uint8_t maxOff;
    float gameMax;
    float ourMax;
    bool raised;
};

ZoomHook g_zoomHooks[kMaxZoomHooks];
int g_zoomHookCount = 0;

volatile LONG g_wantZoomUnlock = 0;

void ApplyZoom(ZoomHook& h, void* self) {
    float* slot = (float*)((uint8_t*)self + h.maxOff);
    __try {
        if (InterlockedCompareExchange(&g_wantZoomUnlock, 0, 0)) {
            // Anything that is not our own last write is the camera setting the
            // game just applied, and is what turning this back off restores.
            if (*slot != h.ourMax)
                h.gameMax = *slot;
            if (h.gameMax < kUnlockedMaxDistance) {
                h.ourMax = kUnlockedMaxDistance;
                h.raised = true;
                *slot = h.ourMax;
            }
        } else if (h.raised) {
            if (*slot == h.ourMax && h.gameMax > 0.0f)
                *slot = h.gameMax;
            h.raised = false;
            h.ourMax = 0.0f;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookedCam0(void* self, float dt) {
    ApplyZoom(g_zoomHooks[0], self);
    g_zoomHooks[0].orig(self, dt);
}

void __fastcall HookedCam1(void* self, float dt) {
    ApplyZoom(g_zoomHooks[1], self);
    g_zoomHooks[1].orig(self, dt);
}

void __fastcall HookedCam2(void* self, float dt) {
    ApplyZoom(g_zoomHooks[2], self);
    g_zoomHooks[2].orig(self, dt);
}

void __fastcall HookedCam3(void* self, float dt) {
    ApplyZoom(g_zoomHooks[3], self);
    g_zoomHooks[3].orig(self, dt);
}

void* const kZoomDetours[kMaxZoomHooks] = {
    (void*)&HookedCam0, (void*)&HookedCam1, (void*)&HookedCam2, (void*)&HookedCam3,
};

// False when the match does not spell out one consistent field layout, which
// means the signature drifted onto something that only looks like a clamp.
bool ReadZoomLayout(const uint8_t* hit, uint8_t* outMax) {
    uint8_t base = hit[kOffBase[0]];
    uint8_t zoom = hit[kOffZoom[0]];
    uint8_t mn = hit[kOffMin[0]];
    uint8_t mx = hit[kOffMax[0]];

    for (int i = 1; i < 4; i++) {
        if (hit[kOffBase[i]] != base || hit[kOffZoom[i]] != zoom)
            return false;
    }
    if (hit[kOffMin[1]] != mn || hit[kOffMax[1]] != mx)
        return false;
    if (base == zoom || mn == mx || mn == base || mx == base || mn == zoom || mx == zoom)
        return false;

    *outMax = mx;
    return true;
}

// Every clamp site, not just the first: the mounted camera has its own copy.
int FindZoomClamps(const pe::Section& text, const uint8_t** out, int maxOut) {
    size_t len = sizeof(kZoomClampSig);
    if (text.size < len)
        return 0;

    int n = 0;
    const uint8_t* p = text.data;
    const uint8_t* limit = text.data + text.size - len;
    while (p <= limit && n < maxOut) {
        p = (const uint8_t*)memchr(p, kZoomClampSig[0], (size_t)(limit - p) + 1);
        if (!p)
            break;
        bool hit = true;
        for (size_t k = 1; k < len && hit; k++) {
            if (kZoomClampMask[k] == 'x' && p[k] != kZoomClampSig[k])
                hit = false;
        }
        if (hit)
            out[n++] = p;
        p++;
    }
    return n;
}

bool InstallZoomHooks(uint8_t* base, const pe::Section& text) {
    const uint8_t* hits[kMaxZoomHooks];
    int count = FindZoomClamps(text, hits, kMaxZoomHooks);
    if (!count) {
        Log("fov: camera zoom clamp not found in this build");
        return false;
    }

    for (int i = 0; i < count; i++) {
        uint8_t maxOff = 0;
        if (!ReadZoomLayout(hits[i], &maxOff)) {
            Log("fov: clamp at +%08X has an inconsistent layout", (uint32_t)(hits[i] - base));
            continue;
        }

        const pe::RuntimeFunction* fn = pe::FindFunction(base, (uint32_t)(hits[i] - base));
        if (!fn) {
            Log("fov: clamp at +%08X has no unwind entry", (uint32_t)(hits[i] - base));
            continue;
        }

        uint8_t* start = base + fn->begin;
        size_t size = fn->end - fn->begin;
        if (start < text.data || start + size > text.data + text.size || size < 256) {
            Log("fov: clamp at +%08X has implausible bounds", (uint32_t)(hits[i] - base));
            continue;
        }

        int slot = g_zoomHookCount;
        g_zoomHooks[slot].fn = start;
        g_zoomHooks[slot].maxOff = maxOff;
        if (MH_CreateHook(start, kZoomDetours[slot], (void**)&g_zoomHooks[slot].orig) != MH_OK ||
            MH_EnableHook(start) != MH_OK) {
            g_zoomHooks[slot].orig = nullptr;
            g_zoomHooks[slot].fn = nullptr;
            Log("fov: could not hook the camera at +%08X", fn->begin);
            continue;
        }
        g_zoomHookCount++;
        Log("fov: camera +%08X hooked (max distance at +0x%02X)", fn->begin, maxOff);
    }

    return g_zoomHookCount != 0;
}

void RemoveZoomHooks() {
    InterlockedExchange(&g_wantZoomUnlock, 0);
    for (int i = 0; i < g_zoomHookCount; i++) {
        if (!g_zoomHooks[i].fn)
            continue;
        MH_DisableHook(g_zoomHooks[i].fn);
        MH_RemoveHook(g_zoomHooks[i].fn);
        g_zoomHooks[i].fn = nullptr;
        g_zoomHooks[i].orig = nullptr;
    }
    g_zoomHookCount = 0;
}

volatile LONG g_installed = 0;

} // namespace

void GameTweakInstall() {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return;

    HMODULE game = GetModuleHandleW(kGameModule);
    if (!game)
        return;

    uint8_t* base = (uint8_t*)game;
    pe::Section text = { nullptr, 0 };
    __try {
        if (pe::FindSection(base, ".text", &text))
            g_minFrameTime = ResolveMinFrameTime(base, text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("fps: faulted while scanning the image");
        g_minFrameTime = nullptr;
        text.data = nullptr;
    }

    if (g_minFrameTime) {
        g_gameFrameTime = *g_minFrameTime;
        InterlockedExchange(&g_running, 1);
        HANDLE h = CreateThread(nullptr, 0, FrameCapThread, nullptr, 0, nullptr);
        if (h)
            CloseHandle(h);
        else
            InterlockedExchange(&g_running, 0);
    }

    if (!text.data)
        return;

    // Shared with the packet hooks; either may get here first.
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        Log("fov: MinHook unavailable (%d)", (int)s);
        return;
    }
    __try {
        InstallZoomHooks(base, text);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("fov: faulted while scanning for the camera clamp");
    }
}

void GameTweakShutdown() {
    InterlockedExchange(&g_running, 0);
    if (g_minFrameTime && g_capApplied && g_gameFrameTime > 0.0f) {
        __try {
            *g_minFrameTime = g_gameFrameTime;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    RemoveZoomHooks();
}

void GameTweakSetFpsCap(uint32_t fps) {
    if (!g_minFrameTime) {
        if (fps != SMH_FPS_GAME)
            Log("fps: cap ignored, frame time did not resolve in this build");
        return;
    }

    if (fps == SMH_FPS_UNCAPPED) {
        fps = kFpsUncapped;
    } else if (fps != SMH_FPS_GAME) {
        if (fps < kFpsMin)
            fps = kFpsMin;
        if (fps > kFpsMax)
            fps = kFpsMax;
    }
    InterlockedExchange(&g_wantFps, (LONG)fps);
}

void GameTweakSetFovUnlock(bool unlock) {
    if (unlock && !g_zoomHookCount) {
        Log("fov: unlock ignored, camera did not resolve in this build");
        return;
    }
    InterlockedExchange(&g_wantZoomUnlock, unlock ? 1 : 0);
}
