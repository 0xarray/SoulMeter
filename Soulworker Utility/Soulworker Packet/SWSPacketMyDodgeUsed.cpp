#include "pch.h"
#include ".\Soulworker Packet\SWSPacketMyDodgeUsed.h"
#include ".\Damage Meter\Damage Meter.h"

SWSPacketMyDodgeUsed::SWSPacketMyDodgeUsed(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

VOID SWSPacketMyDodgeUsed::Do() {
	SWPACKET_MYDODGEUSED* dodgeUsed = (SWPACKET_MYDODGEUSED*)(_data + sizeof(SWHEADER));

	// check is it normal dodge (ground dodge) as rolling (roll after down) is also there
	if (normalDodgeList.find(dodgeUsed->_skillId) == normalDodgeList.end()) {
		return;
	}

	DAMAGEMETER.AddDodgeUsed(dodgeUsed->_playerId);

	return;
}
