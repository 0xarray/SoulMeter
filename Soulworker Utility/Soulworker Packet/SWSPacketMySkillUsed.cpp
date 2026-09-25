#include "pch.h"
#include ".\Soulworker Packet\SWSPacketMySkillUsed.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Combat Meter\CombatMeter.h"

SWSPacketMySkillUsed::SWSPacketMySkillUsed(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

VOID SWSPacketMySkillUsed::Do() {
	SWPACKET_MYSKILLUSED* skillUsed = (SWPACKET_MYSKILLUSED*)(_data + sizeof(SWHEADER));
	DAMAGEMETER.AddSkillUsed(skillUsed->_playerId, skillUsed->_skillId);

	CombatLog* pCombatLog = new CombatLog;
	pCombatLog->_type = CombatLogType::USED_SKILL;
	pCombatLog->_val1 = skillUsed->_skillId;
	COMBATMETER.Insert(skillUsed->_playerId, CombatType::PLAYER, pCombatLog);
	return;
}
