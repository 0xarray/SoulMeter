#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Soulworker Packet\SWPacketWorldChange.h"

SWPacketWorldChange::SWPacketWorldChange(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketWorldChange::Do() {
	
	DAMAGEMETER.Clear();

	SWPACKETWORLDCHANGE* world_change = (SWPACKETWORLDCHANGE*)(_data + sizeof(SWHEADER));

	DAMAGEMETER.SetMyID(world_change->_id);
	DAMAGEMETER.SetWorldID(world_change->_worldID);

	DAMAGEMETER.SetMazeState(FALSE);
}
