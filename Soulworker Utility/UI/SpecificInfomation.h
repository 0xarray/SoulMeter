#pragma once
#include <unordered_set>

class SpecificInformation : public MemoryPool<SpecificInformation, 10> {
public:
	// The enemy picker either follows a single mob, everything the player hit,
	// or a hand-picked subset.
	enum class MonsterFilter { Single, All, Custom };

private:
	float _globalFontScale;
	float _columnFontScale;
	float _tableFontScale;

	uint32_t _playerID;

	float _tableTime;
	float _accumulatedTime;

	// Skill Details
	uint32_t _monsterID_SKILL;

	MonsterFilter _monsterFilter;
	std::unordered_set<uint32_t> _customMonsters;
	bool _customWindowOpen;
	char _comboPreview[128];

	// Skills of every selected monster merged into one row per skill, rebuilt
	// each frame so it can never outlive the objects it was summed from.
	// _damage follows the enemy selection, _totalDamage ignores it.
	struct SKILL_TOTAL {
		uint32_t _id;
		const char* _name;
		uint64_t _damage;
		uint64_t _totalDamage;
		uint32_t _hitCount;
		uint32_t _critHitCount;
		bool _inSelection;
	};
	std::vector<SKILL_TOTAL> _skillTotals;
	uint64_t _skillTotalsDamage;

	char _skillName[64];

	bool IsMonsterSelected(uint32_t monsterID) const;
	void BuildSkillTotals();
	void FormatDamage(uint64_t damage, char* dest, size_t destLen);

	void UpdateSkillInfo();
	void UpdateSkillTotalInfo();
	void UpdateMonsterCombo();
	void UpdateMonsterFilterWindow(int64_t index);
	void UpdateSkillTable();
	void UpdateSkillTotalTable();

	// Buff Aggregation Information
	void UpdateBuffMeter();
	void UpdateBuffTable();

	void DrawBar(float window_Width, float percent, ImU32 color);
	void SetupFontScale();
	void Clear();

	SpecificInformation() : _playerID(0), _monsterID_SKILL(0), _monsterFilter(MonsterFilter::Single), _customWindowOpen(false), _comboPreview(), _skillTotalsDamage(0), _globalFontScale(0), _columnFontScale(0), _tableFontScale(0), _tableTime(0), _accumulatedTime(0) {}
public:
	SpecificInformation(uint32_t playerID);
	~SpecificInformation();

	// The local player gets a new id on every world change, so the window has
	// to be able to follow it instead of being thrown away and reopened.
	void SetPlayerID(uint32_t playerID);
	void ResetMonsterSelection();

	void Update(bool* open, int64_t index);
};
