#include "pch.h"
#include ".\Soulworker Packet\SWPacketMazeStart.h"
#include ".\Damage Meter\Damage Meter.h"

SWPacketMazeStart::SWPacketMazeStart(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketMazeStart::Do() {
	DAMAGEMETER.SetMazeState(FALSE);
}
