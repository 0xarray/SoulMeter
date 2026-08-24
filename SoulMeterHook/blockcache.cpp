#include "blockcache.h"

#include <windows.h>
#include <MinHook.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kBlock = 32 * 1024;
constexpr size_t kWays = 2;
constexpr size_t kSets = 512;                  // power of two
constexpr size_t kBlockCount = kSets * kWays;  // 32 MB
constexpr size_t kMaxFiles = 256;
constexpr size_t kHandleSlots = 4096;          // power of two
constexpr size_t kPathMax = 260;
constexpr size_t kProbe = 8;

struct FileEnt {
    char path[kPathMax];
    int64_t size;      // -1 until queried
};

struct Block {
    int fileId;        // -1 when empty
    uint64_t index;
    uint32_t valid;    // bytes present; < kBlock only at end of file
    uint64_t lastUse;
    uint8_t* data;
};

struct HandleEnt {
    HANDLE h;
    int fileId;        // -1 = tracked but not served
    int64_t pos;       // logical position the cache owns
    bool live;
};

FileEnt g_files[kMaxFiles];
size_t g_fileCount = 0;

// Two-way set associative, so a hot pair of blocks that collide cannot evict
// each other on every access. No side index to keep coherent.
Block g_blocks[kBlockCount];
uint8_t* g_slab = nullptr;
uint64_t g_clock = 0;

HandleEnt g_handles[kHandleSlots];

CRITICAL_SECTION g_cs;
bool g_ready = false;
volatile LONG g_installed = 0;

