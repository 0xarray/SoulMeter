#include "pch.h"
#include ".\Soulworker Packet\SWPacketcharacterUpdateSpecialOptionList.h"
#include ".\Buff Meter\Buff Meter.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\UI\PlotWindow.h"
#include ".\Combat Meter\CombatMeter.h"

SWPacketcharacterUpdateSpecialOptionList::SWPacketcharacterUpdateSpecialOptionList(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {

}

void SWPacketcharacterUpdateSpecialOptionList::Do() {

	if (DAMAGEMETER.isTownMap()) {
		return;
	}

	short offset = sizeof(SWHEADER);
	SWPacketcharacterUpdateSpecialOptionListHeader* listPacket = (SWPacketcharacterUpdateSpecialOptionListHeader*)(_data + offset);
	offset += sizeof(SWPacketcharacterUpdateSpecialOptionListHeader);

	for (BYTE i = 0; i < listPacket->_listCount; i++)
	{
		SWPacketcharacterUpdateSpecialOptionVal* valPacket = (SWPacketcharacterUpdateSpecialOptionVal*)(_data + offset);
		offset += sizeof(SWPacketcharacterUpdateSpecialOptionVal);

		if (valPacket->_listID >= SpecialStatType::End)
		{
#if _DEBUG
			LogInstance.WriteLog("[SWPacketcharacterUpdateSpecialOptionList] Find Unknown statType = %x, statValue = %f", valPacket->_listID, valPacket->_listVal);
#endif
			continue;
		}

		DAMAGEMETER.UpdateSpecialStat(listPacket->_playerID, valPacket->_listID, valPacket->_listVal);

		CombatLog* pCombatLog = new CombatLog;
		pCombatLog->_type = CombatLogType::CHANGED_SPECIAL_STATS;
		pCombatLog->_val1 = valPacket->_listID;
		pCombatLog->_val2 = valPacket->_listVal;
		COMBATMETER.Insert(listPacket->_playerID, CombatType::PLAYER, pCombatLog);
	}
}
