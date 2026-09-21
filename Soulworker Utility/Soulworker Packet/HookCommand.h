#pragma once

#define HOOK_COMMAND_PIPE_NAME L"\\\\.\\pipe\\SoulMeterHookCmd"

// Wire ops, shared with the hook (SoulMeterHook/gamecmd.h).
#define HOOK_CMD_RESTART_MAZE 1
#define HOOK_CMD_EXIT_MAZE 2
#define HOOK_CMD_SET_FPS_CAP 3
#define HOOK_CMD_UNLOCK_FOV 4

#define HOOK_FPS_GAME 0u             // hand the frame rate back to the game
#define HOOK_FPS_UNCAPPED 0xFFFFFFFFu

DWORD HookCommandStart();

bool HookCommandIsConnected();

// Fire and forget; false when the hook is not connected or the write failed.
bool HookCommandSend(uint8_t op, uint32_t arg);

// Hotkey actions. Both refuse in town, mirroring the game's own guard.
void HookCommandRestartMaze();
void HookCommandExitMaze();

// Game tweaks. Both are remembered and re-sent whenever the hook reconnects,
// so they survive a game restart.
void HookCommandSetFpsCap(uint32_t fps);
void HookCommandSetFovUnlock(bool unlock);
