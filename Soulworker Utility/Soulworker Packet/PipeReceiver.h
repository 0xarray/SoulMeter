#pragma once

#define PIPE_RECEIVER_PIPE_NAME L"\\\\.\\pipe\\SoulMeterHook"

bool PipeReceiverIsConnected();

DWORD PipeReceiverStart();
