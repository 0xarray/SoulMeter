// Maze restart / exit. Calls the game's own senders:
//   restart  send_eSUB_CMD_RESTART_START (52,2)  body [u32 myObjId]
//   exit     eSUB_CMD_EXIT_MAZE_REQ      (17,35) body [3 x u32][u8]
// Neither is pinned: each sender logs its own name, so the literal anchors it -
// find it in .rdata, find the one rip-relative lea in .text that loads it, take
// the enclosing function from .pdata, then confirm the (cat,cmd) immediates.
// Resolves the same two functions in the GB and KR clients.
//
// The senders run on the message-pump thread only. The exit path clears state on
// CMyPlayer and pokes the scene manager with no lock, so the window is
// subclassed and commands arrive as a posted message.

#include "gamecmd.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

#include "peutil.h"

namespace {

const wchar_t* kModuleName = L"SoulWorker64.dll";

constexpr uint32_t kDebounceMs = 250;
constexpr UINT WM_SMH_CMD = WM_APP + 0x53;

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA("[SoulMeterHook] ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

typedef void(__fastcall* RestartFn)(void* netMgr, uint64_t objId, uint64_t);
typedef void(__fastcall* ExitMazeFn)(void* netMgr, uint64_t, uint64_t, int, char);

RestartFn g_fnRestart = nullptr;
ExitMazeFn g_fnExitMaze = nullptr;
bool g_resolveFailed = false;

void* volatile g_netMgr = nullptr;

HWND g_hwnd = nullptr;
WNDPROC g_oWndProc = nullptr;
bool g_wndUnicode = true;

uint32_t g_lastFireMs[3] = { 0, 0, 0 };

struct RtFn {
    uint32_t begin;
    uint32_t end;
    uint32_t unwind;
};

constexpr uint8_t kUnwFlagChainInfo = 0x4;

// Function bounds containing `rva`, chained fragments followed back to the entry.
const RtFn* LookupPdata(uint8_t* base, uint32_t rva) {
    IMAGE_NT_HEADERS64* nt = pe::NtHeaders(base);
    if (!nt)
        return nullptr;

    const IMAGE_DATA_DIRECTORY& dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress || dir.Size < sizeof(RtFn))
        return nullptr;

    const RtFn* table = (const RtFn*)(base + dir.VirtualAddress);
    size_t count = dir.Size / sizeof(RtFn);

    const RtFn* hit = nullptr;
    size_t lo = 0;
    size_t hi = count - 1;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (rva < table[mid].begin) {
            if (mid == 0)
                break;
            hi = mid - 1;
        } else if (rva >= table[mid].end) {
            lo = mid + 1;
        } else {
            hit = &table[mid];
            break;
        }
    }

    for (int i = 0; hit && i < 8; i++) {
        const uint8_t* unwind = base + hit->unwind;
        if (((unwind[0] >> 3) & kUnwFlagChainInfo) == 0)
            return hit;
        uint8_t codes = unwind[2];
        hit = (const RtFn*)(unwind + 4 + (size_t)((codes + 1) & ~1) * 2);
    }
    return nullptr;
}

// The linker does not always fold duplicate literals, so collect them all and
// let the code-side scan resolve the ambiguity.
int FindStringAddrs(uint8_t* base, const char* literal, const uint8_t** out, int maxOut) {
    static const char* kDataSections[] = { ".rdata", ".data" };
    size_t len = strlen(literal) + 1;
    int n = 0;

    for (int s = 0; s < 2 && n < maxOut; s++) {
        pe::Section sec = { nullptr, 0 };
        if (!pe::FindSection(base, kDataSections[s], &sec) || sec.size < len)
            continue;

        const uint8_t* p = sec.data;
        const uint8_t* limit = sec.data + sec.size - len;
        while (p <= limit && n < maxOut) {
            p = (const uint8_t*)memchr(p, (uint8_t)literal[0], (size_t)(limit - p) + 1);
            if (!p)
                break;
            if (memcmp(p, literal, len) == 0)
                out[n++] = p;
            p++;
        }
    }
    return n;
}

