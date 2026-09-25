#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Soulworker Packet\SWPacketAkasic.h"

SWPacketAkasic::SWPacketAkasic(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketAkasic::Do() {

	SWPACKETAKASIC* akasic = (SWPACKETAKASIC*)(_data + sizeof(SWHEADER));

	DAMAGEMETER.InsertOwnerID(akasic->_id, akasic->_ownder_id);
}
