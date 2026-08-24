#include "loadprof.h"

#include <windows.h>
#include <MinHook.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "blockcache.h"
#include "peutil.h"

namespace {

constexpr size_t kMaxFiles = 512;
constexpr size_t kHandleSlots = 4096;   // power of two
constexpr size_t kPathMax = 260;

// <=4K, <=16K, <=64K, <=256K, <=1M, >1M
constexpr int kBuckets = 6;

struct FileStat {
    char path[kPathMax];
    uint64_t opens;
    uint64_t reads;
    uint64_t bytes;
    uint64_t readTicks;      // QPC in ReadFile
    uint64_t seeks;
    uint64_t seekBytes;      // total absolute distance skipped
    uint64_t backSeeks;      // seeks to a lower offset: read-ahead cannot help these
    uint64_t sizeHist[kBuckets];
    uint64_t maps;           // MapViewOfFile calls: these bypass ReadFile entirely
    uint64_t mapBytes;
    uint64_t asyncReads;     // overlapped: bytes land later, so timing here is meaningless
    uint64_t cacheHits;      // served from the block cache, never reached the kernel
    uint64_t firstTick;
    uint64_t lastTick;
    int64_t  nextOffset;     // -1 when unknown
};

struct HandleEnt {
    HANDLE h;
    int fileIdx;
};

FileStat g_files[kMaxFiles];
size_t g_fileCount = 0;
HandleEnt g_handles[kHandleSlots];

CRITICAL_SECTION g_cs;
bool g_csReady = false;
volatile LONG g_installed = 0;

// Sampled callers. Knowing which file is read says nothing about which code is
// reading it, and that is what decides whether a read pattern can be fixed.
constexpr size_t kMaxCallers = 96;
constexpr LONG kSampleEvery = 512;

struct CallerSite {
    const char* mod;
    uint64_t rva;
    uint64_t hits;
};

CallerSite g_callers[kMaxCallers];
size_t g_callerCount = 0;
volatile LONG g_sampleCounter = 0;

struct ModRange {
    const char* name;
    uint8_t* base;
    size_t size;
};

ModRange g_mods[3];
size_t g_modCount = 0;
bool g_modsResolved = false;

void ResolveModules() {
    static const wchar_t* kWanted[] = { L"SoulWorker64.dll", L"GamePlugin.vPlugin",
                                        L"VisionDX11.dll" };
    static const char* kNames[] = { "SoulWorker64", "GamePlugin", "VisionDX11" };
    g_modCount = 0;
    for (int i = 0; i < 3; i++) {
        HMODULE m = GetModuleHandleW(kWanted[i]);
        if (!m)
            continue;
        IMAGE_NT_HEADERS64* nt = pe::NtHeaders((uint8_t*)m);
        if (!nt)
            continue;
        g_mods[g_modCount].name = kNames[i];
        g_mods[g_modCount].base = (uint8_t*)m;
        g_mods[g_modCount].size = nt->OptionalHeader.SizeOfImage;
        g_modCount++;
    }
    // All three are loaded well before the archive walk; if one is missing the
    // rest still attribute correctly.
    g_modsResolved = g_modCount > 0;
}

// Caller holds the lock.
void RecordCaller(const char* mod, uint64_t rva) {
    for (size_t i = 0; i < g_callerCount; i++) {
        if (g_callers[i].rva == rva && g_callers[i].mod == mod) {
            g_callers[i].hits++;
            return;
        }
    }
    if (g_callerCount >= kMaxCallers)
        return;
    g_callers[g_callerCount].mod = mod;
    g_callers[g_callerCount].rva = rva;
    g_callers[g_callerCount].hits = 1;
    g_callerCount++;
}

void SampleCaller() {
    void* frames[24];
    USHORT n = CaptureStackBackTrace(1, 24, frames, nullptr);
    if (!n)
        return;

    EnterCriticalSection(&g_cs);
    if (!g_modsResolved)
        ResolveModules();
    // The first frame inside game code, skipping our own hook and the CRT.
    for (USHORT i = 0; i < n; i++) {
        uint8_t* p = (uint8_t*)frames[i];
        for (size_t m = 0; m < g_modCount; m++) {
            if (p >= g_mods[m].base && p < g_mods[m].base + g_mods[m].size) {
                RecordCaller(g_mods[m].name, (uint64_t)(p - g_mods[m].base));
                LeaveCriticalSection(&g_cs);
                return;
            }
        }
    }
    LeaveCriticalSection(&g_cs);
}

uint64_t g_qpf = 1;
uint64_t g_startTick = 0;

char g_reportPath[MAX_PATH] = { 0 };
char g_timelinePath[MAX_PATH] = { 0 };

// Previous-tick totals, for per-interval deltas.
uint64_t g_prevReads = 0, g_prevBytes = 0, g_prevTicks = 0, g_prevSeeks = 0;
uint64_t g_prevPerFileReads[kMaxFiles] = { 0 };
uint64_t g_prevTickStamp = 0;
bool g_timelineStarted = false;

// --- originals -------------------------------------------------------------

typedef HANDLE(WINAPI* CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef HANDLE(WINAPI* CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);
typedef BOOL(WINAPI* ReadFileFn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef DWORD(WINAPI* SetFilePointerFn)(HANDLE, LONG, PLONG, DWORD);
typedef BOOL(WINAPI* SetFilePointerExFn)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);
typedef BOOL(WINAPI* WriteFileFn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI* CloseHandleFn)(HANDLE);
typedef HANDLE(WINAPI* CreateFileMappingAFn)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD,
                                             LPCSTR);
typedef HANDLE(WINAPI* CreateFileMappingWFn)(HANDLE, LPSECURITY_ATTRIBUTES, DWORD, DWORD, DWORD,
                                             LPCWSTR);
typedef LPVOID(WINAPI* MapViewOfFileFn)(HANDLE, DWORD, DWORD, DWORD, SIZE_T);

CreateFileWFn OrigCreateFileW = nullptr;
CreateFileAFn OrigCreateFileA = nullptr;
ReadFileFn OrigReadFile = nullptr;
SetFilePointerFn OrigSetFilePointer = nullptr;
SetFilePointerExFn OrigSetFilePointerEx = nullptr;
WriteFileFn OrigWriteFile = nullptr;
CloseHandleFn OrigCloseHandle = nullptr;
CreateFileMappingAFn OrigCreateFileMappingA = nullptr;
CreateFileMappingWFn OrigCreateFileMappingW = nullptr;
MapViewOfFileFn OrigMapViewOfFile = nullptr;

// --- helpers ---------------------------------------------------------------

inline uint64_t Now() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return (uint64_t)li.QuadPart;
}