// Null unless exactly one `lea r64, [rip+disp32]` loads one of `addrs`.
const uint8_t* FindUniqueLeaRef(const pe::Section& text, const uint8_t** addrs, int addrCount) {
    const uint8_t* found = nullptr;

    const uint8_t* p = text.data + 1;
    const uint8_t* limit = text.data + text.size - 6;
    while (p <= limit) {
        p = (const uint8_t*)memchr(p, 0x8D, (size_t)(limit - p) + 1);
        if (!p)
            break;

        if (p[-1] >= 0x48 && p[-1] <= 0x4F && (p[1] & 0xC7) == 0x05) {
            int32_t disp = *(const int32_t*)(p + 2);
            const uint8_t* target = p + 6 + disp;
            for (int i = 0; i < addrCount; i++) {
                if (target != addrs[i])
                    continue;
                if (found)
                    return nullptr;
                found = p - 1;
                break;
            }
        }
        p++;
    }
    return found;
}

void* ResolveSender(uint8_t* base, const pe::Section& text, const char* literal,
                    uint8_t cat, uint8_t cmd) {
    const uint8_t* strAddrs[8];
    int strCount = FindStringAddrs(base, literal, strAddrs, 8);
    if (!strCount) {
        Log("resolve '%s': literal not found", literal);
        return nullptr;
    }

    const uint8_t* ref = FindUniqueLeaRef(text, strAddrs, strCount);
    if (!ref) {
        Log("resolve '%s': %d copies, no unique reference", literal, strCount);
        return nullptr;
    }

    const RtFn* fn = LookupPdata(base, (uint32_t)(ref - base));
    if (!fn) {
        Log("resolve '%s': reference at +%08X has no unwind entry", literal,
            (uint32_t)(ref - base));
        return nullptr;
    }

    uint8_t* start = base + fn->begin;
    size_t size = fn->end - fn->begin;
    if (start < text.data || start + size > text.data + text.size || size < 16 || size > 4096) {
        Log("resolve '%s': implausible bounds +%08X size %zu", literal, fn->begin, size);
        return nullptr;
    }

    // net_BuildPacket takes the command in r8b and the category in dl as
    // immediates; finding them is what says this is the sender for (cat,cmd).
    const uint8_t header[] = { 0x41, 0xB0, cmd, 0xB2, cat };
    if (!pe::Contains(start, size, header, sizeof(header))) {
        Log("resolve '%s': +%08X does not build (%u,%u)", literal, fn->begin, cat, cmd);
        return nullptr;
    }

    Log("resolve '%s': +%08X size %zu (cat=%u cmd=%u)", literal, fn->begin, size, cat, cmd);
    return start;
}

bool ResolveSenders() {
    HMODULE game = GetModuleHandleW(kModuleName);
    if (!game)
        return false;

    uint8_t* base = (uint8_t*)game;
    __try {
        pe::Section text = { nullptr, 0 };
        if (!pe::FindSection(base, ".text", &text))
            return false;

        void* restart = ResolveSender(base, text, "send_eSUB_CMD_RESTART_START : %d", 52, 2);
        void* exitMaze = ResolveSender(base, text, "eSUB_CMD_EXIT_MAZE_REQ", 17, 35);
        if (!restart || !exitMaze)
            return false;

        g_fnRestart = (RestartFn)restart;
        g_fnExitMaze = (ExitMazeFn)exitMaze;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("resolve: faulted while scanning the image");
        return false;
    }
}

struct WindowSearch {
    DWORD pid;
    HWND best;
    long bestArea;
};

BOOL CALLBACK PickWindow(HWND hwnd, LPARAM param) {
    WindowSearch* s = (WindowSearch*)param;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != s->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
        return TRUE;

    RECT rc = { 0, 0, 0, 0 };
    if (!GetClientRect(hwnd, &rc))
        return TRUE;
    long area = (rc.right - rc.left) * (rc.bottom - rc.top);
    if (rc.right - rc.left < 200 || rc.bottom - rc.top < 200 || area <= s->bestArea)
        return TRUE;

    s->best = hwnd;
    s->bestArea = area;
    return TRUE;
}

