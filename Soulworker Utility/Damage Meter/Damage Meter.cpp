#include "pch.h"
#include ".\Damage Meter\Timer.h"
#include ".\Damage Meter\SWDamagePlayer.h"
#include ".\Damage Meter\Damage Meter.h"
#include ".\UI\PlotWindow.h"
#include ".\Damage Meter\History.h"
#include ".\Damage Meter\MySQLite.h"
#include ".\Combat Meter\CombatMeter.h"
#include ".\UI\UtillWindow.h"
#include ".\Damage Meter\MapList.h"

SWDamageMeter::~SWDamageMeter() {

	bool canLock = _mutex.try_lock();

	if (_historyMode)
		Restore();

	for (auto itr = _playerInfo.begin(); itr != _playerInfo.end(); itr++)
		delete (*itr);

	for (auto itr = _ownerInfo.begin(); itr != _ownerInfo.end(); itr++)
		delete (*itr);

	ClearDB();

	for (auto itr = _playerMetadata.begin(); itr != _playerMetadata.end(); itr++)
		delete (itr->second);

	_playerInfo.clear();
	_ownerInfo.clear();
	_dbInfo.clear();
	_playerMetadata.clear();

	_historyPlayerInfo.clear();
	_historyDbInfo.clear();
	_historyPlayerMetadata.clear();

	_playerUseAwaken.clear();

	FreeLock();
}

void SWDamageMeter::GetLock() {
	_mutex.lock();
}
void SWDamageMeter::FreeLock() {
	_mutex.unlock();
}
uint32_t SWDamageMeter::GetPing()
{
	if (!_historyMode)
		return _ping;
	else
		return _historyPing;
}
void SWDamageMeter::SetPing(uint32_t ping)
{
	_ping = ping;
}
void SWDamageMeter::InsertPlayerInfo(uint32_t id, uint64_t totalDMG, uint64_t soulstoneDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, unsigned short maxCombo, uint32_t monsterID, uint32_t skillID) {
	auto itr = _playerInfo.begin();

	for (; itr != _playerInfo.end(); itr++) {
		if (id == (*itr)->GetID()) {
			(*itr)->AddDamage(totalDMG, soulstoneDMG, damageType, maxCombo, monsterID, skillID);
			return;
		}
	}

	_playerInfo.push_back(new SWDamagePlayer(id, totalDMG, soulstoneDMG, damageType, maxCombo, monsterID, skillID));
}

void SWDamageMeter::AddDamage(uint32_t id, uint64_t totalDMG, uint64_t soulstoneDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, unsigned short maxCombo, uint32_t monsterID, uint32_t skillID) {
	if (_mazeEnd)
		return;

	unsigned short usWorldID = GetWorldID();
	// Meter must know mazeId to work correctly
	if (usWorldID == 0)
		return;

	SW_DB2_STRUCT* db = DAMAGEMETER.GetMonsterDB(monsterID);
	uint32_t monsterId = 0;
	bool bIsListBoss = false, bIsListMap = false;
	if (db != nullptr) {
		monsterId = db->_db2;
	}

	auto stList = StrictModeList.find(usWorldID);
	if (stList != StrictModeList.end())
	{
		auto& vec = (*stList).second;
		auto find = std::find(vec.begin(), vec.end(), monsterId);
		if (find != vec.end())
			bIsListBoss = true;
		bIsListMap = true;
	}

	if (!isRun()) {
		// Do not start/resume if it is damage to Puppet Orb,Ephnel Octagon, or Nabi Bomb wick
		if (resumeIgnoreIdList.find(monsterId) != resumeIgnoreIdList.end()) {
			return;
		}

		// only damage in list Boss can start/resume timer.
		if (bIsListMap && !bIsListBoss)
			return;
	}

	if (totalDMG > 0)
	{
		CombatLog* pCombatLog = new CombatLog;
		pCombatLog->_type = COMBATMETER.ConvertDamageTypeForGiveDamage(damageType);
		pCombatLog->_val1 = static_cast<double>(totalDMG);
		pCombatLog->_val2 = static_cast<double>(soulstoneDMG);
		COMBATMETER.Insert(id, CombatType::PLAYER, pCombatLog);
	}
	else {
		// ignore if rank map
		if (rankMap.find(usWorldID) == rankMap.end())
		{
			// suspend by boss is god mode
			if ((bIsListMap && bIsListBoss) || (db != NULL && db->_type == 4)
				|| godModeIdList.find(monsterId) != godModeIdList.end())
			{
				DAMAGEMETER.Suspend();
				return;
			}
		}
	}

	Start();
	InsertPlayerInfo(id, totalDMG, soulstoneDMG, damageType, maxCombo, monsterID, skillID);
	Sort();
}

