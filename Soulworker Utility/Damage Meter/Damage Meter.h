#pragma once
#include "pch.h"

#include ".\Damage Meter\Timer.h"
#include ".\Damage Meter\SWDamagePlayer.h"
#include ".\UI\PlotWindow.h"
#include ".\Damage Meter\MonsterList.h"
#include ".\UI\Option.h"
#include <unordered_map>
#include ".\Third Party\FlatBuffers\include\SW_HISTORY_.h"
#include "SimpleIni.h"
using namespace SoulMeterFBS::History;

#define MAX_NAME_LEN 64
//#define MAX_MAP_LEN 32
#define MAX_MAP_LEN 64

#define PLAYER_JOB_CANT_FIND 99

#ifdef _DEBUG
#define DEBUG_DAMAGEMETER_OWNER_ID 0
#define DEBUG_DAMAGEMETER_DB 0
#define DEBUG_DAMAGEMETER_WORLD 0
#define DEBUG_DAMAGEMETER_MYID 0
#define DEBUG_DAMAGEMETER_PLAYERMETA 0
#endif

// 
#define DAMAGEMETER SWDamageMeter::getInstance()

//  forward declaration
// https://stackoverflow.com/questions/41502310/c-inclusion-of-typedef-struct-from-inside-a-class
// https://en.cppreference.com/w/cpp/language/class#Forward_declaration

typedef struct _SW_OWNER_ID_STRUCT {
	uint32_t _id;
	uint32_t _owner_id;
}SW_OWNER_ID_STRUCT;

typedef struct _SW_DB2_STRUCT {
	uint32_t _id;
	uint32_t _db2;
	int32_t _type;
}SW_DB2_STRUCT;

struct ImFontObj
{
	std::string path;
	std::string filename;
	bool selectable = false;
};

static std::vector<uint32_t> _dwSkills({

	// Haru
	15000111,
	15000211,
	16000111, // Frenzy
	15130111, // ??
	17000111, // EX

	// Erwin
	25000111,
	25000211,
	26000111,
	25130111, // ??
	27000111,

	// Lily
	35000111,
	35000211,
	36000111,
	35130111, // ??
	37000111,

	// Jin
	45000111,
	45000211,
	46000111,
	45130111, // ??
	47000111,

	// Stella
	55000111,
	55000211,
	56000111,
	55130111, // ??
	57000111,

	// Iris
	65000111,
	65000211,
	66000111,
	67000111,

	// Chii
	75000111,
	75000211,
	76000111,
	77000111,

	// Ephnel
	85000111,
	85000211,
	86000111,
	87000111,

	// Nabi
	95000111,
	95000211,
	96000111,
	97000111,

	// Dhana
	105000111,
	105000211,
	106000111, // Desire
	107000111, // EX

	});

