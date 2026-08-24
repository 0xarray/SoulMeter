#pragma once

#include <windows.h>
#include <cstdint>

// Read-ahead block cache for the archive files.
//
// Mounting datas\data62.v .. data88.v walks their file tables in ~194 byte
// reads with a seek before nearly every one -- 2.57M reads and 1.13M seeks in
// about five seconds, roughly 80% of which is spent inside ReadFile itself. The
// cost is per-call overhead rather than bytes moved, so serving those reads out
// of aligned blocks collapses the syscall count by orders of magnitude.
//
// This module holds policy only. The profiler owns the kernel32 detours and
// feeds events in, because MinHook allows one hook per target and both need the
// same entry points.
//
// The cache owns the file position of any handle it serves, so every call that
// reads or moves that position must be routed here. Anything it cannot model --
// an overlapped read, a write, an unknown seek origin -- permanently disables
// caching for that handle after restoring the real file pointer, so the
// fallback is always the unmodified behaviour.

typedef BOOL(WINAPI* BcReadFn)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL(WINAPI* BcSeekFn)(HANDLE, LARGE_INTEGER, PLARGE_INTEGER, DWORD);

// The originals are supplied by the profiler so the cache never re-enters the
// detours when it does its own I/O.
bool BlockCacheInstall(BcReadFn realRead, BcSeekFn realSeek);
void BlockCacheShutdown();

void BlockCacheNoteOpen(HANDLE h, const char* path, DWORD access, DWORD flags);
void BlockCacheNoteClose(HANDLE h);

// Stops serving this handle and puts the real file pointer back where the
// caller believes it is.
void BlockCacheDisable(HANDLE h);

// True when the call was fully satisfied from the cache.
bool BlockCacheRead(HANDLE h, void* buf, DWORD toRead, DWORD* read);
bool BlockCacheSeek(HANDLE h, int64_t dist, DWORD method, int64_t* outPos);

void BlockCacheStats(char* out, unsigned cap);
