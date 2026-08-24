#pragma once

// Read-ahead block cache for the archive files.
//
// Mounting datas\data62.v .. data88.v walks their file tables in ~194 byte
// reads with a seek before nearly every one -- 2.57M reads and 1.13M seeks in
// about five seconds, roughly 80% of which is spent inside ReadFile itself. The
// cost is per-call overhead rather than bytes moved, so serving those reads out
// of 32KB blocks collapses the syscall count by around 100x.
//
// Serving a read means the cache owns that handle's file position: the real one
// stops advancing. Every call that reads or moves that position is therefore
// hooked, and anything the cache cannot model -- an overlapped read, a write,
// an unknown seek origin, a failed fetch -- restores the real pointer and
// permanently stops serving that handle, so the fallback is always the
// unmodified behaviour.

bool BlockCacheInstall();
void BlockCacheShutdown();
