#include "pch.h"
#include ".\Soulworker Packet\SWPacketCooldown.h"
#include ".\Buff Meter\Buff Meter.h"
#include ".\Damage Meter\Damage Meter.h"

SWPacketCooldown::SWPacketCooldown(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketCooldown::Do() {
	SWPACKETCOOLDOWN* cooldown = (SWPACKETCOOLDOWN*)(_data + sizeof(SWHEADER));

	DAMAGEMETER.AddEnlighten(cooldown->_playerID, cooldown->_value);
}
