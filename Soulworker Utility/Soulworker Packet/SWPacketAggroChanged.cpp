#include "pch.h"
#include ".\Soulworker Packet\SWPacketAggroChanged.h"
#include ".\Damage Meter\Damage Meter.h"

SWPacketAggroChanged::SWPacketAggroChanged(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketAggroChanged::Do() {

	SWPACKETAGGROCHANGED* aggro_data = (SWPACKETAGGROCHANGED*)(_data + sizeof(SWHEADER));
	DAMAGEMETER.SetAggro(aggro_data->_id, aggro_data->_targetedId);

}
