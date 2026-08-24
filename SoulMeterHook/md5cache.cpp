#include "md5cache.h"

#include <windows.h>
#include <MinHook.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "peutil.h"

namespace {

constexpr size_t kMaxEntries = 128;
constexpr size_t kHexLen = 32;
constexpr size_t kRelMax = 128;

void Log(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
}

// int __fastcall ComputeFileMD5(void* self, std::string* relPath, std::string* outHex)
// Returns 0 on success, 13 when the file could not be opened, and releases
// relPath before returning.
typedef int64_t(__fastcall* Md5Fn)(void*, void*, void*);
typedef void*(__fastcall* AssignFn)(void*, const void*, size_t);
typedef void(__fastcall* ReleaseFn)(void*, uint8_t, uint64_t);

Md5Fn OrigMd5 = nullptr;
AssignFn g_assign = nullptr;
ReleaseFn g_release = nullptr;

volatile LONG g_installed = 0;
CRITICAL_SECTION g_cs;
bool g_csReady = false;

struct Entry {
    char rel[kRelMax];
    uint64_t size;
    uint64_t mtime;
    char hex[kHexLen + 1];
};

Entry g_entries[kMaxEntries];
size_t g_entryCount = 0;
bool g_dirty = false;
char g_cachePath[MAX_PATH] = { 0 };

LONG g_hits = 0;
LONG g_misses = 0;

// MSVC std::string: buffer/pointer at 0, size at 0x10, capacity at 0x18. The
// game reads it the same way (capacity < 16 means the small-buffer form).
const char* StdStr(void* s) {
    if (!s)
        return nullptr;
    uint64_t cap = *(uint64_t*)((uint8_t*)s + 0x18);
    return cap < 16 ? (const char*)s : *(const char* const*)s;
}

bool StatRelative(const char* rel, uint64_t* size, uint64_t* mtime) {
    char cwd[MAX_PATH];
    DWORD n = GetCurrentDirectoryA(MAX_PATH, cwd);
    if (n == 0 || n >= MAX_PATH)
        return false;

    char full[MAX_PATH * 2];
    if (_snprintf_s(full, sizeof(full), _TRUNCATE, "%s\\%s", cwd, rel) < 0)
        return false;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(full, GetFileExInfoStandard, &fad))
        return false;

    *size = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    *mtime = ((uint64_t)fad.ftLastWriteTime.dwHighDateTime << 32) |
             fad.ftLastWriteTime.dwLowDateTime;
    return true;
}

// Caller holds the lock.
const Entry* Lookup(const char* rel, uint64_t size, uint64_t mtime) {
    for (size_t i = 0; i < g_entryCount; i++) {
        if (g_entries[i].size == size && g_entries[i].mtime == mtime &&
            _stricmp(g_entries[i].rel, rel) == 0)
            return &g_entries[i];
    }
    return nullptr;
}

// Caller holds the lock.
void Store(const char* rel, uint64_t size, uint64_t mtime, const char* hex) {
    for (size_t i = 0; i < g_entryCount; i++) {
        if (_stricmp(g_entries[i].rel, rel) == 0) {
            g_entries[i].size = size;
            g_entries[i].mtime = mtime;
            strncpy_s(g_entries[i].hex, hex, _TRUNCATE);
            g_dirty = true;
            return;
        }
    }
    if (g_entryCount >= kMaxEntries)
        return;
    Entry& e = g_entries[g_entryCount++];
    strncpy_s(e.rel, rel, _TRUNCATE);
    e.size = size;
    e.mtime = mtime;
    strncpy_s(e.hex, hex, _TRUNCATE);
    g_dirty = true;
}

void LoadCache() {
    FILE* fp = nullptr;
    if (fopen_s(&fp, g_cachePath, "r") != 0 || !fp)
        return;
    char line[512];
    while (g_entryCount < kMaxEntries && fgets(line, sizeof(line), fp)) {
        Entry e;
        memset(&e, 0, sizeof(e));
        if (sscanf_s(line, "%32s %llu %llu %127[^\n]", e.hex, (unsigned)sizeof(e.hex),
                     (unsigned long long*)&e.size, (unsigned long long*)&e.mtime, e.rel,
                     (unsigned)sizeof(e.rel)) != 4)
            continue;
        if (strlen(e.hex) != kHexLen || e.rel[0] == 0)
            continue;
        g_entries[g_entryCount++] = e;
    }
    fclose(fp);
}

