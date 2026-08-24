#pragma once

// Load-time optimisations applied to the game image. Each one resolves its own
// target by signature and refuses to act when the signature is missing or
// ambiguous, so a patch-day netcode or logic change disables the tweak rather
// than corrupting an unrelated instruction.

// Returns the number of optimisations applied. Safe to call repeatedly; each
// one applies at most once.
int LoadOptApply();