void RunCommand(uint8_t op, uint32_t arg) {
    void* netMgr = g_netMgr;
    if (!netMgr) {
        Log("command %u ignored: no netMgr seen yet", op);
        return;
    }

    __try {
        if (op == SMH_CMD_RESTART_MAZE) {
            if (!arg) {
                Log("restart ignored: no player object id");
                return;
            }
            g_fnRestart(netMgr, arg, 0);
            Log("restart sent (52,2) objId=%u", arg);
        } else if (op == SMH_CMD_EXIT_MAZE) {
            g_fnExitMaze(netMgr, 0, 0, 0, 1);
            Log("exit maze sent (17,35)");
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("command %u faulted", op);
    }
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_SMH_CMD) {
        RunCommand((uint8_t)w, (uint32_t)l);
        return 0;
    }
    if (g_wndUnicode)
        return CallWindowProcW(g_oWndProc, hwnd, msg, w, l);
    return CallWindowProcA(g_oWndProc, hwnd, msg, w, l);
}

bool SubclassGameWindow() {
    WindowSearch s = { GetCurrentProcessId(), nullptr, 0 };
    EnumWindows(PickWindow, (LPARAM)&s);
    if (!s.best)
        return false;

    g_wndUnicode = IsWindowUnicode(s.best) != FALSE;
    LONG_PTR prev = g_wndUnicode
                        ? SetWindowLongPtrW(s.best, GWLP_WNDPROC, (LONG_PTR)HookWndProc)
                        : SetWindowLongPtrA(s.best, GWLP_WNDPROC, (LONG_PTR)HookWndProc);
    if (!prev)
        return false;

    g_oWndProc = (WNDPROC)prev;
    g_hwnd = s.best;

    char title[128] = { 0 };
    GetWindowTextA(s.best, title, sizeof(title) - 1);
    Log("maze commands armed on window %p (%s)", (void*)s.best, title);
    return true;
}

} // namespace

bool GameCmdInit() {
    // A recreated window leaves a stale subclass target behind.
    if (g_hwnd && !IsWindow(g_hwnd)) {
        g_hwnd = nullptr;
        g_oWndProc = nullptr;
    }
    if (g_hwnd)
        return true;

    if (!g_fnRestart) {
        if (g_resolveFailed)
            return false;
        if (!ResolveSenders()) {
            // Scanning the image is only worth repeating while the module has
            // yet to load; a failure after that will not fix itself.
            if (GetModuleHandleW(kModuleName)) {
                g_resolveFailed = true;
                Log("maze commands disabled: senders did not resolve in this build");
            }
            return false;
        }
    }
    return SubclassGameWindow();
}

void GameCmdShutdown() {
    if (!g_hwnd || !g_oWndProc)
        return;

    // Only unwind our own subclass; anything installed on top of it owns the
    // chain now.
    LONG_PTR cur = g_wndUnicode ? GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC)
                                : GetWindowLongPtrA(g_hwnd, GWLP_WNDPROC);
    if (cur == (LONG_PTR)HookWndProc) {
        if (g_wndUnicode)
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_oWndProc);
        else
            SetWindowLongPtrA(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_oWndProc);
    }
    g_hwnd = nullptr;
    g_oWndProc = nullptr;
}

void GameCmdSetNetMgr(void* netMgr) {
    if (netMgr && !g_netMgr)
        g_netMgr = netMgr;
}

void GameCmdPost(uint8_t op, uint32_t arg) {
    if (op != SMH_CMD_RESTART_MAZE && op != SMH_CMD_EXIT_MAZE)
        return;
    if (!g_hwnd) {
        Log("command %u ignored: not armed", op);
        return;
    }

    uint32_t now = (uint32_t)GetTickCount64();
    if (now - g_lastFireMs[op] < kDebounceMs)
        return;
    g_lastFireMs[op] = now;

    PostMessageW(g_hwnd, WM_SMH_CMD, op, arg);
}