class SWDamageMeter : public Singleton<SWDamageMeter> {
public: typedef struct _SW_PLAYER_METADATA {
private:
	float _armorBreak = 0.0;
	float _sg = 0.0;
	float _stamina = 0.0;
	float _sv = 0.0;
	float _attackSpeed = 0.0;
	float _partialDamage = 0.0;
	float _maxHP = 0.0;
	float _currentHP = 0.0;
	float _maxAttack = 0.0;
	float _critDamage = 0.0;
	float _bossDamage = 0.0;
	
public:
	uint32_t _id = 0;
	char _name[MAX_NAME_LEN];
	BYTE _job = 0;
	uint64_t _avgABSum = 0;
	uint64_t _avgABPreviousTime = 0;

	uint64_t _avgABSumU = 0;
	uint64_t _avgABPreviousTimeU = 0;

	uint64_t _avgBDSum = 0;
	uint64_t _avgBDPreviousTime = 0;

	uint64_t _avgASSum = 0;
	uint64_t _avgASPreviousTime = 0;

	double _losedHp = 0;

	bool _fullABStarted = false;
	uint64_t _fullABStartTime = 0;
	uint64_t _fullABEndTime = 0;
	double _fullABPrevTime = 0;
	double _fullABTime = 0;

	bool _AggroStarted = false;
	uint64_t _AggroStartTime = 0;
	uint64_t _AggroEndTime = 0;
	double _AggroPrevTime = 0;
	double _AggroTime = 0;

	bool _fullASStarted = false;
	uint64_t _fullASStartTime = 0;
	uint64_t _fullASEndTime = 0;
	double _fullASPrevTime = 0;
	double _fullASTime = 0;


	_SW_PLAYER_METADATA() {
		_id = 0;
		_job = 0;
		_avgABSum = 0;
		_avgABPreviousTime = 0;
		_avgABPreviousTimeU = 0;
		_avgBDSum = 0;
		_avgBDPreviousTime = 0;
	}

	void UpdateStat(unsigned short statType, float statValue) {
		switch (statType) {
		case StatType::SG:
			_sg = statValue;
			break;
		case StatType::Stamina:
			_stamina = statValue;
			break;
		case StatType::SV:
			_sv = statValue;
			break;
		case StatType::AttackSpeed:
			_attackSpeed = statValue;
			if (DAMAGEMETER.isRun()) {
				uint64_t time = (uint64_t)((double)DAMAGEMETER.GetTime()); // timer time
				float correctedAS = (_attackSpeed > 250) ? 250 : _attackSpeed;
				_avgASSum += static_cast<uint64_t>((time - _avgASPreviousTime) * correctedAS);
				_avgASPreviousTime = time;

				if (_attackSpeed >= 250) {
					if (!_fullASStarted) {
						_fullASStarted = true;
						_fullASStartTime = time;
						_fullASPrevTime = _fullASTime;
					}
					_fullASEndTime = time;
				}
				else if (_fullASStarted) {
					_fullASStarted = false;
					_fullASEndTime = time;
				}
				CalcFullASTime();
			}
			break;
		case StatType::PartialDamage:
			_partialDamage = statValue;
			break;
		case StatType::ArmorBreak:
			_armorBreak = statValue;
			if (DAMAGEMETER.isRun()) {
				uint64_t time = (uint64_t)((double)DAMAGEMETER.GetTime()); // timer time
				float correctedAB = (_armorBreak > 100) ? 100 : _armorBreak;
				_avgABSum += static_cast<uint64_t>((time - _avgABPreviousTime) * correctedAB);
				_avgABSumU += static_cast<uint64_t>((time - _avgABPreviousTime) * _armorBreak);
				_avgABPreviousTime = time;
				_avgABPreviousTimeU = time;

				if (_armorBreak >= 100) {
					if (!_fullABStarted) {
						_fullABStarted = true;
						_fullABStartTime = time;
						_fullABPrevTime = _fullABTime;
					}
					_fullABEndTime = time;
				}
				else if (_fullABStarted) {
					_fullABStarted = false;
					_fullABEndTime = time;
				}
				CalcFullABTime();
			}
			break;
		case StatType::CurrentHP:
			if (_currentHP > 0.0 && _currentHP > statValue) {
				_losedHp += ((double)_currentHP - (double)statValue);
			}
			_currentHP = statValue;
			break;
		case StatType::MaxHP:
			_maxHP = statValue;
			break;
		case StatType::MinAttack:
			_maxAttack = static_cast<float>(statValue * 1.25);
			break;
		case StatType::MaxAttack:
			_maxAttack = statValue;
			break;
		case StatType::CritDamage:
			_critDamage = statValue;
			break;

		default:
			//LogInstance.WriteLog("[DEBUG] [statType = %x], [statValue = %f]\n", statType, statValue);
			break;
		}
	}

	void UpdateSpecialStat(unsigned short statType, float statValue) {
		switch (statType) {
		case SpecialStatType::BossDamageAddRate:
			_bossDamage = statValue;
			if (DAMAGEMETER.isRun()) {
				uint64_t time = (uint64_t)((double)DAMAGEMETER.GetTime());
				_avgBDSum += static_cast<uint64_t>((time - _avgBDPreviousTime) * _bossDamage);
				_avgBDPreviousTime = time;
			}
			break;
		default:
			//LogInstance.WriteLog("[DEBUG] [statType = %x], [statValue = %f]\n", statType, statValue);
			break;
		}
	}

	void UpdateAggroTime(bool isSet)
	{
		uint64_t time = (uint64_t)((double)DAMAGEMETER.GetTime()); // timer time

		if (isSet) {
			if (!_AggroStarted) {
				_AggroStarted = true;
				_AggroStartTime = time;
				_AggroPrevTime = _AggroTime;
			}
			_AggroEndTime = time;
		}
		else if (_AggroStarted) {
			_AggroStarted = false;
			_AggroEndTime = time;
		}
		CalcAggroTime();
	}

	void MeterSuspended() {
		uint64_t currentTime = (uint64_t)DAMAGEMETER.GetTime();

		float correctedAB = (_armorBreak > 100) ? 100 : _armorBreak;
		_avgABSum += static_cast<uint64_t>((currentTime - _avgABPreviousTime) * correctedAB);
		_avgABSumU += static_cast<uint64_t>((currentTime - _avgABPreviousTimeU) * _armorBreak);
		_avgABPreviousTime = currentTime;
		_avgABPreviousTimeU = currentTime;

		_avgBDSum += static_cast<uint64_t>((currentTime - _avgBDPreviousTime) * _bossDamage);
		_avgBDPreviousTime = currentTime;

		float correctedAS = (_attackSpeed > 250) ? 250 : _attackSpeed;
		_avgASSum += static_cast<uint64_t>((currentTime - _avgASPreviousTime) * correctedAS);
		_avgASPreviousTime = currentTime;

		if (_fullABStarted) {
			_fullABStarted = false;
			_fullABEndTime = currentTime;
		}
		CalcFullABTime();

		if (_AggroStarted) {
			_AggroStarted = false;
			_AggroEndTime = currentTime;
		}
		CalcAggroTime();

		if (_fullASStarted) {
			_fullASStarted = false;
			_fullASEndTime = currentTime;
		}
		CalcFullASTime();
	}

	void MeterReseted() {
		auto player = DAMAGEMETER.GetPlayerInfo(_id);
		if (player != DAMAGEMETER.end()) {
			uint64_t currentTime = (uint64_t)DAMAGEMETER.GetTime();

			(*player)->setHistoryLosedHP(_losedHp);

			if (_AggroStarted) {
				_AggroStarted = false;
				_AggroEndTime = currentTime;
				CalcAggroTime();
			}
			(*player)->SetHistoryAggroTime(_AggroTime);

			if (_id == DAMAGEMETER.GetMyID()) {

				uint64_t avgTimeDifference = currentTime - _avgABPreviousTime;
				double currentAB = GetStat(StatType::ArmorBreak);
				currentAB = currentAB > 100.0 ? 100.0 : currentAB; // 
				uint64_t calculatedAvgAB = static_cast<uint64_t>((_avgABSum + avgTimeDifference * currentAB));
				(*player)->SetHistoryAvgAB((double)calculatedAvgAB / currentTime);

				uint64_t avgTimeDifferenceU = currentTime - _avgABPreviousTimeU;
				double currentABU = GetStat(StatType::ArmorBreak);
				uint64_t calculatedAvgABU = static_cast<uint64_t>((_avgABSumU + avgTimeDifferenceU * currentABU));
				(*player)->SetHistoryAvgABU((double)calculatedAvgABU / currentTime);

				avgTimeDifference = currentTime - _avgBDPreviousTime;
				uint64_t calculatedAvgBD = static_cast<uint64_t>((_avgBDSum + avgTimeDifference * _bossDamage));
				(*player)->SetHistoryAvgBD((double)calculatedAvgBD / currentTime);

				double currentAS = GetStat(StatType::AttackSpeed);
				currentAS = currentAS > 250 ? 250.0 : currentAS;
				avgTimeDifference = currentTime - _avgASPreviousTime;
				uint64_t calculatedAvgAS = static_cast<uint64_t>((_avgASSum + avgTimeDifference * currentAS));
				(*player)->SetHistoryAvgAS((double)calculatedAvgAS / currentTime);

				if (_fullABStarted) {
					_fullABStarted = false;
					_fullABEndTime = currentTime;
					CalcFullABTime();
				}
				(*player)->SetHistoryABTime(_fullABTime);

				if (_fullASStarted) {
					_fullASStarted = false;
					_fullASEndTime = currentTime;
					CalcFullASTime();
				}
				(*player)->SetHistoryASTime(_fullASTime);
			}
		}

		_losedHp = 0.0;

		_avgABSum = 0;
		_avgABPreviousTime = 0;

		_avgABSumU = 0;
		_avgABPreviousTimeU = 0;

		_avgBDSum = 0;
		_avgBDPreviousTime = 0;

		_avgASSum = 0;
		_avgASPreviousTime = 0;

		_fullABStarted = false;
		_fullABStartTime = 0;
		_fullABEndTime = 0;
		_fullABPrevTime = 0;
		_fullABTime = 0;

		_AggroStarted = false;
		_AggroStartTime = 0;
		_AggroEndTime = 0;
		_AggroPrevTime = 0;
		_AggroTime = 0;

		_fullASStarted = false;
		_fullASStartTime = 0;
		_fullASEndTime = 0;
		_fullASPrevTime = 0;
		_fullASTime = 0;
	}

	void CalcFullABTime(uint64_t endTime = NULL)
	{
		if (endTime == NULL || _armorBreak < 100)
			endTime = _fullABEndTime;

		_fullABTime = _fullABPrevTime + ((double)(endTime - _fullABStartTime) / 1000);
	}

	void CalcFullASTime(uint64_t endTime = NULL)
	{
		if (endTime == NULL || _attackSpeed < 250)
			endTime = _fullASEndTime;

		_fullASTime = _fullASPrevTime + ((double)(endTime - _fullASStartTime) / 1000);
	}

	void CalcAggroTime(uint64_t endTime = NULL)
	{
		if (endTime == NULL || !_AggroStarted)
			endTime = _AggroEndTime;

		_AggroTime = _AggroPrevTime + ((double)(endTime - _AggroStartTime) / 1000);
	}

	float GetStat(unsigned short statType) {

		switch (statType) {
		case StatType::SG:
			return _sg;
		case StatType::Stamina:
			return _stamina;
		case StatType::SV:
			return _sv;
		case StatType::AttackSpeed:
			return _attackSpeed;
		case StatType::PartialDamage:
			return _partialDamage;
		case StatType::ArmorBreak:
			return _armorBreak;
		case StatType::MaxAttack:
			return _maxAttack;
		case StatType::CritDamage:
			return _critDamage;
		}

		return -1;
	}

	float GetSpecialStat(unsigned short statType) {

		switch (statType) {
		case SpecialStatType::BossDamageAddRate:
			return _bossDamage;
		}

		return -1;
	}

	void Serialization(flatbuffers::FlatBufferBuilder& fbb, std::vector<flatbuffers::Offset<_tPlayerMetaData>>& vPlayerMetaData)
	{
		auto fcsName = fbb.CreateString(_name);

		_tPlayerMetaDataBuilder tpmdb(fbb);

		tpmdb.add__armorbreak(GetStat(StatType::ArmorBreak));
		tpmdb.add__sg(GetStat(StatType::SG));
		tpmdb.add__stamina(GetStat(StatType::Stamina));
		tpmdb.add__sv(GetStat(StatType::SV));
		tpmdb.add__attackspeed(GetStat(StatType::AttackSpeed));
		tpmdb.add__partialdamage(GetStat(StatType::PartialDamage));
		tpmdb.add__maxhp(GetStat(StatType::MaxHP));
		tpmdb.add__currenthp(GetStat(StatType::CurrentHP));
		tpmdb.add__maxattack(GetStat(StatType::MaxAttack));
		tpmdb.add__critdamage(GetStat(StatType::CritDamage));

		tpmdb.add__bossdamage(GetSpecialStat(SpecialStatType::BossDamageAddRate));

		tpmdb.add__id(_id);
		tpmdb.add__name(fcsName);
		tpmdb.add__job(_job);

		vPlayerMetaData.push_back(tpmdb.Finish());
	}

	void UnSerialization(const _tPlayerMetaData* tPlayerMetaData)
	{
		_armorBreak = tPlayerMetaData->_armorbreak();
		_sg = tPlayerMetaData->_sg();
		_stamina = tPlayerMetaData->_stamina();
		_sv = tPlayerMetaData->_sv();
		_attackSpeed = tPlayerMetaData->_attackspeed();
		_partialDamage = tPlayerMetaData->_partialdamage();
		_maxHP = tPlayerMetaData->_maxhp();
		_currentHP = tPlayerMetaData->_currenthp();
		_maxAttack = tPlayerMetaData->_maxattack();
		_critDamage = tPlayerMetaData->_critdamage();
		_bossDamage = tPlayerMetaData->_bossdamage();

		_id = tPlayerMetaData->_id();
		strcpy_s(_name, tPlayerMetaData->_name()->c_str());
		_job = tPlayerMetaData->_job();
	}

}SW_PLAYER_METADATA;
private:
	std::vector<SWDamagePlayer*> _playerInfo;
	std::vector<SW_OWNER_ID_STRUCT*> _ownerInfo;
	std::vector<SW_DB2_STRUCT*> _dbInfo;
	std::unordered_map<uint32_t, SW_PLAYER_METADATA*> _playerMetadata;