void SWDamageMeter::AddPlayerGetDamage(uint32_t playerId, uint64_t totalDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, uint32_t monsterID, uint32_t skillID)
{
	if (!isRun() || GetTime() == 0)
		return;

	if (CheckPlayer(playerId))
	{
		uint32_t monsterDB2 = 0;
		SW_DB2_STRUCT* db = DAMAGEMETER.GetMonsterDB(monsterID);
		if (db != nullptr) {
			monsterDB2 = db->_db2;
		}

		CombatLog* pCombatLog = new CombatLog;
		pCombatLog->_type = COMBATMETER.ConvertDamageTypeForTakeDamage(damageType);
		pCombatLog->_val1 = static_cast<double>(totalDMG);
		COMBATMETER.Insert(playerId, CombatType::PLAYER, pCombatLog);

		pCombatLog = new CombatLog;
		pCombatLog->_type = COMBATMETER.ConvertDamageTypeForGiveDamage(damageType);
		pCombatLog->_val1 = static_cast<double>(totalDMG);
		pCombatLog->_val2 = playerId;
		COMBATMETER.Insert(monsterDB2, CombatType::MONSTER, pCombatLog);
	}

	auto itr = _playerInfo.begin();

	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->AddGetDamage(totalDMG, damageType, monsterID, skillID);
			return;
		}
	}

	if (CheckPlayer(playerId)) {
		SWDamagePlayer* newPlayer = new SWDamagePlayer(playerId);
		newPlayer->AddGetDamage(totalDMG, damageType, monsterID, skillID);
		_playerInfo.push_back(newPlayer);
	}
}

void SWDamageMeter::AddEnlighten(uint32_t playerId, float value)
{
	if (!isRun() || GetTime() == 0)
		return;

	auto itr = _playerInfo.begin();

	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->AddEnlighten(value);
			return;
		}
	}

	if (CheckPlayer(playerId)) {
		SWDamagePlayer* newPlayer = new SWDamagePlayer(playerId);
		newPlayer->AddEnlighten(value);
		_playerInfo.push_back(newPlayer);
	}
}

void SWDamageMeter::AddBroochProc(uint32_t playerId, BroochProc type)
{
	if (!isRun() || GetTime() == 0)
		return;

	auto itr = _playerInfo.begin();
	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->AddBroochProc(type);
			return;
		}
	}

	if (CheckPlayer(playerId)) {
		SWDamagePlayer* newPlayer = new SWDamagePlayer(playerId);
		newPlayer->AddBroochProc(type);
		_playerInfo.push_back(newPlayer);
	}
}

void SWDamageMeter::AddSkillUsed(uint32_t playerId, uint32_t skillId)
{

	if (!isRun() || GetTime() == 0)
		return;

	// counts only skill that can proc enlighten (example : not right click)
	bool canProcEnlighten = false;
	if (skillId >= 10000000 && skillId < 1000000000) {
		if ((skillId % 100000) / 10000 == 0) {
			canProcEnlighten = true;
		}
	}
	if (!canProcEnlighten || !DAMAGEMETER.CheckPlayer(playerId)) {
		return;
	}

	if (std::find(_dwSkills.begin(), _dwSkills.end(), skillId) != _dwSkills.end())
	{
		DAMAGEMETER.AddAwakenPlayer(playerId);
	}

	auto itr = _playerInfo.begin();
	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->AddSkillUsed(skillId);
			return;
		}
	}

	SWDamagePlayer* newPlayer = new SWDamagePlayer(playerId);
	newPlayer->AddSkillUsed(skillId);
	_playerInfo.push_back(newPlayer);
}

void SWDamageMeter::AddDodgeUsed(uint32_t playerId)
{
	if (!isRun() || GetTime() == 0)
		return;

	auto itr = _playerInfo.begin();

	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->AddDodgeUsed();
			return;
		}
	}

	SWDamagePlayer* newPlayer = new SWDamagePlayer(playerId);
	newPlayer->AddDodgeUsed();
	_playerInfo.push_back(newPlayer);
}

