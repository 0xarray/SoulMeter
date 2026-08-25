#pragma once
#include ".\Damage Meter\SWDamageMonster.h"
#include ".\Soulworker Packet\PacketInfo.h"
#include ".\Damage Meter\MonsterList.h"
#include <unordered_set>
#include <map>
#include ".\Third Party\FlatBuffers\include\SW_HISTORY_.h"

using namespace SoulMeterFBS::History;

#define DEBUG_DAMAGE_PLAYER 0

struct SkillCount {
	unsigned short _count;
	unsigned short _in_full_ab_count;
};

enum BroochProc {
	BROOCH_FEVER,
	BROOCH_FURY,
	BROOCH_BACKSTEP,
	BROOCH_TECHNIC
};

class SWDamagePlayer : public MemoryPool<SWDamagePlayer, 30> {
private:
	uint32_t _id;
	uint64_t _damage;
	uint64_t _soulstoneDamage;
	unsigned short _hitCount;
	unsigned short _critHitCount;
	unsigned short _maxCombo;
	unsigned short _hitCountForCritRate;
	unsigned short _critHitCountForCritRate;
	unsigned short _missCount;
	unsigned short _soulstoneCount;
	uint64_t _damageForSoulstone;
	uint64_t _soulstoneDamageForSoulstone;
	unsigned short _hitCountForArmorBreak;
	float _armorBreak;
	float _sg;
	float _stamina;
	float _sv;
	float _attackSpeed;
	float _partialDamage;
	unsigned short _getHitAll;
	unsigned short _getHit;
	unsigned short _getHitBS;
	unsigned short _getHitMissed;
	unsigned short _getHitMissedReal;
	float _enlightenSum;
	unsigned short _gigaEnlightenProc;
	unsigned short _teraEnlightenProc;
	unsigned short _teraFeverProc = 0;
	unsigned short _furyProc = 0;
	unsigned short _backstepProc = 0;
	unsigned short _technicProc = 0;
	unsigned short _skillCounts = 0;
	unsigned short _dodgeCounts = 0;
	unsigned short _deathCounts = 0;

	double _historyAvgAB = 0.0;
	double _historyAvgABU = 0.0;
	double _historyAvgBD = 0.0;
	

	double _historyLosedHP = 0.0;
	BYTE _JqStack = 0;

	double _historyABTime = 0;
	double _historyABTimeU = 0;

	double _historyAggroTime = 0;

	double _historyASTime = 0;
	double _historyAvgAS = 0.0;

	std::vector<SWDamageMonster*> _monsterInfo;
	void InsertMonsterInfo(uint32_t monsterID, uint64_t damage, uint64_t critDamage, unsigned short hitCount, unsigned short critHitCount, uint32_t skillID);
	void Sort();

protected:
	

public:
	SWDamagePlayer() : _id(0), _damage(0), _soulstoneDamage(0), _hitCount(0), _critHitCount(0), _maxCombo(0),
		_hitCountForCritRate(0), _critHitCountForCritRate(0), _soulstoneCount(0), _hitCountForArmorBreak(0),
		_armorBreak(0), _sg(0), _stamina(0), _sv(0), _attackSpeed(0), _partialDamage(0.0f) { }
	SWDamagePlayer(uint32_t id, uint64_t damage, uint64_t soulstoneDamage, SWPACKETDAMAGE_DAMAGETYPE damageType, unsigned short maxCombo, uint32_t monsterID, uint32_t skillID);
	SWDamagePlayer(uint32_t id);
	~SWDamagePlayer();

	std::map<uint32_t, SkillCount*> skillCounts;

	static bool SortFunction(SWDamagePlayer* playerA, SWDamagePlayer* playerB);

	uint32_t GetID();
	uint64_t GetDamage();
	uint64_t GetSoulstoneDamage();
	unsigned short GetHitCount();
	unsigned short GetCritHitCount();
	unsigned short GetMaxCombo();
	uint64_t GetMonsterTotalDamage();
	unsigned short GetHitCountForCritRate();
	unsigned short GetCritHitCountForCritRate();
	unsigned short GetMissCount();
	unsigned short GetSoulstoneCount();
	uint64_t GetDamageForSoulstone();
	uint64_t GetSoulStoneDamageForSoulstone();
	unsigned short GetGetHitAll();
	unsigned short GetGetHit();
	unsigned short GetGetHitBS();
	unsigned short GetGetHitMissed();
	unsigned short GetGetHitMissedReal();
	float GetEnlightenSum();
	unsigned short GetGigaEnlighten();
	unsigned short GetTeraEnlighten();
	unsigned short GetBroochProc(BroochProc type);
	unsigned short GetSkillUsed();
	unsigned short GetDodgeUsed();
	unsigned short GetDeathCount();