typedef HANDLE(WINAPI* CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef BOOL(WINAPI* ReadFileFn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI* WriteFileFn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef DWORD(WINAPI* SetFilePointerFn)(HANDLE, LONG, PLONG, DWORD);
typedef BOOL(WINAPI* SetFilePointerExFn)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
typedef BOOL(WINAPI* CloseHandleFn)(HANDLE);

CreateFileWFn OrigCreateFileW = nullptr;
CreateFileAFn OrigCreateFileA = nullptr;
ReadFileFn OrigReadFile = nullptr;
WriteFileFn OrigWriteFile = nullptr;
SetFilePointerFn OrigSetFilePointer = nullptr;
SetFilePointerExFn OrigSetFilePointerEx = nullptr;
CloseHandleFn OrigCloseHandle = nullptr;

inline size_t HSlot(HANDLE h) { return ((size_t)(uintptr_t)h >> 2) & (kHandleSlots - 1); }

// Probes the whole group rather than stopping at the first empty slot. Closing
// a handle leaves a hole, and stopping there could hide a live entry further
// along the chain -- the read would then fall through to the unhooked path at
// the real file pointer, which lags the position the cache owns, and return
// bytes from the wrong offset.
HandleEnt* FindHandle(HANDLE h) {
    size_t slot = HSlot(h);
    for (size_t p = 0; p < kProbe; p++) {
        HandleEnt& e = g_handles[(slot + p) & (kHandleSlots - 1)];
        if (e.live && e.h == h)
            return &e;
    }
    return nullptr;
}

// Never displaces a live entry belonging to a different handle: dropping a
// handle the cache has already served would strand its logical position. When
// the group is full the handle simply goes untracked, which is safe because
// nothing has been served for it yet.
void BindHandle(HANDLE h, int fileId) {
    size_t slot = HSlot(h);
    for (size_t p = 0; p < kProbe; p++) {
        HandleEnt& e = g_handles[(slot + p) & (kHandleSlots - 1)];
        if (e.live && e.h != h)
            continue;
        e.h = h;
        e.fileId = fileId;
        e.pos = 0;
        e.live = true;
        return;
    }
}

int FindOrAddFile(const char* path) {
    for (size_t i = 0; i < g_fileCount; i++) {
        if (_stricmp(g_files[i].path, path) == 0)
            return (int)i;
    }
    if (g_fileCount >= kMaxFiles)
        return -1;
    int id = (int)g_fileCount++;
    strncpy_s(g_files[id].path, path, _TRUNCATE);
    g_files[id].size = -1;
    return id;
}

inline size_t SetOf(int fileId, uint64_t idx) {
    uint64_t k = ((uint64_t)fileId * 0x100000001B3ull) ^ (idx * 0x9E3779B97F4A7C15ull);
    return (size_t)((k >> 17) & (kSets - 1));
}

void InvalidateFile(int fileId) {
    for (size_t i = 0; i < kBlockCount; i++) {
        if (g_blocks[i].fileId == fileId)
            g_blocks[i].fileId = -1;
    }
}

// Caller holds the lock. Puts the real file pointer back where the caller
// believes it is, then stops serving this handle.
void DisableLocked(HandleEnt* e) {
    if (!e || e->fileId < 0)
        return;
    LARGE_INTEGER li;
    li.QuadPart = e->pos;
    OrigSetFilePointerEx(e->h, li, nullptr, FILE_BEGIN);
    e->fileId = -1;
}

bool IsArchive(const char* path) {
    size_t len = strlen(path);
    if (len < 3)
        return false;
    const char* ext = path + len;
    while (ext > path && *(ext - 1) != '.' && *(ext - 1) != '\\' && *(ext - 1) != '/')
        ext--;
    return _stricmp(ext, "v") == 0 || _stricmp(ext, "vArc") == 0;
}

// Caller holds the lock. Null when the underlying read failed.
Block* GetBlock(HANDLE h, int fileId, uint64_t idx) {
    size_t set = SetOf(fileId, idx);
    Block* ways[kWays];
    for (size_t w = 0; w < kWays; w++)
        ways[w] = &g_blocks[set * kWays + w];

    for (size_t w = 0; w < kWays; w++) {
        if (ways[w]->fileId == fileId && ways[w]->index == idx) {
            ways[w]->lastUse = ++g_clock;
            return ways[w];
        }
    }

    Block* victim = ways[0];
    for (size_t w = 1; w < kWays; w++) {
        if (ways[w]->fileId < 0 || ways[w]->lastUse < victim->lastUse)
            victim = ways[w];
    }

    LARGE_INTEGER li;
    li.QuadPart = (int64_t)idx * kBlock;
    if (!OrigSetFilePointerEx(h, li, nullptr, FILE_BEGIN)) {
        victim->fileId = -1;
        return nullptr;
    }
    DWORD got = 0;
    if (!OrigReadFile(h, victim->data, kBlock, &got, nullptr)) {
        victim->fileId = -1;
        return nullptr;
    }

    victim->fileId = fileId;
    victim->index = idx;
    victim->valid = got;
    victim->lastUse = ++g_clock;
    return victim;
}

// Caller holds the lock.
int64_t FileSizeOf(HANDLE h, int fileId) {
    if (g_files[fileId].size >= 0)
        return g_files[fileId].size;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(h, &li))
        return -1;
    g_files[fileId].size = li.QuadPart;
    return li.QuadPart;
}

// Caller holds the lock. False when the read could not be served, in which case
// the handle has already been dropped back to the unhooked path.
bool ServeRead(HandleEnt* e, HANDLE h, void* buf, DWORD toRead, DWORD* read) {
    int fileId = e->fileId;
    int64_t pos = e->pos;
    uint32_t done = 0;

    while (done < toRead) {
        uint64_t at = (uint64_t)pos + done;
        Block* b = GetBlock(h, fileId, at / kBlock);
        if (!b) {
            DisableLocked(e);
            return false;
        }
        uint32_t off = (uint32_t)(at % kBlock);
        if (off >= b->valid)
            break;                          // at end of file
        uint32_t avail = b->valid - off;
        uint32_t want = toRead - done;
        uint32_t n = avail < want ? avail : want;
        memcpy((uint8_t*)buf + done, b->data + off, n);
        done += n;
        if (b->valid < kBlock)
            break;                          // short block means end of file
    }

    e->pos = pos + done;
    if (read)
        *read = done;
    return true;
}

// Caller holds the lock. False when the origin cannot be modelled.
bool Reposition(HandleEnt* e, HANDLE h, int64_t dist, DWORD method, int64_t* outPos) {
    int64_t base;
    switch (method) {
    case FILE_BEGIN:
        base = 0;
        break;
    case FILE_CURRENT:
        base = e->pos;
        break;
    case FILE_END: {
        int64_t sz = FileSizeOf(h, e->fileId);
        if (sz < 0)
            return false;
        base = sz;
        break;
    }
    default:
        return false;
    }
    int64_t p = base + dist;
    if (p < 0)
        return false;
    e->pos = p;
    *outPos = p;
    return true;
}

void NoteOpen(HANDLE h, const char* path, DWORD access, DWORD flags) {
    if (!g_ready || h == INVALID_HANDLE_VALUE || !path)
        return;
    // Never serve a handle opened for writing, or an overlapped one: its reads
    // carry their own offset and do not use the shared file pointer.
    bool cacheable = IsArchive(path) && (access & GENERIC_WRITE) == 0 &&
                     (flags & FILE_FLAG_OVERLAPPED) == 0;

    EnterCriticalSection(&g_cs);
    // Bound even when not cacheable, so a recycled handle value cannot inherit
    // the previous file's entry.
    BindHandle(h, cacheable ? FindOrAddFile(path) : -1);
    LeaveCriticalSection(&g_cs);
}

HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                              DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = OrigCreateFileW(name, access, share, sa, disp, flags, tmpl);
    __try {
        if (h != INVALID_HANDLE_VALUE && name && g_ready) {
            char narrow[kPathMax];
            if (WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr,
                                    nullptr) > 0)
                NoteOpen(h, narrow, access, flags);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return h;
}

HANDLE WINAPI HookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                              DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = OrigCreateFileA(name, access, share, sa, disp, flags, tmpl);
    __try {
        if (h != INVALID_HANDLE_VALUE && name && g_ready)
            NoteOpen(h, name, access, flags);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return h;
}

BOOL WINAPI HookCloseHandle(HANDLE h) {
    if (g_ready) {
        EnterCriticalSection(&g_cs);
        HandleEnt* e = FindHandle(h);
        if (e) {
            e->live = false;
            e->h = nullptr;
            e->fileId = -1;
        }
        LeaveCriticalSection(&g_cs);
    }
    return OrigCloseHandle(h);
}

BOOL WINAPI HookWriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD wrote, LPOVERLAPPED ov) {
    if (g_ready) {
        EnterCriticalSection(&g_cs);
        HandleEnt* e = FindHandle(h);
        if (e && e->fileId >= 0) {
            InvalidateFile(e->fileId);
            DisableLocked(e);
        }
        LeaveCriticalSection(&g_cs);
    }
    return OrigWriteFile(h, buf, n, wrote, ov);
}