void SWDamageMeter::AddDeath(uint32_t playerId)
{
	if (!isRun() || GetTime() == 0)
		return;

	auto itr = _playerInfo.begin();

	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->AddDeathCount();
			return;
		}
	}

	SWDamagePlayer* newPlayer = new SWDamagePlayer(playerId);
	newPlayer->AddDeathCount();
	_playerInfo.push_back(newPlayer);
}

void SWDamageMeter::BuffIn(uint32_t playerId, unsigned short buffId, BYTE stack, uint32_t giverId)
{
	if (_mazeEnd)
		return;
	if (buffId != 60228)
		return;

	auto itr = _playerInfo.begin();
	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->SetJqStack(stack);
			return;
		}
	}
}

void SWDamageMeter::BuffOut(uint32_t playerId, unsigned short buffId)
{
	if (_mazeEnd)
		return;
	// The Real Army 
	if (buffId != 60228)
		return;

	auto itr = _playerInfo.begin();
	for (; itr != _playerInfo.end(); itr++) {
		if (playerId == (*itr)->GetID()) {
			(*itr)->SetJqStack(0);
			return;
		}
	}
}

void SWDamageMeter::InsertOwnerID(uint32_t id, uint32_t owner_id) {

	auto itr = _ownerInfo.begin();

	for (; itr != _ownerInfo.end(); itr++) {
		if ((*itr)->_id == id) {
#if DEBUG_DAMAGEMETER_OWNER_ID == 1
			LogInstance.WriteLog("[DEBUG] [INSERT OWNER] [MODIFY] [ID = %08x] [OWNER = %08x]"), id, owner_id);
#endif
			(*itr)->_owner_id = owner_id;
			return;
		}
	}

	SW_OWNER_ID_STRUCT* obj = new SW_OWNER_ID_STRUCT;
	obj->_id = id;
	obj->_owner_id = owner_id;

#if DEBUG_DAMAGEMETER_OWNER_ID == 1
	LogInstance.WriteLog("[DEBUG] [INSERT OWNER] [NEW] [ID = %08x] [OWNER = %08x]"), obj->_id, obj->_owner_id);
#endif

	_ownerInfo.push_back(obj);
}

uint32_t SWDamageMeter::GetOwnerID(uint32_t id) {

	auto itr = _ownerInfo.begin();

	for (; itr != _ownerInfo.end(); itr++) {
		if ((*itr)->_id == id)
			return (*itr)->_owner_id;
	}

	return 0xffffffff;
}

void SWDamageMeter::InsertPlayerMetadata(uint32_t id, char* str, BYTE job) {

	auto search = _playerMetadata.find(id);

	if (search == _playerMetadata.end()) {
		SW_PLAYER_METADATA* metaData = new SW_PLAYER_METADATA;
		metaData->_id = id;
		metaData->_job = job;
		strcpy_s(metaData->_name, MAX_NAME_LEN, str);
		_playerMetadata[id] = metaData;

#if DEBUG_DAMAGEMETER_PLAYERMETA == 1
		LogInstance.WriteLog(const_cast<char*>("[DEBUG] [INSERT PLAYER META] [NEW] [ID = %08x] [NAME = %s] [JOB = %x]"), id, str, job);
#endif
		return;
	}
	SW_PLAYER_METADATA* metaData = search->second;
	metaData->_id = id;
	metaData->_job = job;
	strcpy_s(metaData->_name, MAX_NAME_LEN, str);
}

const char* SWDamageMeter::GetPlayerName(uint32_t id) {
	if ((_historyMode && id == _historyMyID) || (!_historyMode && id == _myID))
		return LANGMANAGER.GetText("STR_TABLE_YOU").data();

	auto search = GetPlayerMetaDataByHistory()->find(id);
	if (search == GetPlayerMetaDataByHistory()->end()) {
		return LANGMANAGER.GetText("PLAYER_NAME_CANT_FIND").data();
	}
	return search->second->_name;
}

BYTE SWDamageMeter::GetPlayerJob(uint32_t id) {

	auto search = GetPlayerMetaDataByHistory()->find(id);
	if (search == GetPlayerMetaDataByHistory()->end()) {
		return PLAYER_JOB_CANT_FIND;
	}
	return search->second->_job;
}

