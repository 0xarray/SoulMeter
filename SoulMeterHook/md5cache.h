#pragma once

// Startup memoisation for the archive integrity hash.
//
// The client MD5s datas\data01.v .. data60.v plus GameEtc\packinginfo.dat on
// every launch. That is ~9.2 GiB of file content and it measured at ~57s of a
// ~95s cold start, only ~21% of which is inside ReadFile -- the rest is the MD5
// itself, so no amount of read caching touches it.
//
// The archives only change on patch day, so the hash is memoised against the
// file's size and last-write time and replayed on later launches. The value
// handed back is the same string the game would have computed, so nothing
// downstream -- local comparison or server-side check -- can tell the
// difference; a file whose size or timestamp moved is rehashed normally.

bool Md5CacheInstall();
void Md5CacheShutdown();
