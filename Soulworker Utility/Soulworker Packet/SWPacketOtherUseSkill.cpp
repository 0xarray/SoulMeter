#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Damage Meter\MySQLite.h"
#include ".\Soulworker Packet\SWPacketOtherUseSkill.h"
#include ".\UI\UtillWindow.h"
#include ".\Combat Meter\CombatMeter.h"

SWPacketOtherUseSkill::SWPacketOtherUseSkill(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {

}

void SWPacketOtherUseSkill::Do() {
	//	SWPACKETCHAT_HEADER* chat_header = (SWPACKETCHAT_HEADER*)(_data + sizeof(SWHEADER));

	//	BYTE* p_data = _data + sizeof(SWHEADER) + sizeof(SWPACKETCHAT_HEADER);

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

	/*uint32_t userId = otherSkill->_playerId;
	if (!DAMAGEMETER.CheckPlayer(userId)) {
		uint32_t ownerId = DAMAGEMETER.GetOwnerID(userId);
		if (!DAMAGEMETER.CheckPlayer(ownerId)) {
			SW_DB2_STRUCT* db = DAMAGEMETER.GetMonsterDB(userId);
			uint32_t db2 = 0;
			if (db != nullptr) {
				db2 = db->_db2;
			}

			if (db2 == 31310101) {
				//Log::MyLog("1");
				ZeroMemory(_skillName, SKILL_NAME_LEN);
			//	Log::MyLog("2");
				SWDB.GetSkillName(otherSkill->_skillId, _skillName, SKILL_NAME_LEN);
				//Log::MyLog("3");
				string test = u8" [";
				test.append(_skillName);
				test.append(u8"]");
				UTILLWINDOW.AddLog(test);
				//Log::MyLog(test.c_str());
			}
		}
	}*/


}

void SWPacketOtherUseSkill::Log() {

}

void SWPacketOtherUseSkill::Debug() {

	//Log::MyLog("UseSkill Packet\n");
	//for (int i = sizeof(SWHEADER); i < _swheader->_size; i++)
	//	Log::MyLog("%02x ", _data[i]);
	//Log::MyLog("\n");

	//SWPACKET_OTHERUSESKILL* otherSkill = (SWPACKET_OTHERUSESKILL*)(_data + sizeof(SWHEADER));

	//Log::MyLog("id : %08x / skill : %u / x : %f / y : %f / z : %f\n", otherSkill->_playerId, otherSkill->_skillId, otherSkill->_locationX, otherSkill->_locationY, otherSkill->_locationZ);

	//Log::MyLog("Unknown01 : ");
	//for (int i = 0; i < sizeof(otherSkill->_unknown01); i++) {
	//	Log::MyLog("%02x ", otherSkill->_unknown01[i]);
	//}
	//Log::MyLog("\n");

}