SWDamageMeter::SW_PLAYER_METADATA* SWDamageMeter::GetPlayerMetaDataIfNotExistsCreate(uint32_t id)
{
	SW_PLAYER_METADATA* metaData;

	auto search = _playerMetadata.find(id);
	if (search == _playerMetadata.end()) {
		metaData = new SW_PLAYER_METADATA;
		metaData->_id = id;
		strcpy_s(metaData->_name, MAX_NAME_LEN, LANGMANAGER.GetText("PLAYER_NAME_CANT_FIND").data());
		metaData->_job = PLAYER_JOB_CANT_FIND;
		_playerMetadata[id] = metaData;
	}
	else {
		metaData = search->second;
	}

	return metaData;
}

void SWDamageMeter::UpdateSpecialStat(uint32_t id, unsigned short statType, float statValue)
{
	UpdateStat(id, statType, statValue, TRUE);
}

void SWDamageMeter::UpdateStat(uint32_t id, unsigned short statType, float statValue, bool isSpecial)
{
	//if (_historyMode) {
	//	return;
	//}
	
	SW_PLAYER_METADATA* metaData = GetPlayerMetaDataIfNotExistsCreate(id);

	if (isSpecial)
		metaData->UpdateSpecialStat(statType, statValue);
	else
		metaData->UpdateStat(statType, statValue);

	return;
}


void SWDamageMeter::Sort() {
	sort(_playerInfo.begin(), _playerInfo.end(), SWDamagePlayer::SortFunction);
}

void SWDamageMeter::InsertDB(uint32_t id, uint32_t db2) {
	auto itr = _dbInfo.begin();

	for (; itr != _dbInfo.end(); itr++) {
		if ((*itr)->_id == id) {
#if DEBUG_DAMAGEMETER_DB == 1
			LogInstance.WriteLog("[DEBUG] [INSERT DB] [MODIFY] [ID = %04x] [DB1 = %d] [DB2 = %d]"), id, GetWorldID(), db2);
#endif
			(*itr)->_db2 = db2;
			return;
		}
	}

	int32_t type = -1;
	SWDB.GetMonsterType(db2, &type);

	SW_DB2_STRUCT* db = new SW_DB2_STRUCT;
	db->_id = id;
	db->_db2 = db2;
	db->_type = type;

#if DEBUG_DAMAGEMETER_DB == 1
	LogInstance.WriteLog("[DEBUG] [INSERT DB] [NEW] [ID = %04x] [DB2 = %d]"), db->_id, db->_db2);
#endif

	_dbInfo.push_back(db);
}

SW_DB2_STRUCT* SWDamageMeter::GetMonsterDB(uint32_t id) {

	for (auto itr = _dbInfo.begin(); itr != _dbInfo.end(); itr++) {
#if DEBUG_DAMAGEMETER_DB == 1
		LogInstance.WriteLog("[DEBUG] [GetMonsterDB] [FIND ID = %04x] [ID = %04x] [DB2 = %d]"), id, (*itr)->_id, (*itr)->_db2);
#endif
		if ((*itr)->_id == id) {
			return *itr;
		}
	}

	return nullptr;
}

void SWDamageMeter::SetWorldID(unsigned short worldID) {
	_worldID = worldID;

#if DEBUG_DAMAGEMETER_WORLD == 1
	LogInstance.WriteLog("[DEBUG] [Set World] [World ID = %d]"), _worldID);
#endif
}

unsigned short SWDamageMeter::GetWorldID() {

	if (!_historyMode)
		return _worldID;
	else
		return _historyWorldID;
}

bool SWDamageMeter::isTownMap() {

	return townMap.find(_worldID) != townMap.end();
}

const char* SWDamageMeter::GetWorldName() {

	if (!_historyMode)
		SWDB.GetMapName(_worldID, _mapName, MAX_MAP_LEN);
	else
		SWDB.GetMapName(_historyWorldID, _mapName, MAX_MAP_LEN);

	return _mapName;
}

void SWDamageMeter::SetAggro(uint32_t id, uint32_t targetedId)
{
	if (CheckPlayer(id)) {
		return;
	}

	SW_DB2_STRUCT* db = DAMAGEMETER.GetMonsterDB(id);
	if (db != nullptr) {
		uint32_t db2 = db->_db2;

		// usually type 4 only 1 monster
		if (changeAggroIdList.find(db2) != changeAggroIdList.end() || db->_type == 4) {
			_aggroedId = targetedId;

			SW_PLAYER_METADATA* metaData = GetPlayerMetaData(targetedId);
			for (auto itr = _playerMetadata.begin(); itr != _playerMetadata.end(); itr++)
				itr->second->UpdateAggroTime(metaData == itr->second);
		}
	}
}