	std::vector<SWDamagePlayer*> _historyPlayerInfo;
	std::vector<SW_DB2_STRUCT*> _historyDbInfo;
	std::unordered_map<uint32_t, SW_PLAYER_METADATA*> _historyPlayerMetadata;

	std::vector<std::string> _extInfo;
	std::unordered_map<uint32_t, uint64_t> _playerUseAwaken;

	char _mapName[MAX_MAP_LEN];
	uint32_t _myID;
	unsigned short _worldID;

	unsigned short _historyWorldID;
	uint64_t _historyTime;
	uint32_t _historyMyID;
	LPVOID _historyHI;

	uint32_t _aggroedId;
	uint32_t _realClearTime;

	bool _mazeEnd;
	bool _historyMode;

	void InsertPlayerInfo(uint32_t id, uint64_t totalDMG, uint64_t soulstoneDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, unsigned short maxCombo, uint32_t monsterID, uint32_t skillID);
	void Sort();
	void Restore();

	Timer _timer;

	std::mutex _mutex;

	int32_t _currentHistoryId = -1;

	bool _testMode = FALSE;

public:
	int mswideness = 1;
	bool shouldRebuildAtlas = false;
	bool shouldLogMonsterStats = false;
	uint32_t _ping = 0;
	uint32_t _historyPing = 0;
	ImFontObj selectedFont;
	CSimpleIniA ini;
	SWDamageMeter() :   _myID(0), _worldID(0), _mazeEnd(0), _historyMode(0), _historyWorldID(0), _historyTime(0) {}
	~SWDamageMeter();

