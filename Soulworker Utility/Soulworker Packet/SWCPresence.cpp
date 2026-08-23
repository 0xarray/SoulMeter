#include "pch.h"
#include ".\Soulworker Packet\SWCPresence.h"
#include ".\Damage Meter\Damage Meter.h"
SWCPresence::SWCPresence(SWHEADER* swheader, uint8_t* data) {
	_swheader = swheader;
	_data = data;
}

void SWCPresence::Do() {
	SWPACKET_PRESENCE* presencepacket = (SWPACKET_PRESENCE*)(_data + sizeof(SWHEADER));
	std::string utf8name;
	utf8name.assign((const char*)(_data + sizeof(SWHEADER) + sizeof(SWPACKET_PRESENCE)), presencepacket->nicklength);
	auto meta = DAMAGEMETER.GetPlayerMetaData(DAMAGEMETER.GetMyID());
	if (meta)
	{
		meta->_job = presencepacket->playerclass;
	}
	DAMAGEMETER.SetWorldID(presencepacket->maze);
	return;
}

void SWCPresence::Log() {
	return;
}

void SWCPresence::Debug() {
	return;
}