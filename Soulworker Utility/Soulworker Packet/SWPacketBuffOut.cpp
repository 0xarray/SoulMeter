#include "pch.h"
#include ".\Soulworker Packet\SWPacketBuffOut.h"
#include ".\Buff Meter\Buff Meter.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Combat Meter\CombatMeter.h"

SWPacketBuffOut::SWPacketBuffOut(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketBuffOut::Do() {

	BUFFMETER.GetLock();
	{
		SWPACKETBUFFOUT* buff = (SWPACKETBUFFOUT*)(_data + sizeof(SWHEADER));

		if (DAMAGEMETER.CheckPlayer(buff->_playerID)) {
			DAMAGEMETER.BuffOut(buff->_playerID, buff->_buffID);
			BUFFMETER.EndBuff(buff->_playerID, buff->_buffID);

			CombatLog* pCombatLog = new CombatLog;
			pCombatLog->_type = CombatLogType::BUFF_END;
			pCombatLog->_val1 = buff->_buffID;
			COMBATMETER.Insert(buff->_playerID, CombatType::PLAYER, pCombatLog);
		}
			
	}
	BUFFMETER.FreeLock();
}