	void GetLock();
	void FreeLock();

	void AddDamage(uint32_t id, uint64_t totalDMG, uint64_t soulstoneDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, unsigned short maxCombo, uint32_t monsterID, uint32_t skillID);
	void AddPlayerGetDamage(uint32_t playerId, uint64_t totalDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, uint32_t monsterID, uint32_t skillID);
	void AddEnlighten(uint32_t playerId, float value);
	void AddBroochProc(uint32_t playerId, BroochProc type);
	void AddSkillUsed(uint32_t playerId, uint32_t skillId);
	void AddDodgeUsed(uint32_t playerId);
	void AddDeath(uint32_t playerId);
	void BuffIn(uint32_t playerId, unsigned short buffId, BYTE stack, uint32_t giverId);
	void BuffOut(uint32_t playerId, unsigned short buffId);

	void InsertOwnerID(uint32_t id, uint32_t owner_id);
	uint32_t GetOwnerID(uint32_t id);

	void InsertDB(uint32_t id, uint32_t db2);
	SW_DB2_STRUCT* GetMonsterDB(uint32_t id);

	void SetWorldID(unsigned short worldID);
	unsigned short GetWorldID();
	const char* GetWorldName();
	bool isTownMap();

	void SetAggro(uint32_t id, uint32_t targetedId);
	uint32_t GetAggro();