	double GetHistoryABTime();
	double GetHistoryABTimeU();
	void SetHistoryABTime(double historyABTime);
	void SetHistoryABTimeU(double historyABTime);

	void SetHistoryAvgAB(double historyAvgAB);
	void SetHistoryAvgABU(double historyAvgAB);
	double GetHistoryAvgAB();
	double GetHistoryAvgABU();

	void SetHistoryAvgBD(double historyAvgBD);
	double GetHistoryAvgBD();

	void SetHistoryAggroTime(double t);
	double GetHistoryAggroTime();

	double GetHistoryASTime();
	void SetHistoryASTime(double d);

	void SetHistoryAvgAS(double d);
	double GetHistoryAvgAS();


	void setHistoryLosedHP(double losedHP);
	double GetHistoryLosedHP();

	void SetJqStack(BYTE stack);
	BYTE GetJqStack();

	void AddGetDamage(uint64_t totalDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, uint32_t monsterID, uint32_t skillID);
	void AddEnlighten(float value);
	void AddBroochProc(BroochProc type);
	void AddSkillUsed(uint32_t skillId);
	void AddDodgeUsed();
	void AddDeathCount();

	std::vector<SWDamageMonster*>::const_iterator GetMonsterInfo(unsigned int id);
	std::vector<SWDamageMonster*>::const_iterator begin();
	std::vector<SWDamageMonster*>::const_iterator end();
	const size_t size();

	void AddDamage(uint64_t totalDMG, uint64_t soulstoneDMG, SWPACKETDAMAGE_DAMAGETYPE damageType, unsigned short maxCombo, uint32_t monsterID, uint32_t skillID);

