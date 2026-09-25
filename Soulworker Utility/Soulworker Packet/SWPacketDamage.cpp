#include "pch.h"
#include ".\Soulworker Packet\SWPacket.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\Soulworker Packet\SWPacketDamage.h"
#include ".\PacketInfo.h"
#include ".\Damage Meter\MySQLite.h"
#include ".\Damage Meter\MapList.h"

SWPacketDamage::SWPacketDamage(SWHEADER* swheader, BYTE* data) : SWPacket(swheader, data) {

}

void SWPacketDamage::AttackOfMonster()
{
	BYTE playerNum = *(_data + sizeof(SWHEADER));
	SWPACKETDAMAGE_PLAYER* monster = (SWPACKETDAMAGE_PLAYER*)((_data + sizeof(_SWPACKETDAMAGE_MONSTER) * playerNum) + sizeof(SWHEADER) + 1);
	//Log::MyLog("skill ID : %u\n", monster->_skillID);


	for (int i = 0; i < playerNum; i++) {
		SWPACKETDAMAGE_MONSTER* player = (SWPACKETDAMAGE_MONSTER*)((_data + sizeof(SWPACKETDAMAGE_MONSTER) * i) + sizeof(SWHEADER) + 1);
		//Log::MyLog("hitted Player : %08x / damageType : %d\n", player->_monsterID, player->_damageType);

		if (DAMAGEMETER.CheckPlayer(player->_monsterID)) {
			//Log::MyLog("hitted Player : %08x / damage : %u\n", player->_monsterID, player->_totalDMG);
		}
		
		//LogInstance.WriteLog(const_cast<char*>("[DEBUG] [PlayerNum = 0x%01x] [PlayerID = %04x]"), playerNum, player->_monsterID);

		DAMAGEMETER.AddPlayerGetDamage(player->_monsterID, player->_totalDMG, player->_damageType, monster->_playerID, monster->_skillID);
	}
}

void SWPacketDamage::Do() {

	BYTE monsterNum = *(_data + sizeof(SWHEADER));
	SWPACKETDAMAGE_PLAYER* player = (SWPACKETDAMAGE_PLAYER*)((_data + sizeof(_SWPACKETDAMAGE_MONSTER) * monsterNum) + sizeof(SWHEADER) + 1);

	if (player->_playerID == NULL) {
		player->_playerID = 0;
	}

	//if (!DAMAGEMETER.CheckPlayer(player->_playerID))
	//	LogInstance.WriteLog(const_cast<char*>("[DEBUG] [ID = %04x]"), player->_playerID);

	// If attacker is not a player, then check it is summoned object and get summoner's id for checking is summoner a player
	// 
	if (!DAMAGEMETER.CheckPlayer(player->_playerID)) {
		uint32_t owner_id = 0xffffffff;
		if ((owner_id = DAMAGEMETER.GetOwnerID(player->_playerID)) == 0xffffffff) {
			AttackOfMonster();
			return;
		}
		else {
			if (!DAMAGEMETER.CheckPlayer(owner_id)) {
				AttackOfMonster();
				return;
			}
			else {
				player->_playerID = owner_id;
			}
		}
	}

	for (int i = 0; i < monsterNum; i++) {
		SWPACKETDAMAGE_MONSTER* monster = (SWPACKETDAMAGE_MONSTER*)((_data + sizeof(SWPACKETDAMAGE_MONSTER) * i) + sizeof(SWHEADER) + 1);

		//LogInstance.WriteLog("[PLAYER] [DamageType = %d]", monster->_damageType);
			//DAMAGEMETER.AddDamage(player->_playerID, monster->_totalDMG, monster->_criticalDMG, player->_maxCombo != 0 ? 1 : 0, player->_maxCombo != 0 ? CheckCritical(monster->_damageType) : 0, player->_maxCombo, monster->_monsterID, player->_skillID);

		//LogInstance.WriteLog(const_cast<char*>("[DEBUG] [MonsterNum = 0x%01x] [MonsterID = %04x] [PlayerID = %04x] [CheckPlayer = %d] [GetOwnerID = %04x] [OwnerCheckPlayer = %d]"),
		//	monsterNum, monster->_monsterID, player->_playerID, DAMAGEMETER.CheckPlayer(player->_playerID),
		//	DAMAGEMETER.GetOwnerID(player->_playerID), DAMAGEMETER.CheckPlayer(DAMAGEMETER.GetOwnerID(player->_playerID)));

		SW_DB2_STRUCT* db = DAMAGEMETER.GetMonsterDB(monster->_monsterID);
		uint32_t db2 = 0;
		// Don't calc not in db monster
		if (db != nullptr) {
			db2 = db->_db2;

			DAMAGEMETER.SetMonsterRemainHP(monster->_monsterID, monster->_remainHP);
			DAMAGEMETER.AddDamage(player->_playerID, monster->_totalDMG, monster->_soulstoneDMG, (SWPACKETDAMAGE_DAMAGETYPE)(monster->_damageType),
				player->_maxCombo, monster->_monsterID, player->_skillID);

			if (UIOPTION.isSoloRankMode() && rankMap.find(DAMAGEMETER.GetWorldID()) != rankMap.end()) {
				return;
			}

			if (monster->_remainHP <= 0) {
				bool isEndId = false;
				if (endIdList.find(db2) != endIdList.end() || db->_type == 4)
					isEndId = true;

				if (UIOPTION.isTeamTALF() && DAMAGEMETER.GetWorldID() == 22061 && LunarFallBossListId.find(db2) != LunarFallBossListId.end()) {
					bool skipClear = false;
					// only edgar+queen
					if (UIOPTION.TeamTALFMode() == 2)
					{
						if (db2 != 31309101)
							skipClear = true;
					}
					if (!skipClear) {
						DAMAGEMETER.Clear();
						DAMAGEMETER.SetMazeState(isEndId);
					}
				}

				if (pauseIdList.find(db2) != pauseIdList.end()) {
					DAMAGEMETER.Suspend();
				}
				else if (isEndId) {
					DAMAGEMETER.SetMazeState(TRUE);
					DAMAGEMETER.Suspend();
				}
			}
		}
	}
}

