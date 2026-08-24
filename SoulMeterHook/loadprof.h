#pragma once

// Load-time profiler. Attributes every ReadFile in the process to the file it
// came from and records the access shape (read sizes, seeks, wall time), so the
// cost of startup archive mounting and of a zone load can be told apart from
// each other and from CPU-side work.

bool LoadProfInstall();
void LoadProfShutdown();

// Rewrites the report. Safe to call from any thread.
void LoadProfDumpReport();
