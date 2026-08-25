#include "pch.h"
#include ".\Soulworker Packet\SWPacketBrooch.h"
#include ".\Buff Meter\Buff Meter.h"
#include ".\Damage Meter\Damage Meter.h"

SWPacketBrooches::SWPacketBrooches(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {

}

void SWPacketBrooches::Do() {
	SWPACKETBROOCH* brooch_trigger = (SWPACKETBROOCH*)(_data + sizeof(SWHEADER));

	switch (brooch_trigger->_broochID) {
	case BROOCHID_FEVER:
		DAMAGEMETER.AddBroochProc(brooch_trigger->_playerID, BROOCH_FEVER);
		break;
	case BROOCHID_FURY:
		DAMAGEMETER.AddBroochProc(brooch_trigger->_playerID, BROOCH_FURY);
		break;
	case BROOCHID_BACKSTEP:
		DAMAGEMETER.AddBroochProc(brooch_trigger->_playerID, BROOCH_BACKSTEP);
		break;
	case BROOCHID_TECHNIC:
		DAMAGEMETER.AddBroochProc(brooch_trigger->_playerID, BROOCH_TECHNIC);
		break;
	}
}

void SWPacketBrooches::Log() {

}

void SWPacketBrooches::Debug() {

}