void SWPacketDamage::Debug() {

	/*BYTE monsterNum = *(_data + sizeof(SWHEADER));
	SWPACKETDAMAGE_PLAYER* player = (SWPACKETDAMAGE_PLAYER*)((_data + sizeof(_SWPACKETDAMAGE_MONSTER) * monsterNum) + sizeof(SWHEADER) + 2);

	//LogInstance.WriteLog(const_cast<char*>("[DEBUG] [MonsterNum = %d] [PlayerID = %08x] [CheckPlayer = %d] [GetOwnerID = %08x] [OwnerCheckPlayer = %d]"), monsterNum, player->_playerID, DAMAGEMETER.CheckPlayer(player->_playerID), DAMAGEMETER.GetOwnerID(player->_playerID), DAMAGEMETER.CheckPlayer(DAMAGEMETER.GetOwnerID(player->_playerID)));
	//Log::MyLog("1234");

	//if (DAMAGEMETER.CheckPlayer(player->_playerID)) {
	//	return;
	//}

	if (!DAMAGEMETER.CheckPlayer(player->_playerID)) {
		uint32_t owner_id = 0xffffffff;
		if ((owner_id = DAMAGEMETER.GetOwnerID(player->_playerID)) == 0xffffffff) {
			return;
		}
		else {
			if (!DAMAGEMETER.CheckPlayer(owner_id)) {
				return;
			}
			else {
				player->_playerID = owner_id;
			}
		}
	}

	for (int i = 0; i < monsterNum; i++) {
		SWPACKETDAMAGE_MONSTER* monster = (SWPACKETDAMAGE_MONSTER*)((_data + sizeof(SWPACKETDAMAGE_MONSTER) * i) + sizeof(SWHEADER) + 2);

		//if (monster->_monsterID != 0x0034eee4) {
		//	return;
		//}

		/*Log::MyLog("unknown02\n");
		for (int i = 0; i < 14; i++)
			Log::MyLog("%02x ", monster->_unknown02[i]);
		Log::MyLog("\n");

		Log::MyLog("Unknown 01 : %02x", monster->_unknown01);

		LogInstance.WriteLog(const_cast<char*>("\t[unknown01 = %02x] [UnknownF1 = %f] [UnknownF2 = %f] [UnknownF3 = %f]"),
		monster->_unknown01,monster->_unknownF1, monster->_unknownF2, monster->_unknownF3);

		LogInstance.WriteLog(const_cast<char*>("\t[PlayerID = %08x] [MonsterID = %08x] [SkillID = %08x] [Damage = %d] [remainHP = %d]"),
			player->_playerID, monster->_monsterID, player->_skillID, monster->_totalDMG, monster->_remainHP);*/
	//}
}