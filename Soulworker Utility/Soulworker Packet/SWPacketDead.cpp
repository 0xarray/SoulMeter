#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Soulworker Packet\SWPacketDead.h"
#include ".\Damage Meter\Damage Meter.h"


SWPacketDead::SWPacketDead(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {

}

void SWPacketDead::Do() {
	SWPACKET_DEAD* stat_header = (SWPACKET_DEAD*)(_data + sizeof(SWHEADER));

	uint32_t playerId = stat_header->_playerid;
	if (DAMAGEMETER.CheckPlayer(playerId)) {
		DAMAGEMETER.AddDeath(playerId);
	}
}