uint32_t SWDamageMeter::GetAggro()
{
	return _aggroedId;
}

void SWDamageMeter::SetMyID(uint32_t id) {
	_myID = id;

#if DEBUG_DAMAGEMETER_MYID == 1
	LogInstance.WriteLog("[DEBUG] [Set MyID] [My ID = %08x]", _myID);
#endif
}

uint32_t SWDamageMeter::GetMyID(bool useHistoryID) {
	if (useHistoryID && _historyMode)
		return _historyMyID;
	return _myID;
}

bool SWDamageMeter::CheckPlayer(uint32_t id) {
	if (id < 10000000)
		return TRUE;
	else
		return FALSE;
}

std::vector<SWDamagePlayer*>* SWDamageMeter::GetPlayerInfoByHistory()
{
	if (_historyMode)
		return &_historyPlayerInfo;
	else
		return &_playerInfo;
}

std::vector<SWDamagePlayer*>::const_iterator SWDamageMeter::GetPlayerInfo(uint32_t id) {

	auto itr = GetPlayerInfoByHistory()->begin();
	for (; itr != GetPlayerInfoByHistory()->end(); itr++) {
		if ((*itr)->GetID() == id) {
			return itr;
		}
	}

	return itr;
}

uint64_t SWDamageMeter::GetPlayerTotalDamage() {

	uint64_t playerTotalDamage = 0;

	for (auto itr = GetPlayerInfoByHistory()->begin(); itr != GetPlayerInfoByHistory()->end(); itr++) {
		playerTotalDamage += (*itr)->GetDamage();
	}

	return playerTotalDamage;
}

std::vector<SWDamagePlayer*>::const_iterator SWDamageMeter::begin() {
	return GetPlayerInfoByHistory()->begin();
}

std::vector<SWDamagePlayer*>::const_iterator SWDamageMeter::end() {
	return GetPlayerInfoByHistory()->end();
}

const size_t SWDamageMeter::size() {
	return GetPlayerInfoByHistory()->size();
}

std::unordered_map<uint32_t, SWDamageMeter::SW_PLAYER_METADATA*>* SWDamageMeter::GetPlayerMetaDataByHistory()
{
	if (_historyMode)
		return &_historyPlayerMetadata;
	else
		return &_playerMetadata;
}

SWDamageMeter::SW_PLAYER_METADATA* SWDamageMeter::GetPlayerMetaData(uint32_t id)
{
	auto search = GetPlayerMetaDataByHistory()->find(id);
	if (search == GetPlayerMetaDataByHistory()->end()) {
		return nullptr;
	}
	return search->second;
}

bool SWDamageMeter::isRun() {
	return _timer.isRun();
}
uint64_t SWDamageMeter::GetStartTime() {
	return _timer._startTimePoint;
}
void SWDamageMeter::Suspend() {

	if (_historyMode) {
		Restore();
		_historyMode = FALSE;
	}

	_timer.Suspend();

	for (auto itr = _playerMetadata.begin(); itr != _playerMetadata.end(); itr++) {
		itr->second->MeterSuspended();
	}

	PLOTWINDOW.End();
}

void SWDamageMeter::Start() {

	if (_historyMode) {
		Restore();
		_historyMode = FALSE;
	}
	_timer.Run();
	PLOTWINDOW.Start();
}

