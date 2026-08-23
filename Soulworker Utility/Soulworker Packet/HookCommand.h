#pragma once

#define HOOK_COMMAND_PIPE_NAME L"\\\\.\\pipe\\SoulMeterHookCmd"

// Wire ops, shared with the hook (SoulMeterHook/gamecmd.h).
#define HOOK_CMD_RESTART_MAZE 1
#define HOOK_CMD_EXIT_MAZE 2

DWORD HookCommandStart();

bool HookCommandIsConnected();

// Fire and forget; false when the hook is not connected or the write failed.
bool HookCommandSend(uint8_t op, uint32_t arg);

// Hotkey actions. Both refuse in town, mirroring the game's own guard.
void HookCommandRestartMaze();
void HookCommandExitMaze();
