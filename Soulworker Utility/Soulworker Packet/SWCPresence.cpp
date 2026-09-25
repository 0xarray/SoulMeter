#include "pch.h"
#include ".\Soulworker Packet\SWCPresence.h"
#include ".\Damage Meter\Damage Meter.h"
SWCPresence::SWCPresence(SWHEADER* swheader, uint8_t* data) : SWPacket(swheader, data) {
}

void SWCPresence::Do() {
	SWPACKET_PRESENCE* presencepacket = (SWPACKET_PRESENCE*)(_data + sizeof(SWHEADER));
	auto meta = DAMAGEMETER.GetPlayerMetaData(DAMAGEMETER.GetMyID());
	if (meta)
	{
		meta->_job = presencepacket->playerclass;
	}
	DAMAGEMETER.SetWorldID(presencepacket->maze);
	return;
}
