#include "pch.h"
#include ".\Soulworker Packet\SWPacketBuffIn.h"
#include ".\Buff Meter\Buff Meter.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\UI\PlotWindow.h"
#include ".\Combat Meter\CombatMeter.h"

SWPacketBuffIn::SWPacketBuffIn(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketBuffIn::Do() {

	BUFFMETER.GetLock();
	{

		SWPACKETBUFFIN* buff = (SWPACKETBUFFIN*)(_data + sizeof(SWHEADER));

		if (DAMAGEMETER.CheckPlayer(buff->_playerID)) {
			DAMAGEMETER.BuffIn(buff->_playerID, buff->_buffID, buff->_stack, buff->_giverID);
			BUFFMETER.AddBuff(buff->_playerID, buff->_buffID, buff->_stack);

			// Desire, LG loop send this packet BRUH
			if (buff->_buffID != 43104 && buff->_buffID != 43105)
			{
				CombatLog* pCombatLog = new CombatLog;
				pCombatLog->_type = CombatLogType::BUFF_STARTED;
				pCombatLog->_val1 = buff->_buffID;
				pCombatLog->_val2 = buff->_stack;
				COMBATMETER.Insert(buff->_playerID, CombatType::PLAYER, pCombatLog);
			}
		}

	}
	BUFFMETER.FreeLock();

}