	void InsertPlayerMetadata(uint32_t id, char* str, BYTE job);
	const char* GetPlayerName(uint32_t id);
	BYTE GetPlayerJob(uint32_t id);

	void UpdateSpecialStat(uint32_t id, unsigned short statType, float statValue);
	void UpdateStat(uint32_t id, unsigned short statType, float statValue, bool isSpecial = FALSE);

	void SetMyID(uint32_t id);

	uint32_t GetMyID(bool useHistoryID = FALSE);

	bool CheckPlayer(uint32_t id);

	std::vector<SWDamagePlayer*>* GetPlayerInfoByHistory();
	std::vector<SWDamagePlayer*>::const_iterator GetPlayerInfo(uint32_t id);
	std::vector<SWDamagePlayer*>::const_iterator begin();
	std::vector<SWDamagePlayer*>::const_iterator end();
	const size_t size();

	std::unordered_map<uint32_t, SW_PLAYER_METADATA*>* GetPlayerMetaDataByHistory();

	SW_PLAYER_METADATA* GetPlayerMetaData(uint32_t id);
	SW_PLAYER_METADATA* GetPlayerMetaDataIfNotExistsCreate(uint32_t id);

	uint64_t GetPlayerTotalDamage();

	bool isRun();

	void Suspend();
	void Start();
	void Clear();
	void Toggle();
	uint64_t GetStartTime();
	uint32_t GetPing();
	void SetPing(uint32_t ping);
	uint64_t GetTime();

