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

	for (int i = 0; i < playerNum; i++) {
		SWPACKETDAMAGE_MONSTER* player = (SWPACKETDAMAGE_MONSTER*)((_data + sizeof(SWPACKETDAMAGE_MONSTER) * i) + sizeof(SWHEADER) + 1);

		DAMAGEMETER.AddPlayerGetDamage(player->_monsterID, player->_totalDMG, player->_damageType, monster->_playerID, monster->_skillID);
	}
}

void SWPacketDamage::Do() {

	BYTE monsterNum = *(_data + sizeof(SWHEADER));
	SWPACKETDAMAGE_PLAYER* player = (SWPACKETDAMAGE_PLAYER*)((_data + sizeof(_SWPACKETDAMAGE_MONSTER) * monsterNum) + sizeof(SWHEADER) + 1);

	// If attacker is not a player, then check it is summoned object and get summoner's id for checking is summoner a player
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
