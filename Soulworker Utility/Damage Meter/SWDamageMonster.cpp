#include "pch.h"
#include ".\Damage Meter\SWDamageSkill.h"
#include ".\Damage Meter\SWDamageMonster.h"
#include ".\Damage Meter\MySQLite.h"

#define DEBUG_DAMAGE_MONSTER 0

SWDamageMonster::SWDamageMonster(uint32_t id, uint32_t db2, uint64_t damage, uint64_t critDamage, unsigned short hitCount, unsigned short critHitCount, uint32_t skillID) {
	_id = id;
	_db2 = db2;
	_damage = damage;
	_critDamage = critDamage;
	_hitCount = hitCount;
	_critHitCount = critHitCount;

	SetNameFromDB();

	SWDB.GetMonsterType(db2, &_type);

#if DEBUG_DAMAGE_MONSTER == 1
	LogInstance.WriteLog("\t[MONSTER] [ID = %d] [DB2 = %d] [NAME = %s] [DMG = %llu] [cirDMG = %llu] [hitCount = %d] [cirtHitCount = %d]", _id, _db2, _name, _damage, _critDamage, _hitCount, _critHitCount);
#endif
	

	InsertSkillInfo(skillID, damage, critDamage, hitCount, critHitCount);
}

SWDamageMonster::~SWDamageMonster() {

	for (auto itr = _skillinfo.begin(); itr != _skillinfo.end(); itr++) {
		delete (*itr);
	}

	_skillinfo.clear();
}

void SWDamageMonster::SetNameFromDB()
{
	ZeroMemory(_name, MONSTER_NAME_LEN);
	SWDB.GetMonsterName(_db2, _name, MONSTER_NAME_LEN);
}

bool SWDamageMonster::SortFunction(SWDamageMonster* monsterA, SWDamageMonster* monsterB) {
	return monsterA->GetDamage() > monsterB->GetDamage();
}

void SWDamageMonster::AddDamage(uint64_t damage, uint64_t critDamage, unsigned short hitCount, unsigned short critHitCount, uint32_t skillID) {
	_damage += damage;
	_critDamage += critDamage;
	_hitCount += hitCount;
	_critHitCount += critHitCount;

#if DEBUG_DAMAGE_MONSTER == 1
	LogInstance.WriteLog("\t[MONSTER] [ID = %d] [DB2 = %d] [NAME = %s] [DMG = %llu] [cirDMG = %llu] [hitCount = %d] [cirtHitCount = %d]", _id, _db2, _name, _damage, _critDamage, _hitCount, _critHitCount);
#endif

	InsertSkillInfo(skillID, damage, critDamage, hitCount, critHitCount);
	Sort();
}

void SWDamageMonster::InsertSkillInfo(uint32_t skillID, uint64_t damage, uint64_t critDamage, unsigned short hitCount, unsigned short critHitCount) {

	auto itr = _skillinfo.begin();

	for (; itr != _skillinfo.end(); itr++) {
		if (skillID == (*itr)->GetID()) {
			(*itr)->AddDamage(damage, critDamage, hitCount, critHitCount);
			return;
		}
	}

	_skillinfo.push_back(new SWDamageSkill(skillID, damage, critDamage, hitCount, critHitCount));
}

void SWDamageMonster::Sort() {
	sort(_skillinfo.begin(), _skillinfo.end(), SWDamageSkill::SortFunction);
}

uint32_t SWDamageMonster::GetID() {
	return _id;
}

int32_t SWDamageMonster::GetType() {
	return _type;
}

//unsigned short SWDamageMonster::GetDB1() {
//	return _db1;
//}

uint32_t SWDamageMonster::GetDB2() {
	return _db2;
}

uint64_t SWDamageMonster::GetDamage() {
	return _damage;
}

uint64_t SWDamageMonster::GetCritDamage() {
	return _critDamage;
}

unsigned short SWDamageMonster::GetHitCount() {
	return _hitCount;
}

unsigned short SWDamageMonster::GetCritHitCount() {
	return _critHitCount;
}

const char* SWDamageMonster::GetName() {
	return _name;
}

std::vector<SWDamageSkill*>::const_iterator SWDamageMonster::begin() {
	return _skillinfo.begin();
}

std::vector<SWDamageSkill*>::const_iterator SWDamageMonster::end() {
	return _skillinfo.end();
}

const size_t SWDamageMonster::size() {
	return _skillinfo.size();
}