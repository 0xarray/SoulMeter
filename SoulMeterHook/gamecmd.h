#pragma once

#include <cstdint>

// Wire ops, shared with the meter (Soulworker Packet/HookCommand.h).
#define SMH_CMD_RESTART_MAZE 1
#define SMH_CMD_EXIT_MAZE 2

// False until SoulWorker64.dll is loaded, resolved and has a window.
bool GameCmdInit();
void GameCmdShutdown();

// netMgr `this`, captured from the send hook rather than a pinned global.
void GameCmdSetNetMgr(void* netMgr);

// Queues `op` onto the game's message-pump thread. Safe from any thread.
void GameCmdPost(uint8_t op, uint32_t arg);
