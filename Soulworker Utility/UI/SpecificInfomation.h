#pragma once

class SpecificInformation : public MemoryPool<SpecificInformation, 10> {
private:
	float _globalFontScale;
	float _columnFontScale;
	float _tableFontScale;

	uint32_t _playerID;

	float _tableTime;
	float _accumulatedTime;

	// Skill Details
	uint32_t _monsterID_SKILL;

	char _skillName[64];

	void UpdateSkillInfo();
	void UpdateSkillTotalInfo();
	void UpdateMonsterCombo();
	void UpdateSkillTable();
	void UpdateSkillTotalTable();

	// Buff Aggregation Information
	void UpdateBuffMeter();
	void UpdateBuffTable();

	void DrawBar(float window_Width, float percent, ImU32 color);
	void SetupFontScale();
	void Clear();

	SpecificInformation() : _playerID(0), _monsterID_SKILL(0), _globalFontScale(0), _columnFontScale(0), _tableFontScale(0), _tableTime(0), _accumulatedTime(0) {}
public:
	SpecificInformation(uint32_t playerID);
	~SpecificInformation();

	// The local player gets a new id on every world change, so the window has
	// to be able to follow it instead of being thrown away and reopened.
	void SetPlayerID(uint32_t playerID);
	void ResetMonsterSelection();

	void Update(bool* open, int64_t index);
};