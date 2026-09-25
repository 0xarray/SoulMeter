#pragma once
#include ".\UI\SpecificInfomation.h"

typedef struct _SELECTED_PLAYER {
	
	uint32_t _playerID;
	bool _isSelected;
	// TRUE when this window follows the local player instead of a fixed id.
	bool _isMe;
	// Stays the same for the life of the window so the imgui window keeps its
	// position when other detail windows are removed.
	int64_t _windowIndex;
	SpecificInformation* _specificInfo;

	_SELECTED_PLAYER(uint32_t playerID, bool isSelected, bool isMe, int64_t windowIndex, SpecificInformation* specificInfo) : _playerID(playerID), _isSelected(isSelected), _isMe(isMe), _windowIndex(windowIndex), _specificInfo(specificInfo) { }
}SELECTED_PLAYER;

#define PLAYERTABLE PlayerTable::getInstance()

class PlayerTable : public Singleton<PlayerTable> {
private:
	std::vector<SELECTED_PLAYER*> _selectInfo;

	bool ToggleSelectInfo(uint32_t id);
	void ShowSelectedTable();
	void FollowMyID();
	void ClearSelectInfo(bool all);
	void BeginPopupMenu();

	void SetWindowSize();
	void SetMainWindowSize();
	void HandleResizeGrip(bool& hovered, bool& held);
	void DrawResizeGrip(bool hovered, bool held);
	float _gripGrabOffset = 0.0f;
	void PushRowSelectable();
	void StoreWindowWidth();
	void SetupFontScale();

	void DrawBar(float window_Width, float percent, ImU32 color);
	void SetupTable();
	void UpdateTable(float windowWidth);

	// Vertical mode runs UpdateTable with _collecting set: each row's cells
	// land in _vertical instead of being drawn, then get drawn transposed.
	struct VerticalPlayer {
		uint32_t id;
		const char* name;
		ImVec4 nameColor;
		ImU32 jobColor;
		int job;
		std::vector<std::string> cells;	// cells[i] is meter column i + 1
	};
	std::vector<VerticalPlayer> _vertical;
	bool _collecting = false;
	bool _wasVertical = false;

	void Cell(const char* text);
	void NextCell();
	void SetupVerticalTable();
	bool VerticalRowShown(int column);
	void ToggleVerticalRow(int column);

	float _globalFontScale;
	float _columnFontScale;
	float _tableFontScale;

	float _curWindowSize;

	bool _tableResize;
	bool _fitColumns = false;

	float _tableTime;
	float _accumulatedTime;

	int64_t _nextWindowIndex;

public:
	PlayerTable();
	~PlayerTable();

	void Update();
	void ClearTable();
	void ResizeTalbe();
	// Sizes every column to its content once there is data to measure.
	void FitColumns() { _fitColumns = true; }

	float GetTableTime()
	{
		return _tableTime;
	}

};