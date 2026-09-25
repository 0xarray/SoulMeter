#pragma once
#include ".\Damage Meter\SWDamageSkill.h"
#include ".\Third Party\FlatBuffers\include\SW_HISTORY_.h"

using namespace SoulMeterFBS::History;

#define DEBUG_DAMAGE_MONSTER 0
#define MONSTER_NAME_LEN 64

class SWDamageMonster : public MemoryPool<SWDamageMonster, 100> {
private:
	uint32_t _id;
	uint32_t _db2;
	uint64_t _damage;
	uint64_t _critDamage;
	unsigned short _hitCount;
	unsigned short _critHitCount;
	char _name[MONSTER_NAME_LEN];
	int32_t _type;

	std::vector<SWDamageSkill*> _skillinfo;

	void InsertSkillInfo(uint32_t skillID, uint64_t damage, uint64_t critDamage, unsigned short hitCount, unsigned short critHitCount);
	void Sort();

public:
	SWDamageMonster(uint32_t id, uint32_t db2, uint64_t damage, uint64_t critDamage, unsigned short hitCount, unsigned short critHitCount, uint32_t skillID);
	SWDamageMonster() : _type(-1), _id(0), _db2(0), _damage(0), _critDamage(0), _hitCount(0), _critHitCount(0) { }
	~SWDamageMonster();

	static bool SortFunction(SWDamageMonster* monsterA, SWDamageMonster* monsterB);

	uint32_t GetID();
	int32_t GetType();
	uint32_t GetDB2();
	uint64_t GetDamage();
	uint64_t GetCritDamage();
	unsigned short GetHitCount();
	unsigned short GetCritHitCount();
	const char* GetName();

	std::vector<SWDamageSkill*>::const_iterator begin();
	std::vector<SWDamageSkill*>::const_iterator end();
	const size_t size();

	void SetNameFromDB();
	void AddDamage(uint64_t damage, uint64_t critDamage, unsigned short hitCount, unsigned short critHitCount, uint32_t skillID);

	void Serialization(flatbuffers::FlatBufferBuilder& fbb, std::vector<flatbuffers::Offset<_tDamageMonster>>& vDamageMonster)
	{
		// _tDamageSkill
		std::vector<flatbuffers::Offset<_tDamageSkill>> vSkillInfo;

		for (auto skill_itr = begin(); skill_itr != end(); skill_itr++)
			(*skill_itr)->Serialization(fbb, vSkillInfo);
		auto fcvSkillInfo = fbb.CreateVector(vSkillInfo);

		// _tDamageMonster
		_tDamageMonsterBuilder tdmb(fbb);

		tdmb.add__id(GetID());
		tdmb.add__db2(GetDB2());
		tdmb.add__damage(GetDamage());
		tdmb.add__critdamage(GetCritDamage());
		tdmb.add__hitcount(GetHitCount());
		tdmb.add__crithitcount(GetCritHitCount());
		tdmb.add__type(GetType());
		tdmb.add__skillinfo(fcvSkillInfo);

		vDamageMonster.push_back(tdmb.Finish());
	}
	void UnSerialization(const _tDamageMonster* tDamageMonster)
	{
		// _tDamageSkill
		for (auto itr = tDamageMonster->_skillinfo()->begin(); itr != tDamageMonster->_skillinfo()->end(); itr++)
		{
			SWDamageSkill* newDamageSkill = new SWDamageSkill;
			newDamageSkill->UnSerialization(*itr);

			_skillinfo.push_back(newDamageSkill);
		}

		// _tDamageMonster
		_id = tDamageMonster->_id();
		_db2 = tDamageMonster->_db2();
		_damage = tDamageMonster->_damage();
		_critDamage = tDamageMonster->_critdamage();
		_hitCount = tDamageMonster->_hitcount();
		_critHitCount = tDamageMonster->_crithitcount();
		_type = tDamageMonster->_type();

		SetNameFromDB();
	}
};