	void SetMazeState(bool end);
	void SetHistory(LPVOID hi);
	bool isHistoryMode();

	void ClearInfo(bool clear = FALSE);
	void ClearDB();

	int32_t GetCurrentHistoryId()
	{
		return _currentHistoryId;
	}

	void SetCurrentHistoryId(int32_t id)
	{
		_currentHistoryId = id;
	}

	void SetTestMode()
	{
		_testMode = TRUE;
	}

	void ClearTestMode()
	{
		_testMode = FALSE;
	}

	LPVOID GetHistoryHI()
	{
		return _historyHI;
	}

	void AddExtInfo(std::string str)
	{
		_extInfo.push_back(str);
	}

	void SetRealClearTime(uint32_t t)
	{
		_realClearTime = t;
	}

	void AddAwakenPlayer(uint32_t uPlayerId)
	{
		uint64_t ulTime = GetTime();
		if (_playerUseAwaken.find(uPlayerId) != _playerUseAwaken.end())
			_playerUseAwaken[uPlayerId] = ulTime;
		else
			_playerUseAwaken.insert({ uPlayerId, ulTime });
	}

	bool PlayerInAwakening(uint32_t uPlayerId)
	{
		WORD wTimeOut = 10000;
		do
		{
			auto find = _playerUseAwaken.find(uPlayerId);
			if (find != _playerUseAwaken.end())
			{
				if (GetTime() <= find->second + wTimeOut)
					return TRUE;
			}

		} while (false);

		return FALSE;
	}
};