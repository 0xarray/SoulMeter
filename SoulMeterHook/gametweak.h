#pragma once

#include <cstdint>

// The two game-side tweaks the meter can drive: the frame-rate cap and a camera
// FOV scale. Each resolves its own target and refuses to act when the target is
// missing, so a patch-day change turns the tweak off rather than corrupting
// something else.

// Safe to call repeatedly; resolves and hooks at most once.
void GameTweakInstall();
void GameTweakShutdown();

// SMH_FPS_GAME, SMH_FPS_UNCAPPED, or the cap in frames/second. Safe from any
// thread.
void GameTweakSetFpsCap(uint32_t fps);

// Raises the camera's zoom-out limit. Safe from any thread.
void GameTweakSetFovUnlock(bool unlock);