void SWDamageMeter::Clear() {
	bool clearOwnerAndDB = true;
	if (!_historyMode) {
		if (_playerInfo.size() > 0) {
			std::unordered_map<uint32_t, SW_PLAYER_METADATA*> newHistoryPlayerMeta;
			std::vector<SW_DB2_STRUCT*> newDB;
			for (auto itr = _playerMetadata.begin(); itr != _playerMetadata.end(); itr++) {
				itr->second->MeterReseted();

				// copy this time player to history
				for (auto itr2 = _playerInfo.begin(); itr2 != _playerInfo.end(); itr2++) {
					if ((*itr2)->GetID() == itr->first) {
						SW_PLAYER_METADATA* newMeta = new SW_PLAYER_METADATA;
						memcpy_s(newMeta, sizeof(SW_PLAYER_METADATA), itr->second, sizeof(SW_PLAYER_METADATA));
						newHistoryPlayerMeta.emplace(itr->first, newMeta);
						break;
					}
				}
			}
			for (auto itr2 = _dbInfo.begin(); itr2 != _dbInfo.end(); itr2++)
			{
				SW_DB2_STRUCT* newStruct = new SW_DB2_STRUCT;
				memcpy_s(newStruct, sizeof(SW_DB2_STRUCT), *itr2, sizeof(SW_DB2_STRUCT));
				newDB.push_back(newStruct);
			}

			BUFFMETER.EndAllBuff();

			HISTORY_DATA* hd = new HISTORY_DATA;
			hd->_playerHistory = _playerInfo;
			hd->_dbHistory = newDB;
			hd->_buffHistory = BUFFMETER.GetPlayerInfo();
			hd->_plotHistory = PLOTWINDOW.GetPlotInfo();
			hd->_playerMetadata = newHistoryPlayerMeta;
			hd->_combatIF = COMBATMETER.Get();

			HISTORY_INFO* pHI = new HISTORY_INFO;
			pHI->Setup(hd, DAMAGEMETER.GetWorldID(), DAMAGEMETER.GetTime(), DAMAGEMETER.GetMyID(), _testMode, nullptr, _realClearTime);

			if (_testMode)
			{
				ClearInfo(FALSE);
				pHI->Clear();
				delete pHI;
			}
			else {
				HISTORY.push_back(pHI);

				if (UIOPTION.isTeamTALF() && GetWorldID() == 22061) {
					ClearDB();

					for (auto itr = hd->_dbHistory.begin(); itr != hd->_dbHistory.end(); itr++) {
						SW_DB2_STRUCT* newDB = new SW_DB2_STRUCT;
						memcpy_s(newDB, sizeof(SW_DB2_STRUCT), *itr, sizeof(SW_DB2_STRUCT));
						_dbInfo.push_back(newDB);
					}

					clearOwnerAndDB = false;
				}
				ClearInfo(clearOwnerAndDB);
			}
		}
		else {
			// not player data
			COMBATMETER.Clear(TRUE);
		}
		_mazeEnd = FALSE;
	}
	else {
		Restore();
		_historyMode = FALSE;
	}
	_testMode = FALSE;
	_timer.Stop();
}

void SWDamageMeter::Toggle() {

	if (isRun())
		Suspend();
	else
		Start();
}

uint64_t SWDamageMeter::GetTime() {

	if (!_historyMode)
		return _timer.GetTime();
	else
		return _historyTime;
}

void SWDamageMeter::SetMazeState(bool end) {
	_mazeEnd = end;
	if (end) {
		PLOTWINDOW.End();
		BUFFMETER.EndAllBuff();
	}
}

void SWDamageMeter::Restore() {

	if (!_historyMode)
		return;

	_currentHistoryId = -1;
	ClearInfo();
}

void SWDamageMeter::ClearInfo(bool clear)
{
	if (!_historyMode) {
		_playerInfo.clear();
	}

	if (clear) {
		_ownerInfo.clear();
		ClearDB();
	}

	_playerUseAwaken.clear();
	PLOTWINDOW.Clear();
	BUFFMETER.Clear();
	COMBATMETER.End();
	COMBATMETER.Clear();
	UTILLWINDOW.ClearCombatTemp();

	_aggroedId = 0;
	_currentHistoryId = -1;
	_historyHI = nullptr;
	_realClearTime = 0;
}

void SWDamageMeter::ClearDB()
{
	for (auto itr2 = _dbInfo.begin(); itr2 != _dbInfo.end(); itr2++)
	{
		delete (*itr2);
	}
	_dbInfo.clear();
}

void SWDamageMeter::SetHistory(LPVOID pHi) {

	if (pHi == nullptr)
		return;

	HISTORY_INFO* hi = (HISTORY_INFO*)pHi;

	if (!_historyMode) {
		Clear();
	}

	if (std::find(HISTORY.begin(), HISTORY.end(), hi) == HISTORY.end())
		return;

	_historyPlayerInfo = hi->_historyData->_playerHistory;
	_historyPlayerMetadata = hi->_historyData->_playerMetadata;
	_historyDbInfo = hi->_historyData->_dbHistory;
	BUFFMETER.SetPlayerInfo(hi->_historyData->_buffHistory);
	PLOTWINDOW.SetPlotInfo(hi->_historyData->_plotHistory);
	COMBATMETER.Set(hi->_historyData->_combatIF);

	_historyWorldID = hi->_worldID;
	_historyTime = hi->_time;
	_historyMyID = hi->_myID;
	_historyHI = hi;

	_historyMode = TRUE;
}

bool SWDamageMeter::isHistoryMode()
{
	return _historyMode;
}