void SaveCache() {
    if (!g_dirty)
        return;
    FILE* fp = nullptr;
    if (fopen_s(&fp, g_cachePath, "w") != 0 || !fp)
        return;
    for (size_t i = 0; i < g_entryCount; i++)
        fprintf(fp, "%s %llu %llu %s\n", g_entries[i].hex,
                (unsigned long long)g_entries[i].size, (unsigned long long)g_entries[i].mtime,
                g_entries[i].rel);
    fclose(fp);
    g_dirty = false;
}

int64_t __fastcall HookedMd5(void* self, void* relPath, void* outHex) {
    char rel[kRelMax] = { 0 };
    uint64_t size = 0, mtime = 0;
    bool usable = false;

    __try {
        const char* p = StdStr(relPath);
        if (p) {
            strncpy_s(rel, p, _TRUNCATE);
            usable = StatRelative(rel, &size, &mtime);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        usable = false;
    }

    if (usable) {
        char hit[kHexLen + 1] = { 0 };
        EnterCriticalSection(&g_cs);
        const Entry* e = Lookup(rel, size, mtime);
        if (e)
            memcpy(hit, e->hex, sizeof(hit));
        LeaveCriticalSection(&g_cs);

        if (hit[0]) {
            // Stand in for the original exactly: publish the hash and release
            // the path string, which the real function also owns.
            __try {
                g_assign(outHex, hit, kHexLen);
                g_release(relPath, 1, 0);
                InterlockedIncrement(&g_hits);
                return 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 13;
            }
        }
    }

    // The original releases relPath itself, so the path was copied above.
    int64_t r = OrigMd5(self, relPath, outHex);
    InterlockedIncrement(&g_misses);

    if (r == 0 && usable) {
        __try {
            const char* h = StdStr(outHex);
            if (h && strlen(h) == kHexLen) {
                EnterCriticalSection(&g_cs);
                Store(rel, size, mtime, h);
                SaveCache();
                LeaveCriticalSection(&g_cs);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return r;
}

// Prologue of the hash routine. Verified unique in GB 511199, GB 0820, KR and
// KR 2.4.27.0.
constexpr uint8_t kMd5Sig[] = {
    0x4C, 0x89, 0x44, 0x24, 0x18,                     // mov [rsp+18h], r8
    0x48, 0x89, 0x54, 0x24, 0x10,                     // mov [rsp+10h], rdx
    0x48, 0x89, 0x4C, 0x24, 0x08,                     // mov [rsp+8], rcx
    0x57,                                             // push rdi
    0x48, 0x81, 0xEC, 0xF0, 0x06, 0x00, 0x00,         // sub rsp, 6F0h
    0x48, 0xC7, 0x84, 0x24, 0xD8, 0x06, 0x00, 0x00,   // mov [rsp+6D8h], -2
    0xFE, 0xFF, 0xFF, 0xFF,
};
constexpr char kMd5Mask[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

constexpr size_t kBodyMax = 0x600;

// add rsp, 6F0h / pop rdi / retn -- the routine's single epilogue, used to stop
// the scan at the real end rather than running into the next function.
constexpr uint8_t kEpilogue[] = { 0x48, 0x81, 0xC4, 0xF0, 0x06, 0x00, 0x00, 0x5F, 0xC3 };

// The routine clears the out string on entry and publishes the hash on exit
// (assign, twice), and releases two strings on each of its two exit paths
// (release, four times). Rather than trust the position of any one call, every
// direct call in the body is bucketed by target and the helpers are identified
// by those counts -- a stray 0xE8 inside an operand lands on a target with a
// count of one and is ignored. Ambiguity fails closed and skips memoisation.
bool ResolveHelpers(uint8_t* fn) {
    size_t bodyLen = 0;
    for (size_t i = 0; i + sizeof(kEpilogue) <= kBodyMax; i++) {
        if (memcmp(fn + i, kEpilogue, sizeof(kEpilogue)) == 0) {
            bodyLen = i;
            break;
        }
    }
    if (!bodyLen) {
        Log("[swopt] md5 cache: epilogue not found, skipped\n");
        return false;
    }

    struct Bucket {
        const uint8_t* target;
        int count;
        size_t firstAt;
    };
    Bucket buckets[32];
    size_t n = 0;

    for (size_t i = 0; i + 5 <= bodyLen; i++) {
        if (fn[i] != 0xE8)
            continue;
        const uint8_t* target = fn + i + 5 + *(int32_t*)(fn + i + 1);
        size_t k = 0;
        for (; k < n; k++) {
            if (buckets[k].target == target) {
                buckets[k].count++;
                break;
            }
        }
        if (k == n && n < 32)
            buckets[n++] = { target, 1, i };
    }

    // Clearing the out string is the routine's very first direct call, which
    // separates assign from the other target that also happens to be called
    // twice (the path builder). Release is the only target called four times.
    const uint8_t* assign = nullptr;
    size_t earliest = (size_t)-1;
    for (size_t i = 0; i < n; i++) {
        if (buckets[i].firstAt < earliest) {
            earliest = buckets[i].firstAt;
            assign = buckets[i].target;
        }
    }
    int assignCount = 0;
    for (size_t i = 0; i < n; i++) {
        if (buckets[i].target == assign)
            assignCount = buckets[i].count;
    }

    const uint8_t* release = nullptr;
    int releaseCands = 0;
    for (size_t i = 0; i < n; i++) {
        if (buckets[i].count == 4) {
            release = buckets[i].target;
            releaseCands++;
        }
    }

    if (!assign || assignCount != 2 || releaseCands != 1 || assign == release) {
        Log("[swopt] md5 cache: assign count %d, %d release candidates, skipped\n", assignCount,
            releaseCands);
        return false;
    }

    g_assign = (AssignFn)assign;
    g_release = (ReleaseFn)release;
    return true;
}

void BuildCachePath() {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        strcpy_s(g_cachePath, "swmd5cache.txt");
        return;
    }
    for (DWORD i = n; i > 0; i--) {
        if (exe[i - 1] == '\\') {
            exe[i] = 0;
            break;
        }
    }
    sprintf_s(g_cachePath, "%sswmd5cache.txt", exe);
}

} // namespace

bool Md5CacheInstall() {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return true;

    HMODULE game = GetModuleHandleW(L"SoulWorker64.dll");
    if (!game) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }

    uint8_t* fn = nullptr;
    __try {
        pe::Section text = { nullptr, 0 };
        if (!pe::FindSection((uint8_t*)game, ".text", &text)) {
            InterlockedExchange(&g_installed, 0);
            return false;
        }
        fn = (uint8_t*)pe::FindUnique(text, kMd5Sig, kMd5Mask);
        if (!fn) {
            Log("[swopt] md5 cache: signature missing or ambiguous, skipped\n");
            InterlockedExchange(&g_installed, 0);
            return false;
        }
        if (!ResolveHelpers(fn)) {
            InterlockedExchange(&g_installed, 0);
            return false;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }

    InitializeCriticalSection(&g_cs);
    g_csReady = true;
    BuildCachePath();
    LoadCache();

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }
    if (MH_CreateHook(fn, (void*)&HookedMd5, (void**)&OrigMd5) != MH_OK ||
        MH_EnableHook(fn) != MH_OK) {
        Log("[swopt] md5 cache: hook failed\n");
        InterlockedExchange(&g_installed, 0);
        return false;
    }

    Log("[swopt] md5 cache armed, %zu entries loaded\n", g_entryCount);
    return true;
}

void Md5CacheShutdown() {
    if (!g_csReady)
        return;
    EnterCriticalSection(&g_cs);
    SaveCache();
    LeaveCriticalSection(&g_cs);
    Log("[swopt] md5 cache: %ld hits, %ld misses\n", g_hits, g_misses);
}