BOOL WINAPI HookReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD read, LPOVERLAPPED ov) {
    if (!g_ready)
        return OrigReadFile(h, buf, toRead, read, ov);

    EnterCriticalSection(&g_cs);
    HandleEnt* e = FindHandle(h);
    if (!e || e->fileId < 0) {
        LeaveCriticalSection(&g_cs);
        return OrigReadFile(h, buf, toRead, read, ov);
    }
    if (ov) {
        // Explicit-offset read: the cache does not own this position.
        DisableLocked(e);
        LeaveCriticalSection(&g_cs);
        return OrigReadFile(h, buf, toRead, read, ov);
    }

    DWORD done = 0;
    bool served = ServeRead(e, h, buf, toRead, &done);
    LeaveCriticalSection(&g_cs);

    if (!served)
        return OrigReadFile(h, buf, toRead, read, ov);
    if (read)
        *read = done;
    SetLastError(NO_ERROR);
    return TRUE;
}

DWORD WINAPI HookSetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method) {
    if (!g_ready)
        return OrigSetFilePointer(h, lo, hi, method);

    EnterCriticalSection(&g_cs);
    HandleEnt* e = FindHandle(h);
    if (!e || e->fileId < 0) {
        LeaveCriticalSection(&g_cs);
        return OrigSetFilePointer(h, lo, hi, method);
    }
    int64_t dist = hi ? (((int64_t)*hi << 32) | (uint32_t)lo) : (int64_t)lo;
    int64_t pos = 0;
    if (!Reposition(e, h, dist, method, &pos)) {
        DisableLocked(e);
        LeaveCriticalSection(&g_cs);
        return OrigSetFilePointer(h, lo, hi, method);
    }
    LeaveCriticalSection(&g_cs);

    if (hi)
        *hi = (LONG)(pos >> 32);
    SetLastError(NO_ERROR);
    return (DWORD)(pos & 0xFFFFFFFF);
}