inline double MsSince(uint64_t t) { return (double)t * 1000.0 / (double)g_qpf; }

// Only the last two path components are kept: the archives all live in one
// directory and the full path buries the interesting part.
void ShortenPath(const char* in, char* out, size_t outCap) {
    size_t len = strlen(in);
    size_t seps = 0;
    size_t start = 0;
    for (size_t i = len; i > 0; i--) {
        if (in[i - 1] == '\\' || in[i - 1] == '/') {
            if (++seps == 2) {
                start = i;
                break;
            }
        }
    }
    size_t n = len - start;
    if (n >= outCap)
        n = outCap - 1;
    memcpy(out, in + start, n);
    out[n] = 0;
}

int BucketOf(DWORD n) {
    if (n <= 4 * 1024) return 0;
    if (n <= 16 * 1024) return 1;
    if (n <= 64 * 1024) return 2;
    if (n <= 256 * 1024) return 3;
    if (n <= 1024 * 1024) return 4;
    return 5;
}

inline size_t HandleSlot(HANDLE h) {
    return ((size_t)(uintptr_t)h >> 2) & (kHandleSlots - 1);
}

// Caller holds the lock.
int FindOrAddFile(const char* shortPath) {
    for (size_t i = 0; i < g_fileCount; i++) {
        if (strcmp(g_files[i].path, shortPath) == 0)
            return (int)i;
    }
    if (g_fileCount >= kMaxFiles)
        return -1;
    int idx = (int)g_fileCount++;
    FileStat& f = g_files[idx];
    memset(&f, 0, sizeof(f));
    strncpy_s(f.path, shortPath, _TRUNCATE);
    f.nextOffset = -1;
    f.firstTick = Now();
    return idx;
}