	void Serialization(flatbuffers::FlatBufferBuilder& fbb, std::vector<flatbuffers::Offset<_tDamagePlayer>>& vDamagePlayer)
	{
		// _tDamageMonster
		std::vector<flatbuffers::Offset<_tDamageMonster>> vDamageMonster;
		for (auto itr = _monsterInfo.begin(); itr != _monsterInfo.end(); itr++)
			(*itr)->Serialization(fbb, vDamageMonster);

		// _tSkillCount
		std::vector<flatbuffers::Offset<_tSkillCount>> vSkillCount;
		for (auto itr = skillCounts.begin(); itr != skillCounts.end(); itr++) {
			_tSkillCountBuilder tscb(fbb);

			tscb.add__id(itr->first);
			tscb.add__count(itr->second->_count);
			tscb.add__in_full_ab_count(itr->second->_in_full_ab_count);

			vSkillCount.push_back(tscb.Finish());
		}

		auto fcvDamageMonster = fbb.CreateVector(vDamageMonster);
		auto fcvSkillCount = fbb.CreateVector(vSkillCount);

		// _tDamagePlayer
		_tDamagePlayerBuilder tdpb(fbb);

		tdpb.add__id(_id);
		tdpb.add__damage(_damage);
		tdpb.add__soulstonedamage(_soulstoneDamage);
		tdpb.add__hitcount(_hitCount);
		tdpb.add__crithitcount(_critHitCount);

		tdpb.add__maxcombo(_maxCombo);
		tdpb.add__hitcountforcritrate(_hitCountForCritRate);
		tdpb.add__crithitcountforcritrate(_critHitCountForCritRate);
		tdpb.add__misscount(_missCount);
		tdpb.add__soulstonecount(_soulstoneCount);

		tdpb.add__damageforsoulstone(_damageForSoulstone);
		tdpb.add__soulstonedamageforsoulstone(_soulstoneDamageForSoulstone);
		tdpb.add__hitcountforarmorbreak(_hitCountForArmorBreak);
		tdpb.add__armorbreak(_armorBreak);
		tdpb.add__sg(_sg);

		tdpb.add__stamina(_stamina);
		tdpb.add__sv(_sv);
		tdpb.add__attackspeed(_attackSpeed);
		tdpb.add__partialdamage(_partialDamage);
		tdpb.add__gethitall(_getHitAll);

		tdpb.add__gethit(_getHit);
		tdpb.add__gethitbs(_getHitBS);
		tdpb.add__gethitmissed(_getHitMissed);
		tdpb.add__gethitmissedreal(_getHitMissedReal);
		tdpb.add__enlightensum(_enlightenSum);

		tdpb.add__gigaenlightenproc(_gigaEnlightenProc);
		tdpb.add__teraenlightenproc(_teraEnlightenProc);
		tdpb.add__terafevernproc(_teraFeverProc);
		tdpb.add__furyproc(_furyProc);
		tdpb.add__backstepproc(_backstepProc);
		tdpb.add__technicproc(_technicProc);
		tdpb.add__skillcounts(_skillCounts);
		tdpb.add__dodgecounts(_dodgeCounts);
		tdpb.add__deathcounts(_deathCounts);

		tdpb.add__historyavgab(_historyAvgAB);
		tdpb.add__historyavgabu(_historyAvgABU);
		tdpb.add__historyavgbd(_historyAvgBD);

		tdpb.add__historylosedhp(_historyLosedHP);
		tdpb.add__jqstack(_JqStack);
		tdpb.add__historyabtime(_historyABTime);
		tdpb.add__historyabtimeu(_historyABTimeU);

		tdpb.add__monsterinfo(fcvDamageMonster);
		tdpb.add__skillcount(fcvSkillCount);

		tdpb.add__historyaggrotime(_historyAggroTime);

		tdpb.add__historyavgas(_historyAvgAS);
		tdpb.add__historyastime(_historyASTime);
		
		vDamagePlayer.push_back(tdpb.Finish());
	}
	void UnSerialization(const _tDamagePlayer* tDamagePlayer)
	{
		// _tDamageMonster
		for (auto itr = tDamagePlayer->_monsterinfo()->begin(); itr != tDamagePlayer->_monsterinfo()->end(); itr++)
		{
			SWDamageMonster* newDamageMonster = new SWDamageMonster;
			newDamageMonster->UnSerialization(*itr);

			_monsterInfo.push_back(newDamageMonster);
		}

		// _tSkillCount
		for (auto itr = tDamagePlayer->_skillcount()->begin(); itr != tDamagePlayer->_skillcount()->end(); itr++)
		{
			SkillCount* newSkillCount = new SkillCount;
			newSkillCount->_count = itr->_count();
			newSkillCount->_in_full_ab_count = itr->_in_full_ab_count();

			skillCounts[itr->_id()] = newSkillCount;
		}

		_id = tDamagePlayer->_id();
		_damage = tDamagePlayer->_damage();
		_soulstoneDamage = tDamagePlayer->_soulstonedamage();
		_hitCount = tDamagePlayer->_hitcount();
		_critHitCount = tDamagePlayer->_crithitcount();

		_maxCombo = tDamagePlayer->_maxcombo();
		_hitCountForCritRate = tDamagePlayer->_hitcountforcritrate();
		_critHitCountForCritRate = tDamagePlayer->_crithitcountforcritrate();
		_missCount = tDamagePlayer->_misscount();
		_soulstoneCount = tDamagePlayer->_soulstonecount();

		_damageForSoulstone = tDamagePlayer->_damageforsoulstone();
		_soulstoneDamageForSoulstone = tDamagePlayer->_soulstonedamageforsoulstone();
		_hitCountForArmorBreak = tDamagePlayer->_hitcountforarmorbreak();
		_armorBreak = tDamagePlayer->_armorbreak();
		_sg = tDamagePlayer->_sg();

		_stamina = tDamagePlayer->_stamina();
		_sv = tDamagePlayer->_sv();
		_attackSpeed = tDamagePlayer->_attackspeed();
		_partialDamage = tDamagePlayer->_partialdamage();
		_getHitAll = tDamagePlayer->_gethitall();

		_getHit = tDamagePlayer->_gethit();
		_getHitBS = tDamagePlayer->_gethitbs();
		_getHitMissed = tDamagePlayer->_gethitmissed();
		_getHitMissedReal = tDamagePlayer->_gethitmissedreal();
		_enlightenSum = tDamagePlayer->_enlightensum();

		_gigaEnlightenProc = tDamagePlayer->_gigaenlightenproc();
		_teraEnlightenProc = tDamagePlayer->_teraenlightenproc();
		_teraFeverProc = tDamagePlayer->_terafevernproc();
		_furyProc = tDamagePlayer->_furyproc();
		_backstepProc = tDamagePlayer->_backstepproc();
		_technicProc = tDamagePlayer->_technicproc();
		_skillCounts = tDamagePlayer->_skillcounts();
		_dodgeCounts = tDamagePlayer->_dodgecounts();
		_deathCounts = tDamagePlayer->_deathcounts();

		_historyAvgAB = tDamagePlayer->_historyavgab();
		_historyAvgABU = tDamagePlayer->_historyavgabu();
		_historyAvgBD = tDamagePlayer->_historyavgbd();

		_historyLosedHP = tDamagePlayer->_historylosedhp();
		_JqStack = tDamagePlayer->_jqstack();
		_historyABTime = tDamagePlayer->_historyabtime();
		_historyABTimeU = tDamagePlayer->_historyabtimeu();

		_historyAggroTime = tDamagePlayer->_historyaggrotime();

		_historyAvgAS = tDamagePlayer->_historyavgas();
		_historyASTime = tDamagePlayer->_historyastime();
	}
};