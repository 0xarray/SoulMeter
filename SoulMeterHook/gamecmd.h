#pragma once

#include <cstdint>

// Wire ops, shared with the meter (Soulworker Packet/HookCommand.h).
#define SMH_CMD_RESTART_MAZE 1
#define SMH_CMD_EXIT_MAZE 2
#define SMH_CMD_SET_FPS_CAP 3   // arg: frames/second, or one of SMH_FPS_* below
#define SMH_CMD_UNLOCK_FOV 4    // arg: 1 = raise the camera's zoom-out limit
#define SMH_CMD_MAX 4

#define SMH_FPS_GAME 0u             // hand the frame rate back to the game
#define SMH_FPS_UNCAPPED 0xFFFFFFFFu

// False until SoulWorker64.dll is loaded and has a window.
bool GameCmdInit();
void GameCmdShutdown();

// netMgr `this`, captured from the send hook rather than a pinned global.
void GameCmdSetNetMgr(void* netMgr);

// Queues `op` onto the game's message-pump thread. Safe from any thread.
void GameCmdPost(uint8_t op, uint32_t arg);