BOOL WINAPI HookSetFilePointerEx(HANDLE h, LARGE_INTEGER dist, PLARGE_INTEGER newPos,
                                 DWORD method) {
    if (!g_ready)
        return OrigSetFilePointerEx(h, dist, newPos, method);

    EnterCriticalSection(&g_cs);
    HandleEnt* e = FindHandle(h);
    if (!e || e->fileId < 0) {
        LeaveCriticalSection(&g_cs);
        return OrigSetFilePointerEx(h, dist, newPos, method);
    }
    int64_t pos = 0;
    if (!Reposition(e, h, dist.QuadPart, method, &pos)) {
        DisableLocked(e);
        LeaveCriticalSection(&g_cs);
        return OrigSetFilePointerEx(h, dist, newPos, method);
    }
    LeaveCriticalSection(&g_cs);

    if (newPos)
        newPos->QuadPart = pos;
    SetLastError(NO_ERROR);
    return TRUE;
}

} // namespace

bool BlockCacheInstall() {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return true;

    g_slab = (uint8_t*)VirtualAlloc(nullptr, (SIZE_T)kBlock * kBlockCount,
                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_slab) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }
    for (size_t i = 0; i < kBlockCount; i++) {
        g_blocks[i].fileId = -1;
        g_blocks[i].data = g_slab + i * kBlock;
    }

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }

    InitializeCriticalSection(&g_cs);

    struct { const char* name; void* detour; void** orig; } targets[] = {
        { "CreateFileW",      (void*)&HookCreateFileW,      (void**)&OrigCreateFileW },
        { "CreateFileA",      (void*)&HookCreateFileA,      (void**)&OrigCreateFileA },
        { "ReadFile",         (void*)&HookReadFile,         (void**)&OrigReadFile },
        { "WriteFile",        (void*)&HookWriteFile,        (void**)&OrigWriteFile },
        { "SetFilePointer",   (void*)&HookSetFilePointer,   (void**)&OrigSetFilePointer },
        { "SetFilePointerEx", (void*)&HookSetFilePointerEx, (void**)&OrigSetFilePointerEx },
        { "CloseHandle",      (void*)&HookCloseHandle,      (void**)&OrigCloseHandle },
    };
    for (auto& t : targets) {
        void* fn = (void*)GetProcAddress(k32, t.name);
        if (!fn)
            continue;
        if (MH_CreateHook(fn, t.detour, t.orig) == MH_OK)
            MH_EnableHook(fn);
    }

    // Every one of these is needed to keep a served handle's position coherent.
    // Without the full set the cache would hand back bytes from the wrong
    // offset, so it stays off rather than run half-wired.
    if (!OrigCreateFileW || !OrigCreateFileA || !OrigReadFile || !OrigWriteFile ||
        !OrigSetFilePointer || !OrigSetFilePointerEx || !OrigCloseHandle) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }

    g_ready = true;
    return true;
}

void BlockCacheShutdown() { g_ready = false; }
