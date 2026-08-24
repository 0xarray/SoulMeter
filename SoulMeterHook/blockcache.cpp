#include "blockcache.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kBlock = 32 * 1024;
constexpr size_t kWays = 2;
constexpr size_t kSets = 512;                  // power of two
constexpr size_t kBlockCount = kSets * kWays;  // 32 MB
constexpr size_t kMaxFiles = 256;
constexpr size_t kHandleSlots = 4096;          // power of two
constexpr size_t kPathMax = 260;

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

BcReadFn RealRead = nullptr;
BcSeekFn RealSeek = nullptr;

volatile LONG64 g_served = 0;
volatile LONG64 g_hits = 0;
volatile LONG64 g_fetches = 0;
volatile LONG64 g_bypassed = 0;

inline size_t HSlot(HANDLE h) { return ((size_t)(uintptr_t)h >> 2) & (kHandleSlots - 1); }

constexpr size_t kProbe = 8;

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

// Caller holds the lock.
void DisableLocked(HandleEnt* e) {
    if (!e || e->fileId < 0)
        return;
    LARGE_INTEGER li;
    li.QuadPart = e->pos;
    RealSeek(e->h, li, nullptr, FILE_BEGIN);
    e->fileId = -1;
    InterlockedIncrement64(&g_bypassed);
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
            InterlockedIncrement64(&g_hits);
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
    if (!RealSeek(h, li, nullptr, FILE_BEGIN)) {
        victim->fileId = -1;
        return nullptr;
    }
    DWORD got = 0;
    if (!RealRead(h, victim->data, kBlock, &got, nullptr)) {
        victim->fileId = -1;
        return nullptr;
    }

    victim->fileId = fileId;
    victim->index = idx;
    victim->valid = got;
    victim->lastUse = ++g_clock;
    InterlockedIncrement64(&g_fetches);
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

} // namespace

bool BlockCacheInstall(BcReadFn realRead, BcSeekFn realSeek) {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return true;
    if (!realRead || !realSeek) {
        InterlockedExchange(&g_installed, 0);
        return false;
    }

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

    RealRead = realRead;
    RealSeek = realSeek;
    InitializeCriticalSection(&g_cs);
    g_ready = true;
    return true;
}

void BlockCacheShutdown() { g_ready = false; }

void BlockCacheNoteOpen(HANDLE h, const char* path, DWORD access, DWORD flags) {
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

void BlockCacheNoteClose(HANDLE h) {
    if (!g_ready)
        return;
    EnterCriticalSection(&g_cs);
    HandleEnt* e = FindHandle(h);
    if (e) {
        e->live = false;
        e->h = nullptr;
        e->fileId = -1;
    }
    LeaveCriticalSection(&g_cs);
}

void BlockCacheDisable(HANDLE h) {
    if (!g_ready)
        return;
    EnterCriticalSection(&g_cs);
    HandleEnt* e = FindHandle(h);
    if (e && e->fileId >= 0) {
        InvalidateFile(e->fileId);
        DisableLocked(e);
    }
    LeaveCriticalSection(&g_cs);
}

bool BlockCacheRead(HANDLE h, void* buf, DWORD toRead, DWORD* read) {
    if (!g_ready)
        return false;

    EnterCriticalSection(&g_cs);
    HandleEnt* e = FindHandle(h);
    if (!e || e->fileId < 0) {
        LeaveCriticalSection(&g_cs);
        return false;
    }

    int fileId = e->fileId;
    int64_t pos = e->pos;
    uint32_t done = 0;
    bool ok = true;

    while (done < toRead) {
        uint64_t at = (uint64_t)pos + done;
        Block* b = GetBlock(h, fileId, at / kBlock);
        if (!b) {
            ok = false;
            break;
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

    if (!ok) {
        DisableLocked(e);
        LeaveCriticalSection(&g_cs);
        return false;
    }

    e->pos = pos + done;
    LeaveCriticalSection(&g_cs);

    if (read)
        *read = done;
    InterlockedIncrement64(&g_served);
    SetLastError(NO_ERROR);
    return true;
}

bool BlockCacheSeek(HANDLE h, int64_t dist, DWORD method, int64_t* outPos) {
    if (!g_ready)
        return false;

    EnterCriticalSection(&g_cs);
    HandleEnt* e = FindHandle(h);
    if (!e || e->fileId < 0) {
        LeaveCriticalSection(&g_cs);
        return false;
    }

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
        if (sz < 0) {
            DisableLocked(e);
            LeaveCriticalSection(&g_cs);
            return false;
        }
        base = sz;
        break;
    }
    default:
        DisableLocked(e);
        LeaveCriticalSection(&g_cs);
        return false;
    }

    int64_t p = base + dist;
    if (p < 0) {
        DisableLocked(e);
        LeaveCriticalSection(&g_cs);
        return false;
    }
    e->pos = p;
    LeaveCriticalSection(&g_cs);

    *outPos = p;
    SetLastError(NO_ERROR);
    return true;
}

void BlockCacheStats(char* out, unsigned cap) {
    _snprintf_s(out, cap, _TRUNCATE,
                "block cache: %lld reads served, %lld block hits, %lld block fetches, "
                "%lld handles bypassed\n",
                (long long)g_served, (long long)g_hits, (long long)g_fetches,
                (long long)g_bypassed);
}