// Caller holds the lock.
void BindHandle(HANDLE h, int fileIdx) {
    // A handle value is only reused after a close, so overwriting on open keeps
    // the table correct without hooking CloseHandle.
    size_t slot = HandleSlot(h);
    for (size_t probe = 0; probe < 8; probe++) {
        HandleEnt& e = g_handles[(slot + probe) & (kHandleSlots - 1)];
        if (e.h == h || e.h == nullptr) {
            e.h = h;
            e.fileIdx = fileIdx;
            return;
        }
    }
    HandleEnt& e = g_handles[slot];
    e.h = h;
    e.fileIdx = fileIdx;
}

// Caller holds the lock.
int LookupHandle(HANDLE h) {
    size_t slot = HandleSlot(h);
    for (size_t probe = 0; probe < 8; probe++) {
        const HandleEnt& e = g_handles[(slot + probe) & (kHandleSlots - 1)];
        if (e.h == h)
            return e.fileIdx;
        if (e.h == nullptr)
            return -1;
    }
    return -1;
}

// Data files only. Hooking every handle in the process would bury the signal in
// GameGuard's own scanning and in log writes.
bool IsInteresting(const char* path) {
    size_t len = strlen(path);
    if (len < 3)
        return false;
    const char* ext = path + len;
    while (ext > path && *(ext - 1) != '.' && *(ext - 1) != '\\' && *(ext - 1) != '/')
        ext--;
    return _stricmp(ext, "v") == 0 || _stricmp(ext, "vArc") == 0 ||
           _stricmp(ext, "vPlugin") == 0 || _stricmp(ext, "swf") == 0 ||
           _stricmp(ext, "gfx") == 0 || _stricmp(ext, "dat") == 0 ||
           _stricmp(ext, "vResources") == 0;
}

void NoteOpen(HANDLE h, const char* path) {
    if (h == INVALID_HANDLE_VALUE || !path || !IsInteresting(path))
        return;

    char shortPath[kPathMax];
    ShortenPath(path, shortPath, sizeof(shortPath));

    EnterCriticalSection(&g_cs);
    int idx = FindOrAddFile(shortPath);
    if (idx >= 0) {
        g_files[idx].opens++;
        g_files[idx].nextOffset = -1;
        BindHandle(h, idx);
    }
    LeaveCriticalSection(&g_cs);
}

// --- hooks -----------------------------------------------------------------

HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share,
                              LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = OrigCreateFileW(name, access, share, sa, disp, flags, tmpl);
    __try {
        if (h != INVALID_HANDLE_VALUE && name) {
            char narrow[kPathMax];
            int n = WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof(narrow), nullptr,
                                        nullptr);
            if (n > 0) {
                NoteOpen(h, narrow);
                BlockCacheNoteOpen(h, narrow, access, flags);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return h;
}

HANDLE WINAPI HookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                              DWORD disp, DWORD flags, HANDLE tmpl) {
    HANDLE h = OrigCreateFileA(name, access, share, sa, disp, flags, tmpl);
    __try {
        if (h != INVALID_HANDLE_VALUE && name) {
            NoteOpen(h, name);
            BlockCacheNoteOpen(h, name, access, flags);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return h;
}

BOOL WINAPI HookWriteFile(HANDLE h, LPCVOID buf, DWORD n, LPDWORD wrote, LPOVERLAPPED ov) {
    BlockCacheDisable(h);
    return OrigWriteFile(h, buf, n, wrote, ov);
}

BOOL WINAPI HookCloseHandle(HANDLE h) {
    BlockCacheNoteClose(h);
    return OrigCloseHandle(h);
}

BOOL WINAPI HookReadFile(HANDLE h, LPVOID buf, DWORD toRead, LPDWORD read, LPOVERLAPPED ov) {
    // Only time the call when the handle is one we track, so untracked I/O pays
    // nothing but the lookup.
    int idx = -1;
    if (g_csReady) {
        EnterCriticalSection(&g_cs);
        idx = LookupHandle(h);
        LeaveCriticalSection(&g_cs);
    }
    if (idx < 0) {
        // Still cacheable: an archive opened before the profiler was armed has
        // no stats slot but the cache tracks its own handles.
        if (!ov) {
            DWORD served = 0;
            if (BlockCacheRead(h, buf, toRead, &served)) {
                if (read)
                    *read = served;
                return TRUE;
            }
        } else {
            BlockCacheDisable(h);
        }
        return OrigReadFile(h, buf, toRead, read, ov);
    }

    if (ov) {
        BlockCacheDisable(h);
    } else {
        DWORD served = 0;
        if (BlockCacheRead(h, buf, toRead, &served)) {
            if (read)
                *read = served;
            EnterCriticalSection(&g_cs);
            FileStat& f = g_files[idx];
            f.reads++;
            f.bytes += served;
            f.cacheHits++;
            f.sizeHist[BucketOf(toRead)]++;
            if (f.nextOffset >= 0)
                f.nextOffset += served;
            f.lastTick = Now();
            LeaveCriticalSection(&g_cs);
            return TRUE;
        }
    }

    if (InterlockedIncrement(&g_sampleCounter) % kSampleEvery == 0) {
        __try {
            SampleCaller();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    uint64_t t0 = Now();
    BOOL ok = OrigReadFile(h, buf, toRead, read, ov);
    uint64_t dt = Now() - t0;

    __try {
        DWORD got = (read && ok) ? *read : 0;
        EnterCriticalSection(&g_cs);
        FileStat& f = g_files[idx];
        if (ov) {
            // Completion is reported through the IOCP, not here. Count it so the
            // report shows the gap instead of pretending the read was free.
            f.asyncReads++;
            f.bytes += toRead;
        } else {
            f.reads++;
            f.bytes += got;
            f.readTicks += dt;
            f.sizeHist[BucketOf(toRead)]++;
            if (f.nextOffset >= 0)
                f.nextOffset += got;
        }
        f.lastTick = Now();
        LeaveCriticalSection(&g_cs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ok;
}

void NoteSeek(HANDLE h, int64_t target) {
    if (!g_csReady)
        return;
    EnterCriticalSection(&g_cs);
    int idx = LookupHandle(h);
    if (idx >= 0) {
        FileStat& f = g_files[idx];
        f.seeks++;
        if (f.nextOffset >= 0) {
            int64_t delta = target - f.nextOffset;
            if (delta < 0) {
                f.backSeeks++;
                delta = -delta;
            }
            f.seekBytes += (uint64_t)delta;
        }
        f.nextOffset = target;
    }
    LeaveCriticalSection(&g_cs);
}

DWORD WINAPI HookSetFilePointer(HANDLE h, LONG lo, PLONG hi, DWORD method) {
    int64_t dist = hi ? (((int64_t)*hi << 32) | (uint32_t)lo) : (int64_t)lo;
    int64_t pos = 0;
    if (BlockCacheSeek(h, dist, method, &pos)) {
        NoteSeek(h, pos);
        if (hi)
            *hi = (LONG)(pos >> 32);
        return (DWORD)(pos & 0xFFFFFFFF);
    }

    DWORD r = OrigSetFilePointer(h, lo, hi, method);
    __try {
        if (r != INVALID_SET_FILE_POINTER || GetLastError() == NO_ERROR) {
            int64_t pos = (int64_t)r;
            if (hi)
                pos |= ((int64_t)*hi) << 32;
            NoteSeek(h, pos);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return r;
}

BOOL WINAPI HookSetFilePointerEx(HANDLE h, LARGE_INTEGER dist, PLARGE_INTEGER newPos,
                                 DWORD method) {
    int64_t cached = 0;
    if (BlockCacheSeek(h, dist.QuadPart, method, &cached)) {
        NoteSeek(h, cached);
        if (newPos)
            newPos->QuadPart = cached;
        return TRUE;
    }

    LARGE_INTEGER local;
    local.QuadPart = 0;
    BOOL ok = OrigSetFilePointerEx(h, dist, newPos ? newPos : &local, method);
    __try {
        if (ok)
            NoteSeek(h, newPos ? newPos->QuadPart : local.QuadPart);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return ok;
}

// A mapping handle inherits the file's slot, so a later MapViewOfFile can be
// charged to the file it actually reads.
void NoteMapping(HANDLE file, HANDLE mapping) {
    if (!g_csReady || !mapping)
        return;
    EnterCriticalSection(&g_cs);
    int idx = LookupHandle(file);
    if (idx >= 0)
        BindHandle(mapping, idx);
    LeaveCriticalSection(&g_cs);
}

HANDLE WINAPI HookCreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD prot,
                                     DWORD hi, DWORD lo, LPCSTR name) {
    HANDLE m = OrigCreateFileMappingA(file, sa, prot, hi, lo, name);
    __try {
        NoteMapping(file, m);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return m;
}

HANDLE WINAPI HookCreateFileMappingW(HANDLE file, LPSECURITY_ATTRIBUTES sa, DWORD prot,
                                     DWORD hi, DWORD lo, LPCWSTR name) {
    HANDLE m = OrigCreateFileMappingW(file, sa, prot, hi, lo, name);
    __try {
        NoteMapping(file, m);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return m;
}

LPVOID WINAPI HookMapViewOfFile(HANDLE mapping, DWORD access, DWORD offHi, DWORD offLo,
                                SIZE_T bytes) {
    LPVOID p = OrigMapViewOfFile(mapping, access, offHi, offLo, bytes);
    __try {
        if (p && g_csReady) {
            EnterCriticalSection(&g_cs);
            int idx = LookupHandle(mapping);
            if (idx >= 0) {
                g_files[idx].maps++;
                g_files[idx].mapBytes += bytes;
                g_files[idx].lastTick = Now();
            }
            LeaveCriticalSection(&g_cs);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return p;
}

// --- reporting -------------------------------------------------------------

void BuildReportPath() {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        strcpy_s(g_reportPath, "swloadprof.log");
        strcpy_s(g_timelinePath, "swloadtimeline.log");
        return;
    }
    for (DWORD i = n; i > 0; i--) {
        if (exe[i - 1] == '\\') {
            exe[i] = 0;
            break;
        }
    }
    sprintf_s(g_reportPath, "%sswloadprof.log", exe);
    sprintf_s(g_timelinePath, "%sswloadtimeline.log", exe);
}

} // namespace

void LoadProfTick() {
    if (!g_csReady)
        return;

    uint64_t now = Now();
    uint64_t reads = 0, bytes = 0, ticks = 0, seeks = 0;

    // Busiest file this interval, which is what identifies the phase.
    char topName[kPathMax] = { 0 };
    uint64_t topReads = 0;

    EnterCriticalSection(&g_cs);
    for (size_t i = 0; i < g_fileCount; i++) {
        const FileStat& f = g_files[i];
        reads += f.reads;
        bytes += f.bytes;
        ticks += f.readTicks;
        seeks += f.seeks;
        uint64_t d = f.reads - g_prevPerFileReads[i];
        if (d > topReads) {
            topReads = d;
            strncpy_s(topName, f.path, _TRUNCATE);
        }
        g_prevPerFileReads[i] = f.reads;
    }
    LeaveCriticalSection(&g_cs);

    if (!g_timelineStarted) {
        g_timelineStarted = true;
        g_prevTickStamp = now;
        FILE* fh = nullptr;
        if (fopen_s(&fh, g_timelinePath, "w") == 0 && fh) {
            fprintf(fh, "%8s %10s %10s %10s %10s  %s\n", "t(s)", "reads", "MB", "ms", "seeks",
                    "busiest file");
            fclose(fh);
        }
    }

    uint64_t dReads = reads - g_prevReads;
    g_prevTickStamp = now;
    g_prevReads = reads;

    if (dReads == 0) {
        g_prevBytes = bytes;
        g_prevTicks = ticks;
        g_prevSeeks = seeks;
        return;
    }

    FILE* fp = nullptr;
    if (fopen_s(&fp, g_timelinePath, "a") != 0 || !fp)
        return;
    fprintf(fp, "%8.1f %10llu %10.2f %10.1f %10llu  %s\n", MsSince(now - g_startTick) / 1000.0,
            (unsigned long long)dReads, (double)(bytes - g_prevBytes) / (1024.0 * 1024.0),
            MsSince(ticks - g_prevTicks), (unsigned long long)(seeks - g_prevSeeks), topName);
    fclose(fp);

    g_prevBytes = bytes;
    g_prevTicks = ticks;
    g_prevSeeks = seeks;
}

void LoadProfDumpReport() {
    if (!g_csReady)
        return;

    FILE* fp = nullptr;
    if (fopen_s(&fp, g_reportPath, "w") != 0 || !fp)
        return;

    EnterCriticalSection(&g_cs);

    uint64_t totalBytes = 0, totalReads = 0, totalTicks = 0, totalSeeks = 0, totalBack = 0;
    uint64_t totalMaps = 0, totalMapBytes = 0;
    for (size_t i = 0; i < g_fileCount; i++) {
        totalBytes += g_files[i].bytes;
        totalReads += g_files[i].reads;
        totalTicks += g_files[i].readTicks;
        totalSeeks += g_files[i].seeks;
        totalBack += g_files[i].backSeeks;
        totalMaps += g_files[i].maps;
        totalMapBytes += g_files[i].mapBytes;
    }

    fprintf(fp, "SoulMeter load profile\n");
    fprintf(fp, "uptime %.1fs  files %zu\n", MsSince(Now() - g_startTick) / 1000.0, g_fileCount);
    fprintf(fp, "total: %llu reads, %.1f MB, %.0f ms in ReadFile, %llu seeks (%llu backward)\n",
            (unsigned long long)totalReads, (double)totalBytes / (1024.0 * 1024.0),
            MsSince(totalTicks), (unsigned long long)totalSeeks,
            (unsigned long long)totalBack);
    if (totalReads)
        fprintf(fp, "avg read %.0f bytes, avg %.3f ms/read\n",
                (double)totalBytes / (double)totalReads, MsSince(totalTicks) / (double)totalReads);
    // Mapped bytes never reach ReadFile, so a file with mappings and few reads
    // is being paged in, not streamed, and its cost is invisible above.
    fprintf(fp, "mapped: %llu views, %.1f MB\n", (unsigned long long)totalMaps,
            (double)totalMapBytes / (1024.0 * 1024.0));
    char bc[256];
    BlockCacheStats(bc, sizeof(bc));
    fputs(bc, fp);

    fprintf(fp, "\n%-34s %8s %10s %9s %8s %7s  %s\n", "file", "reads", "MB", "ms", "seeks",
            "back", "read sizes <4K/16K/64K/256K/1M/+");
    for (size_t i = 0; i < g_fileCount; i++) {
        const FileStat& f = g_files[i];
        if (!f.reads && !f.opens)
            continue;
        fprintf(fp, "%-34s %8llu %10.2f %9.1f %8llu %7llu  %llu/%llu/%llu/%llu/%llu/%llu",
                f.path, (unsigned long long)f.reads, (double)f.bytes / (1024.0 * 1024.0),
                MsSince(f.readTicks), (unsigned long long)f.seeks,
                (unsigned long long)f.backSeeks,
                (unsigned long long)f.sizeHist[0], (unsigned long long)f.sizeHist[1],
                (unsigned long long)f.sizeHist[2], (unsigned long long)f.sizeHist[3],
                (unsigned long long)f.sizeHist[4], (unsigned long long)f.sizeHist[5]);
        if (f.maps)
            fprintf(fp, "  mapped %llu/%.1fMB", (unsigned long long)f.maps,
                    (double)f.mapBytes / (1024.0 * 1024.0));
        if (f.asyncReads)
            fprintf(fp, "  async %llu", (unsigned long long)f.asyncReads);
        if (f.cacheHits)
            fprintf(fp, "  cached %llu", (unsigned long long)f.cacheHits);
        fprintf(fp, "\n");
    }

    // Sorted on a copy: the report is rewritten every second and must not
    // disturb the counters it reads.
    fprintf(fp, "\nread callers, sampled 1 in %d (rva is module-relative)\n", (int)kSampleEvery);
    CallerSite sorted[kMaxCallers];
    size_t sortedCount = g_callerCount;
    memcpy(sorted, g_callers, sortedCount * sizeof(CallerSite));
    for (size_t i = 1; i < sortedCount; i++) {
        CallerSite key = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1].hits < key.hits) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }
    for (size_t i = 0; i < sortedCount && i < 25; i++)
        fprintf(fp, "  %-14s +%08llX  %llu\n", sorted[i].mod,
                (unsigned long long)sorted[i].rva, (unsigned long long)sorted[i].hits);

    fprintf(fp, "\nfirst/last access, seconds since inject\n");
    for (size_t i = 0; i < g_fileCount; i++) {
        const FileStat& f = g_files[i];
        if (!f.reads && !f.maps)
            continue;
        fprintf(fp, "%-34s %8.2f %8.2f  opens %llu\n", f.path,
                MsSince(f.firstTick - g_startTick) / 1000.0,
                MsSince(f.lastTick - g_startTick) / 1000.0, (unsigned long long)f.opens);
    }

    LeaveCriticalSection(&g_cs);
    fclose(fp);
}

bool LoadProfInstall() {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0)
        return true;

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpf = (uint64_t)f.QuadPart;
    g_startTick = Now();

    InitializeCriticalSection(&g_cs);
    g_csReady = true;
    BuildReportPath();

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

    struct { const char* name; void* detour; void** orig; } targets[] = {
        { "CreateFileW",      (void*)&HookCreateFileW,      (void**)&OrigCreateFileW },
        { "CreateFileA",      (void*)&HookCreateFileA,      (void**)&OrigCreateFileA },
        { "ReadFile",         (void*)&HookReadFile,         (void**)&OrigReadFile },
        { "SetFilePointer",   (void*)&HookSetFilePointer,   (void**)&OrigSetFilePointer },
        { "SetFilePointerEx", (void*)&HookSetFilePointerEx, (void**)&OrigSetFilePointerEx },
        { "CreateFileMappingA", (void*)&HookCreateFileMappingA, (void**)&OrigCreateFileMappingA },
        { "CreateFileMappingW", (void*)&HookCreateFileMappingW, (void**)&OrigCreateFileMappingW },
        { "MapViewOfFile",    (void*)&HookMapViewOfFile,    (void**)&OrigMapViewOfFile },
        { "WriteFile",        (void*)&HookWriteFile,        (void**)&OrigWriteFile },
        { "CloseHandle",      (void*)&HookCloseHandle,      (void**)&OrigCloseHandle },
    };

    for (auto& t : targets) {
        void* fn = (void*)GetProcAddress(k32, t.name);
        if (!fn)
            continue;
        if (MH_CreateHook(fn, t.detour, t.orig) == MH_OK)
            MH_EnableHook(fn);
    }

    if (!OrigReadFile)
        return false;

    // The cache needs every one of these to keep a served handle's position
    // coherent; without the full set it would hand back bytes from the wrong
    // offset, so it stays off rather than run half-wired.
    if (OrigSetFilePointerEx && OrigSetFilePointer && OrigCloseHandle && OrigWriteFile &&
        OrigCreateFileW && OrigCreateFileA)
        BlockCacheInstall(OrigReadFile, OrigSetFilePointerEx);

    return true;
}

void LoadProfShutdown() {
    if (InterlockedCompareExchange(&g_installed, 0, 1) != 1)
        return;
    LoadProfDumpReport();
}
