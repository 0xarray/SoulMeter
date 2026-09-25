#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Soulworker Packet\SWPacketOtherUseSkill.h"
#include ".\Combat Meter\CombatMeter.h"

SWPacketOtherUseSkill::SWPacketOtherUseSkill(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {
}

void SWPacketOtherUseSkill::Do() {

	SWPACKET_OTHERUSESKILL* otherSkill = (SWPACKET_OTHERUSESKILL*)(_data + sizeof(SWHEADER));

	DAMAGEMETER.AddSkillUsed(otherSkill->_playerId, otherSkill->_skillId);

	// check id
	uint32_t userId = otherSkill->_playerId;
	bool isPlayer = TRUE;
	if (!DAMAGEMETER.CheckPlayer(userId)) {
		// is summon
		uint32_t ownerId = DAMAGEMETER.GetOwnerID(userId);

		// is mob
		if (!DAMAGEMETER.CheckPlayer(ownerId))
		{
			SW_DB2_STRUCT* db = DAMAGEMETER.GetMonsterDB(userId);
			if (db != nullptr) {
				isPlayer = FALSE;
				userId = db->_db2;
			}
		}
		else {
			userId = ownerId;
		}
	}

	if (!isPlayer) {
		DAMAGEMETER.LogBossSkill(otherSkill->_playerId, DAMAGEMETER.GetMonsterDB(otherSkill->_playerId), otherSkill->_skillId);

		if (pauseSkillIdList.contains(otherSkill->_skillId))
			DAMAGEMETER.SuspendBossImmune();
	}

	CombatLog* pCombatLog = new CombatLog;
	pCombatLog->_type = CombatLogType::USED_SKILL;
	pCombatLog->_val1 = otherSkill->_skillId;
	COMBATMETER.Insert(userId, isPlayer ? CombatType::PLAYER : CombatType::MONSTER, pCombatLog);
}
